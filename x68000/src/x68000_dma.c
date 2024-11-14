/* -----------------------------------------------------------------------------------
  "SHARP X68000" DMA
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "x68000_dma.h"
#include "x68000_cpu.h"

typedef struct {
	UINT8      r[0x40];
	UINT32     MAR;
	UINT32     DAR;
	UINT32     BAR;
	UINT16     MTC;
	UINT16     BTC;
} DMA_CH;

typedef struct {
	CPUDEV*        cpu;
	MEM16HDL       mem;
	X68DMA_READYCB is_ready[4];
	void*          cbprm[4];

	DMA_CH         ch[4];
	UINT32         irq;       // IRQ�t���O�i�r�b�g�}�b�v��CH�ɑΉ��j
	UINT32         last_irq;  // �O��IRQ��������CH�ԍ�

	// �ȉ��� X68DMA_Exec() �֐����Ŋ�������̂ŃX�e�[�g�ۑ��s�v
	BOOL           in_dma;    // ����DMA���s��
	UINT32         bus_err;   // ��L�� TRUE �̊Ԃɔ��������o�X�G���[
} INFO_DMA;

// --------------------------------------------------------------------------
//   ��`��
// --------------------------------------------------------------------------
#define CSR  r[0x00]
#define CER  r[0x01]
#define DCR  r[0x04]
#define OCR  r[0x05]
#define SCR  r[0x06]
#define CCR  r[0x07]
#define NIV  r[0x25]
#define EIV  r[0x27]
#define MFC  r[0x29]
#define CPR  r[0x2D]
#define DFC  r[0x31]
#define BFC  r[0x39]
#define GCR  r[0x3F]

#define ccrSTR  0x80
#define ccrCNT  0x40
#define ccrHLT  0x20
#define ccrSAB  0x10
#define ccrINT  0x08

#define csrACT  0x08
#define csrERR  0x10
#define csrBTC  0x40
#define csrCOC  0x80

#define DMAINT(chnum)       if ( ch->CCR & ccrINT ) { \
                                dma->irq |= (1<<(chnum)); \
                                IRQ_Request(dma->cpu, IRQLINE_LINE|3, 0); \
                            }

#define DMAERR(chnum,err)   { \
                                ch->CER = err;                    \
                                ch->CSR |= csrERR;  /* ERR on */  \
                                ch->CSR &= ~csrACT; /* ACT off */ \
                                ch->CCR &= ~ccrSTR; /* STR off */ \
                                DMAINT(chnum);                    \
                            }

#define DMA_RD08(a)         Mem16_Read8BE8(dma->mem,a)
#define DMA_RD16(a)         Mem16_Read16BE8(dma->mem,a)
#define DMA_RD32(a)         Mem16_Read32BE8(dma->mem,a)
#define DMA_WR08(a,d)       Mem16_Write8BE8(dma->mem,a,d)
#define DMA_WR16(a,d)       Mem16_Write16BE8(dma->mem,a,d)
#define DMA_WR32(a,d)       Mem16_Write32BE8(dma->mem,a,d)


// --------------------------------------------------------------------------
//   �����֐�
// --------------------------------------------------------------------------
static int IsReadyDummy(void* prm)
{
	return 0;
}

