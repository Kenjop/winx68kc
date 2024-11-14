/* -----------------------------------------------------------------------------------
  "SHARP X68000" SCC
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	�}�E�X�ǂݍ��݂ɕK�v�ȋ@�\��������
*/

#include "x68000_scc.h"

typedef struct {
	UINT8      reg[16];
	UINT32     regidx;
	UINT8      buf[4];
	UINT32     bufcnt;
} ST_SCC_CH;

typedef struct {
	CPUDEV*    cpu;
	TIMERHDL   t;
	TIMER_ID   tm_irq;
	 X68SCC_MOUSEREADCB mouse_cb;
	 void*     mouse_cbprm;
	ST_SCC_CH  ch[2];
	BOOL       in_irq;
} INFO_SCC;


// --------------------------------------------------------------------------
//   �����֐�
// --------------------------------------------------------------------------
static void SCC_DummyMouseCb(void* prm, SINT8* px, SINT8* py, UINT8* pstat)
{
	*px = 0;
	*py = 0;
	*pstat = 0;
}

static TIMER_HANDLER(SCC_CheckInt)
{
	INFO_SCC* scc = (INFO_SCC*)prm;

	if ( !scc->in_irq )  // ���荞�݃N���A������܂Ŏ��̊��荞�݂͏グ�Ȃ��悤�ɂ��Ă���
	{
		ST_SCC_CH* ch = &scc->ch[1];
		BOOL irq = FALSE;

		if ( ( ch->bufcnt != 0 ) && ( ch->reg[9] & 0x08 ) ) {
			switch ( (ch->reg[1]>>3) & 3 )
			{
			case 1:  // �ŏ��̃L�����N�^�Ŋ��荞��
				if ( ch->bufcnt == 3 ) irq = TRUE;
				break;
			case 2:  // �S�ẴL�����N�^�Ŋ��荞��
				if ( ch->bufcnt != 0 ) irq = TRUE;
				break;
			default:
				break;
			}
		}

		if ( irq ) {
			scc->in_irq = TRUE;
			scc->ch[0].reg[3] |= 0x04;   // CH-B��M���荞�݃t���O�Z�b�g�iCH-A��RR3�j
			IRQ_Request(scc->cpu, IRQLINE_LINE|5, 0);
		}
	}
}

static void SCC_WriteReg(INFO_SCC* scc, UINT32 chidx, UINT32 data)
{
	ST_SCC_CH* ch = &scc->ch[chidx];
	UINT32 regidx = ch->regidx;
	UINT32 old = ch->reg[regidx];

	ch->reg[regidx] = data;
	ch->regidx = 0;

	switch ( regidx )
	{
	case 0:  // WR0 �S�̐���
		ch->regidx = data & 7;
		// �R�}���h����
		switch ( (data>>3) & 7 )
		{
		case 1:  // 001 ��ʃ��W�X�^�I��
			ch->regidx += 8;
			break;
		case 7:  // 111 �ŏ��IUS���Z�b�g
			scc->in_irq = FALSE;
			scc->ch[0].reg[3] &= ~0x04;   // CH-B��M���荞�݃t���O�N���A�iCH-A��RR3�j
			break;
		}
		break;

	case 1:  // WR1  ���荞�ݐݒ�
		break;

	case 2:  // WR2  �x�N�^�ݒ�
		scc->ch[0].reg[2] = (UINT8)data;  // CH-A���͐ݒ�l�Œ�iCH-B���͎��ۂɔ��s���ꂽ�x�N�^�j
		break;

	case 3:  // WR3  ��M����p�����[�^�ݒ�
		break;

	case 5:  // WR5  ���M����p�����[�^�ݒ�
		if ( ( ( old ^ data ) & data ) & 0x02 ) {  // /RTS H->L (enable)
			if ( ch->reg[3] & 0x01 ) {  // Rx Enable
				// �}�E�X�f�[�^���o�b�t�@�Ɏ�荞��
				if ( ch->bufcnt == 0 ) {  // ����ҁ[�ł̓o�b�t�@����̎�������荞��ł��̂œ��P
					scc->mouse_cb(scc->mouse_cbprm, (SINT8*)&ch->buf[1], (SINT8*)&ch->buf[0], &ch->buf[2]);
					ch->bufcnt = 3;
				}
			}
		}
		break;

	case 9:  // WR9  ���荞�ݐ���ESCC���Z�b�g
		break;

	default:
		break;
	}
//if ( (data>>3) & 7 ) LOG(("      SCC : CH#%d Write Reg[%d]=$%02X", chidx, regidx, data));
}

static UINT32 SCC_ReadReg(INFO_SCC* scc, UINT32 chidx)
{
	ST_SCC_CH* ch = &scc->ch[chidx];
	UINT32 regidx = ch->regidx;
	UINT32 ret = 0;//ch->reg[regidx];
	ch->regidx = 0;

	switch ( regidx )
	{
	case 0:  // RR0
		if ( ch->bufcnt ) ret |= 0x01;
		ret |= 0x04;  // ���M�o�b�t�@�͏�ɋ�Ƃ���
		break;

	default:
		break;
	}

//LOG(("      SCC : CH#%d Read Reg[%d], $%02X", chidx, regidx, ret));
	return ret;
}