static void CheckStartDMA(INFO_DMA* dma, UINT32 chnum, UINT32 old, UINT32 data)
{
	DMA_CH* ch = &dma->ch[chnum];

/*if ( chnum==3 ) {
	LOG(("DMA#%d : MAR=$%08X, DAR=$%08X, BAR=$%08X, MTC=$%04X, BTC=$%04X", chnum, ch->MAR, ch->DAR, ch->BAR, ch->MTC, ch->BTC));
}*/

	do {
		// Software Abort �`�F�b�N
		if (  data & ccrSAB ) {
			if ( /*ch->CCR & ccrSTR*/ ch->CSR & csrACT ) {  // STR�ł͂Ȃ�ACT�Ń`�F�b�N
				// COC�͗����Ċ��������ɂ͂Ȃ���ۂ��H
				ch->CSR |= csrCOC;  // COC
				// ������~�i�G���[�����j
				DMAERR(chnum, 0x11);
				break;
			} else {
				ch->CSR &= ~csrCOC; // COC������
			}
		}

		// Halt �`�F�b�N
		if ( data & ccrHLT ) {
//			ch->CSR &= ~csrACT;  // �{����ACT������͂��H Nemesis'90�Œ��q�����̂ŃR�����g�A�E�g
			break;
		}

		// ����J�n
		if ( data & ccrSTR ) {
			if ( old & ccrHLT ) {
				// ����HALT��ԂȂ�ĊJ
				ch->CSR |= csrACT;  // ���ꂢ��Ȃ��H
				X68DMA_Exec((X68DMA)dma, chnum);
			} else {
				// �V�K�J�n
				if ( ch->CSR & 0xF8 ) {
					// �^�C�~���O�G���[
					DMAERR(chnum, 0x02);
					break;
				}
				if ( (ch->OCR & 0x08)/*&&(!ch->MTC)*/ ) {
					// �A���C�^�����N�A���C�`�F�C��
					ch->MAR = DMA_RD32(ch->BAR) & 0xFFFFFF;
					ch->MTC = DMA_RD16(ch->BAR+4);
					if ( ch->OCR & 0x04 ) {
						ch->BAR = DMA_RD32(ch->BAR+6);
					} else {
						ch->BAR += 6;
						if ( !ch->BTC ) {
							// �J�E���g�G���[�i�x�[�X�J�E���^�j
							DMAERR(chnum, 0x0F);
							break;
						}
					}
				}
				if ( !ch->MTC ) {
					// �J�E���g�G���[�i�������J�E���^�j
					DMAERR(chnum, 0x0D);
					break;
				}
				ch->CSR |= csrACT;
				ch->CCR &= ~(ccrSTR | ccrSAB);  // ACT�����������_��STR������iSTAR MOBILE�j cnt�͗��Ƃ���OVER TAKE��PCM����Ȃ��Ȃ�
				ch->CER  = 0x00;     // XXX ErrorCode�͎��ۂ͏������Ɏc�葱����H
				X68DMA_Exec((X68DMA)dma, chnum);  // �J�n����ɃJ�E���^�����ē���`�F�b�N����ꍇ������̂ŁA�����������s���Ă���
			}
		}

		// �R���e�B�j���[����
		if ( ( data & ccrCNT ) && ( !ch->MTC ) ) {
			if ( /*ch->CCR & ccrSTR*/ ch->CSR & csrACT ) {  // STR�ł͂Ȃ�ACT�Ń`�F�b�N
				if ( ch->CCR & ccrCNT ) {
					DMAERR(chnum, 0x02);
				} else if ( ch->OCR & 0x08 ) {
					// �A���C�^�����N�A���C�`�F�C��
					DMAERR(chnum, 0x01);
				} else {
					ch->MAR = ch->BAR;
					ch->MTC = ch->BTC;
					ch->CSR |= csrACT;  // ���ꂢ��H
					ch->BAR = 0;
					ch->BTC = 0;
					if ( !ch->MAR ) {
						ch->CSR |= csrBTC;  // �u���b�N�]���I���r�b�g�^���荞��
						DMAINT(chnum);
						break;
					} else if ( !ch->MTC ) {
						// �J�E���g�G���[�i�������J�E���^�j
						DMAERR(chnum, 0x0D);
						break;
					}
					ch->CCR &= ~ccrCNT;
					X68DMA_Exec((X68DMA)dma, chnum);
				}
			} else {
				// ��Active����CNT�r�b�g�͓���^�C�~���O�G���[
				DMAERR(chnum, 0x02);
			}
		}
	} while ( 0 );
}


// --------------------------------------------------------------------------
//   ���J�֐�
// --------------------------------------------------------------------------
X68DMA X68DMA_Init(CPUDEV* cpu, MEM16HDL mem)
{
	INFO_DMA* dma = NULL;
	do {
		dma = (INFO_DMA*)_MALLOC(sizeof(INFO_DMA), "DMA struct");
		if ( !dma ) break;
		memset(dma, 0, sizeof(INFO_DMA));
		dma->cpu = cpu;
		dma->mem = mem;
		dma->is_ready[0] = &IsReadyDummy;
		dma->is_ready[1] = &IsReadyDummy;
		dma->is_ready[2] = &IsReadyDummy;
		dma->is_ready[3] = &IsReadyDummy;
		X68DMA_Reset((X68DMA)dma);
		LOG(("DMA : initialize OK"));
		return (X68DMA)dma;
	} while ( 0 );

	X68DMA_Cleanup((X68DMA)dma);
	return NULL;
}

void X68DMA_Cleanup(X68DMA hdl)
{
	INFO_DMA* dma = (INFO_DMA*)hdl;
	if ( dma ) {
		_MFREE(dma);
	}
}

void X68DMA_Reset(X68DMA hdl)
{
	INFO_DMA* dma = (INFO_DMA*)hdl;
	if ( dma ) {
		memset(dma->ch, 0, sizeof(dma->ch));
		dma->irq = 0;
		dma->last_irq = 0;
		IRQ_Clear(dma->cpu, 3);
	}
}

MEM16W_HANDLER(X68DMA_Write)
{
	INFO_DMA* dma = (INFO_DMA*)prm;
	UINT32 chnum = (adr>>6)&3;
	DMA_CH* ch = &dma->ch[chnum];
	adr &= 0x3F;
	data &= 0xFF;
	switch ( adr )
	{
		// CSR
		case 0x00:
			ch->CSR &= ( (~data) | 0x09 );
			break;
		// CER
		case 0x01:
			// R/O
			break;
		// CCR
		case 0x07:
			{
			UINT32 old = ch->CCR;
			ch->CCR = ( ch->CCR & ccrSTR ) | ( data & ~ccrSAB );  // CCR��STR�͏������݂ł͗��Ƃ��Ȃ�
			CheckStartDMA(dma, chnum, old, data);
			}
			break;
		// MTC
		case 0x0A: ch->MTC = ( ch->MTC & 0x000000FF ) | (data <<  8); break;
		case 0x0B: ch->MTC = ( ch->MTC & 0x0000FF00 ) | (data <<  0); break;
		// MAR
		case 0x0C: ch->MAR = ( ch->MAR & 0x00FFFFFF ) | (data << 24); break;
		case 0x0D: ch->MAR = ( ch->MAR & 0xFF00FFFF ) | (data << 16); break;
		case 0x0E: ch->MAR = ( ch->MAR & 0xFFFF00FF ) | (data <<  8); break;
		case 0x0F: ch->MAR = ( ch->MAR & 0xFFFFFF00 ) | (data <<  0); break;
		// DAR
		case 0x14: ch->DAR = ( ch->DAR & 0x00FFFFFF ) | (data << 24); break;
		case 0x15: ch->DAR = ( ch->DAR & 0xFF00FFFF ) | (data << 16); break;
		case 0x16: ch->DAR = ( ch->DAR & 0xFFFF00FF ) | (data <<  8); break;
		case 0x17: ch->DAR = ( ch->DAR & 0xFFFFFF00 ) | (data <<  0); break;
		// BTC
		case 0x1A: ch->BTC = ( ch->BTC & 0x000000FF ) | (data <<  8); break;
		case 0x1B: ch->BTC = ( ch->BTC & 0x0000FF00 ) | (data <<  0); break;
		// BAR
		case 0x1C: ch->BAR = ( ch->BAR & 0x00FFFFFF ) | (data << 24); break;
		case 0x1D: ch->BAR = ( ch->BAR & 0xFF00FFFF ) | (data << 16); break;
		case 0x1E: ch->BAR = ( ch->BAR & 0xFFFF00FF ) | (data <<  8); break;
		case 0x1F: ch->BAR = ( ch->BAR & 0xFFFFFF00 ) | (data <<  0); break;
		// GCR
		case 0x3F:
			if ( chnum==3 ) ch->GCR = data;
			break;
		// others
		default:
			ch->r[adr] = data;
			break;
	}
}