// --------------------------------------------------------------------------
//   ���J�֐�
// --------------------------------------------------------------------------
X68SCC X68SCC_Init(CPUDEV* cpu, TIMERHDL t)
{
	INFO_SCC* scc = NULL;
	do {
		scc = (INFO_SCC*)_MALLOC(sizeof(INFO_SCC), "SCC struct");
		if ( !scc ) break;
		memset(scc, 0, sizeof(INFO_SCC));
		scc->cpu = cpu;
		scc->t = t;
		scc->mouse_cb = &SCC_DummyMouseCb;
		// 4800bps�Ȃ̂ŁA���荞�݂͍ő��600��/sec�ɂȂ�͂��i�X�^�[�g/�X�g�b�v�r�b�g��4800�Ɋ܂܂��Ȃ�400���傢�j
		scc->tm_irq = Timer_CreateItem(scc->t, TIMER_NORMAL, TIMERPERIOD_HZ(400), &SCC_CheckInt, (void*)scc, 0, MAKESTATEID('S','C','T','M'));
		X68SCC_Reset((X68SCC)scc);
		LOG(("SCC : initialize OK"));
		return (X68SCC)scc;
	} while ( 0 );

	X68SCC_Cleanup((X68SCC)scc);
	return NULL;
}

void X68SCC_Cleanup(X68SCC hdl)
{
	INFO_SCC* scc = (INFO_SCC*)hdl;
	if ( scc ) {
		_MFREE(scc);
	}
}

void X68SCC_Reset(X68SCC hdl)
{
	INFO_SCC* scc = (INFO_SCC*)hdl;
	if ( scc ) {
		memset(scc->ch, 0, sizeof(scc->ch));
		scc->in_irq = FALSE;
		IRQ_Clear(scc->cpu, 5);
	}
}

MEM16W_HANDLER(X68SCC_Write)
{
	INFO_SCC* scc = (INFO_SCC*)prm;

	switch ( adr & 7 )
	{
	case 1:  // CH-B command
		SCC_WriteReg(scc, 1, data);
		break;

	case 3:  // CH-B data
		break;

	case 5:  // CH-A command
		SCC_WriteReg(scc, 0, data);
		break;

	case 7:  // CH-A data
		break;

	default:
		break;
	}
}

MEM16R_HANDLER(X68SCC_Read)
{
	INFO_SCC* scc = (INFO_SCC*)prm;
	UINT32 ret = 0xFF;

	switch ( adr & 7 )
	{
	case 1:  // CH-B command
		ret = SCC_ReadReg(scc, 1);
		break;

	case 3:  // CH-B data
		if ( scc->ch[1].bufcnt>0 ) {
			scc->ch[1].bufcnt--;
			ret = scc->ch[1].buf[scc->ch[1].bufcnt];
		} else {
			ret = 0;
		}
		break;

	case 5:  // CH-A command
		ret = SCC_ReadReg(scc, 0);
		break;

	case 7:  // CH-A data
		ret = 0;
		break;

	default:
		break;
	}

	return ret;
}

int X68SCC_GetIntVector(X68SCC hdl)
{
	INFO_SCC* scc = (INFO_SCC*)hdl;
	ST_SCC_CH* ch = &scc->ch[1];
	int vector = -1;

	if ( scc->ch[0].reg[3] & 0x04 ) {
		// CH-B��M���荞�݃t���O�������Ă���
		if ( !( ch->reg[9] & 0x02 ) ) {
			// WR9 NV�iNo Vector�j�������Ă��Ȃ�
			vector = scc->ch[0].reg[2];  // CH-A���i�ݒ�l�j���Q��
			if ( ch->reg[9] & 0x01 ) {
				// WR9 VIS=1�i���荞�ݗv���Ńx�N�^���ω�����j
				if ( ch->reg[9] & 0x10 ) {
					// Status High�i���荞�ݗv����bit4-6���ω��j
					vector = ( vector & ~(7<<4) ) | ( 0x02 << 4 );
				} else {
					// Status Low �i���荞�ݗv����bit1-3���ω��j
					vector = ( vector & ~(7<<1) ) | ( 0x02 << 1 );
				}
			}
			ch->reg[2] = (UINT8)vector;  // CH-B��RR2�ɂ͎��ۂɔ��s���ꂽ�x�N�^������
		}
	}

	/*
		�h���L�����ȂǁA��M���荞�݃n���h�����������Ȃ��irte �I�����[�ȁj�ꍇ�AIRQ5
		�𗎂Ƃ��^�C�~���O���Ȃ��Ȃ薳����IRQ��������������`�ɂȂ��Ă��܂��B
		�Ȃ̂ŁA�����������Ȃ������荞�ݎ󂯕t����ꂽ���_�ŗ��Ƃ��Ă��܂����Ƃɂ���B
	*/
	IRQ_Clear(scc->cpu, 5);

	return vector;
}

void X68SCC_SetMouseCallback(X68SCC hdl, X68SCC_MOUSEREADCB cb, void* cbprm)
{
	INFO_SCC* scc = (INFO_SCC*)hdl;
	if ( scc ) {
		scc->mouse_cb = (cb) ? cb : &SCC_DummyMouseCb;
		scc->mouse_cbprm = cbprm;
	}
}

void X68SCC_LoadState(X68SCC hdl, STATE* state, UINT32 id)
{
	INFO_SCC* scc = (INFO_SCC*)hdl;
	if ( scc && state ) {
		ReadState(state, id, MAKESTATEID('S','C','C','C'), scc->ch, sizeof(scc->ch));
		ReadState(state, id, MAKESTATEID('I','I','R','Q'), &scc->in_irq, sizeof(scc->in_irq));
	}
}

void X68SCC_SaveState(X68SCC hdl, STATE* state, UINT32 id)
{
	INFO_SCC* scc = (INFO_SCC*)hdl;
	if ( scc && state ) {
		WriteState(state, id, MAKESTATEID('S','C','C','C'), scc->ch, sizeof(scc->ch));
		WriteState(state, id, MAKESTATEID('I','I','R','Q'), &scc->in_irq, sizeof(scc->in_irq));
	}
}