MEM16R_HANDLER(X68DMA_Read)
{
	INFO_DMA* dma = (INFO_DMA*)prm;
	UINT32 chnum = (adr>>6)&3;
	DMA_CH* ch = &dma->ch[chnum];
	UINT32 ret = 0;
	adr &= 0x3F;
	switch ( adr )
	{
		// MTC
		case 0x0A: ret = ( ch->MTC >>  8 ) & 0xFF; break;
		case 0x0B: ret = ( ch->MTC >>  0 ) & 0xFF; break;
		// MAR
		case 0x0C: ret = ( ch->MAR >> 24 ) & 0xFF; break;
		case 0x0D: ret = ( ch->MAR >> 16 ) & 0xFF; break;
		case 0x0E: ret = ( ch->MAR >>  8 ) & 0xFF; break;
		case 0x0F: ret = ( ch->MAR >>  0 ) & 0xFF; break;
		// DAR
		case 0x14: ret = ( ch->DAR >> 24 ) & 0xFF; break;
		case 0x15: ret = ( ch->DAR >> 16 ) & 0xFF; break;
		case 0x16: ret = ( ch->DAR >>  8 ) & 0xFF; break;
		case 0x17: ret = ( ch->DAR >>  0 ) & 0xFF; break;
		// BTC
		case 0x1A: ret = ( ch->BTC >>  8 ) & 0xFF; break;
		case 0x1B: ret = ( ch->BTC >>  0 ) & 0xFF; break;
		// BAR
		case 0x1C: ret = ( ch->BAR >> 24 ) & 0xFF; break;
		case 0x1D: ret = ( ch->BAR >> 16 ) & 0xFF; break;
		case 0x1E: ret = ( ch->BAR >>  8 ) & 0xFF; break;
		case 0x1F: ret = ( ch->BAR >>  0 ) & 0xFF; break;
		// others
		default:
			ret = ch->r[adr];
			break;
	}
	return ret;
}

BOOL X68DMA_Exec(X68DMA hdl, UINT32 chnum)
{
	INFO_DMA* dma = (INFO_DMA*)hdl;
	DMA_CH* ch = &dma->ch[chnum];
	BOOL ret = FALSE;

	// dma->is_ready / cbprm �� DMA_CH �Ɉړ�����������킾���A�X�e�[�g�̕ۑ����ʓ|�ɂȂ�
	#define IS_READY(chnum) dma->is_ready[chnum](dma->cbprm[chnum])

	// ���s�\����
	#define EXEC_CONDITION  (ch->CSR & csrACT)/*ACT*/ && (!(ch->CSR & csrCOC))/*!COC*/ && (!(ch->CCR & ccrHLT))/*HALT���ł͂Ȃ�*/ && ( ch->MTC )/*�]���J�E���g��0�ȏ�*/ && ( ((ch->OCR&3)!=2) || (IS_READY(chnum)) )/*�I�[�g���N�G�X�g�܂��͊O��READY*/

	if ( EXEC_CONDITION )
	{
		UINT32 *pS, *pD;
		UINT32 mode;
		SINT32 mdir, ddir;

		// �]������
		if ( ch->OCR & 0x80 ) {
			// Device->Memory
			pS = &ch->DAR;
			pD = &ch->MAR;
		} else {
			// Memory->Device
			pS = &ch->MAR;
			pD = &ch->DAR;
		}

		// �A�h���X�����i�s
		if ( ch->SCR & 0x04 ) mdir = 1; else if ( ch->SCR & 0x08 ) mdir = -1; else mdir = 0;
		if ( ch->SCR & 0x01 ) ddir = 1; else if ( ch->SCR & 0x02 ) ddir = -1; else ddir = 0;

		// �]�����[�h�ɂ��i�s�ʕ␳
		mode = ((ch->OCR>>4)&3) + ((ch->DCR>>1)&4);
		switch ( mode )
		{
			case 0: case 1: case 2: case 3:
				mdir *= 1; ddir *= 2;
				break;
			case 4:
				mdir *= 1; ddir *= 1;
				break;
			case 5:
				mdir *= 2; ddir *= 2;
				break;
			case 6:
				mdir *= 4; ddir *= 4;
				break;
		}

		// �o�X�G���[�t���O�N���A
		dma->bus_err = 0;

		// DMA�J�n
		dma->in_dma = TRUE;

		// �����ɗ������_�Ŏ��s�����͖������Ă���̂ŁA1��͕K�����s����
		do {
			// 1�]�����s
			switch ( mode ) {
				case 0:
				case 3:
					DMA_WR08( *pD, DMA_RD08(*pS) ); ch->MAR += mdir; ch->DAR += ddir;
					break;
				case 1:
					DMA_WR08( *pD, DMA_RD08(*pS) ); ch->MAR += mdir; ch->DAR += ddir;
					DMA_WR08( *pD, DMA_RD08(*pS) ); ch->MAR += mdir; ch->DAR += ddir;
					break;
				case 2:
					DMA_WR08( *pD, DMA_RD08(*pS) ); ch->MAR += mdir; ch->DAR += ddir;
					DMA_WR08( *pD, DMA_RD08(*pS) ); ch->MAR += mdir; ch->DAR += ddir;
					DMA_WR08( *pD, DMA_RD08(*pS) ); ch->MAR += mdir; ch->DAR += ddir;
					DMA_WR08( *pD, DMA_RD08(*pS) ); ch->MAR += mdir; ch->DAR += ddir;
					break;
				case 4:
					DMA_WR08( *pD, DMA_RD08(*pS) ); ch->MAR += mdir; ch->DAR += ddir;
					break;
				case 5:
					DMA_WR16( *pD, DMA_RD16(*pS) ); ch->MAR += mdir; ch->DAR += ddir;
					break;
				case 6:
					DMA_WR32( *pD, DMA_RD32(*pS) ); ch->MAR += mdir; ch->DAR += ddir;
					break;
			}

			// �G���[����
			if ( dma->bus_err ) {
				if ( dma->bus_err & 1 ) {
					// read
					DMAERR(chnum, ( ch->OCR & 0x80 ) ? 0x0A : 0x09);
				} else {
					// write
					DMAERR(chnum, ( ch->OCR & 0x80 ) ? 0x09 : 0x0A);
				}
				break;
			} else {
				// �G���[�o�ĂȂ��Ȃ�1�x�͓]�����������͂�
				ret = TRUE;
			}

			ch->MTC--;
			if ( !ch->MTC ) {
				// �w�蕪�̃o�C�g���]���I��
				if ( ch->OCR & 0x08 ) {
					// �`�F�C�����[�h�œ����Ă���ꍇ
					if ( ch->OCR & 0x04 ) {
						// �����N�A���C�`�F�C��
						if ( ch->BAR ) {
							ch->MAR = DMA_RD32(ch->BAR);
							ch->MTC = DMA_RD16(ch->BAR+4);
							ch->BAR = DMA_RD32(ch->BAR+6);
							if ( dma->bus_err ) {
								DMAERR(chnum, ( dma->bus_err & 1 ) ? 0x0B : 0x07);
								break;
							} else if ( !ch->MTC ) {
								DMAERR(chnum, 0x0D);
								break;
							}
						}
					} else {
						// �A���C�`�F�C��
						ch->BTC--;
						if ( ch->BTC ) {
							// ���̃u���b�N������
							ch->MAR = DMA_RD32(ch->BAR);
							ch->MTC = DMA_RD16(ch->BAR+4);
							ch->BAR += 6;
							if ( dma->bus_err ) {
								DMAERR(chnum, ( dma->bus_err & 1 ) ? 0x0B : 0x07);
								break;
							} else if ( !ch->MTC ) {
								DMAERR(chnum, 0x0D);
								break;
							}
						}
					}
				} else {
					// �ʏ탂�[�h�i1�u���b�N�̂݁j�I��
					if ( ch->CCR & ccrCNT ) {
						// Countinuous���쒆
						ch->CSR |= csrBTC;  // �u���b�N�]���I���r�b�g�^���荞��
						DMAINT(chnum);
						if ( ch->BAR ) {
							ch->MAR  = ch->BAR;
							ch->MTC  = ch->BTC;
							ch->CSR |= csrACT;  // ���ꂢ��H
							ch->BAR  = 0x00;
							ch->BTC  = 0x00;
							if ( !ch->MTC ) {
								DMAERR(chnum, 0x0D);
								break;
							}
							ch->CCR &= ~ccrCNT;
						}
					}
				}
				if ( !ch->MTC ) {
					ch->CSR |= csrCOC;  // COC
					ch->CSR &= ~csrACT; // !ACT
					DMAINT(chnum);
				}
			}

//			X68CPU_ConsumeClock(dma->cpu, 2);

			// �I�[�g���N�G�X�g�ő呬�x�ȊO�͒��f
			if ( (ch->OCR&3)!=1 ) break;

		} while ( EXEC_CONDITION );

		// DMA���t���O�𗎂Ƃ�
		dma->in_dma = FALSE;
	}

	return ret;
}

void X68DMA_BusErr(X68DMA hdl, UINT32 adr, BOOL is_read)
{
	INFO_DMA* dma = (INFO_DMA*)hdl;
	// DMA���s���̃o�X�G���[��DMA���Ńt�b�N����
	if ( dma->in_dma ) {
		dma->bus_err |= ( is_read ) ? 1 : 2;
		return;
	}
	// DMA���ŏE��Ȃ������ꍇ��CPU�֒ʒm
	X68CPU_BusError(dma->cpu, adr, is_read);
}

int X68DMA_GetIntVector(X68DMA hdl)
{
	INFO_DMA* dma = (INFO_DMA*)hdl;
	UINT32 chnum = dma->last_irq;
	int ret = -1;
	/*
		���荞�݃v���C�I���e�B��CP�i�`�����l���v���C�I���e�B�j�Ɠ���B
		CP��0�`3�ŒႢ�قǗD��A����ݒ肪����ꍇ�A���s�D��x���ƑO����s�̂��̂����ɉ񂳂��
		���E���h���r���ɂȂ邪�A���荞�݂��������ǂ����͕s���B

		����ҁ[�̓�����́A���̂ւ�����Ƃ����悤�Ƃ����`�ՁH
		�����A���E���h���r���ɂ���ƂȂ�ADPCM���x�ꂪ���ɕ�������悤�ȋC������A�̂ŁA�ЂƂ܂�
		������Ȃ܂܂Ƃ��Ƃ��B
	*/
#if 0
	UINT32 cp;

	for (cp=0; cp<=3 && ret<0; cp++) {
		do {
			DMA_CH* ch = &dma->ch[chnum];
			// CP����v����z��T��
			if ( ( ch->CPR & 3 ) == cp ) {
				UINT32 bit = 1 << chnum;
				if ( dma->irq & bit ) {
					ret = ( ch->CSR & 0x10 /* ERR */ ) ? ch->EIV : ch->NIV;
					dma->irq &= ~bit;
					break;
				}
			}
			chnum = ( chnum + 1 ) & 3;
		} while ( chnum != dma->last_irq );
	}
#else
	// CP�Q�Ƃ��Ă�ł��Ȃ��A���x���Ă������
	do {
		DMA_CH* ch = &dma->ch[chnum];
		UINT32 bit = 1 << chnum;
		if ( dma->irq & bit ) {
			ret = ( ch->CSR & 0x10 /* ERR */ ) ? ch->EIV : ch->NIV;
			dma->irq &= ~bit;
			break;
		}
		chnum = (chnum+1) & 3;
	} while ( chnum!=dma->last_irq );
#endif

	dma->last_irq = chnum;

	// ���ɂ�IRQ���オ���Ă�Όp���A�����łȂ����IRQ3�𗎂Ƃ�
	if ( dma->irq ) {
		IRQ_Request(dma->cpu, IRQLINE_LINE|3, 0);
	} else {
		IRQ_Clear(dma->cpu, 3);
	}

	return ret;
}

void X68DMA_SetReadyCb(X68DMA hdl, UINT32 chnum, X68DMA_READYCB cb, void* cbprm)
{
	INFO_DMA* dma = (INFO_DMA*)hdl;
	dma->is_ready[chnum] = ( cb ) ? cb : &IsReadyDummy;
	dma->cbprm[chnum] = cbprm;
}


void X68DMA_LoadState(X68DMA hdl, STATE* state, UINT32 id)
{
	INFO_DMA* dma = (INFO_DMA*)hdl;
	if ( dma && state ) {
		ReadState(state, id, MAKESTATEID('D','M','A','C'), dma->ch,        sizeof(dma->ch));
		ReadState(state, id, MAKESTATEID('I','R','Q','F'), &dma->irq,      sizeof(dma->irq));
		ReadState(state, id, MAKESTATEID('L','I','R','Q'), &dma->last_irq, sizeof(dma->last_irq));
	}
}

void X68DMA_SaveState(X68DMA hdl, STATE* state, UINT32 id)
{
	INFO_DMA* dma = (INFO_DMA*)hdl;
	if ( dma && state ) {
		WriteState(state, id, MAKESTATEID('D','M','A','C'), dma->ch,        sizeof(dma->ch));
		WriteState(state, id, MAKESTATEID('I','R','Q','F'), &dma->irq,      sizeof(dma->irq));
		WriteState(state, id, MAKESTATEID('L','I','R','Q'), &dma->last_irq, sizeof(dma->last_irq));
	}
}
