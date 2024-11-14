/* -----------------------------------------------------------------------------------
  "SHARP X68000" FDC
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	TODO�F
	- 9scdrv ��W���I�v�V�����ŏ풓����ƃf�B�X�N�������Ȃ��Ȃ�i/M ���K�v�AReadID ��
	  ��郁�f�B�A���ʂɎ��s���Ă�j
	- ����ҁ[�x�[�X�Ȃ̂łƂɂ�������
*/

#include "x68000_fdc.h"

// --------------------------------------------------------------------------
//   FDC�ʐM�f�[�^�\����
// --------------------------------------------------------------------------
// �O�̂��� pack ��1�o�C�g�A���C�������g�ɂ��Ă����iWin���ȊO�ł��ʂ�H�j
#pragma pack(1)

typedef struct {
	UINT8 cmd;
	UINT8 us;
	UINT8 c;
	UINT8 h;
	UINT8 r;
	UINT8 n;
	UINT8 eot;
	UINT8 gsl;
	UINT8 dtl;
} FDCPRM0;

/*  Params for ReadID / Seek / SenseDevStat */
typedef struct {
	UINT8 cmd;
	UINT8 us;
	UINT8 n;
} FDCPRM1;

/*  Params for WriteID  */
typedef struct {
	UINT8 cmd;
	UINT8 us;
	UINT8 n;
	UINT8 sc;
	UINT8 gap;
	UINT8 d;
} FDCPRM2;

/*  Response for many commands  */
typedef struct {
	UINT8 st0;
	UINT8 st1;
	UINT8 st2;
	UINT8 c;
	UINT8 h;
	UINT8 r;
	UINT8 n;
} FDCRSP;

#pragma pack()


// --------------------------------------------------------------------------
//   FDC�\����
// --------------------------------------------------------------------------
typedef struct {
	X68IOC   ioc;
	X68FDD   fdd;

	UINT32   cmd;
	UINT32   cyl;
	UINT32   st0;
	UINT32   st1;
	UINT32   st2;

	UINT32   wrcnt;
	UINT32   rdcnt;
	UINT32   wrpos;
	UINT32   rdpos;
	UINT8    prm_buf[16];
	UINT8    rsp_buf[16];

	UINT32   bufcnt;
	UINT32   wexec;
	UINT8    data_buf[0x8000];
	UINT8    scan_buf[0x8000];

	UINT32   drv_ctrl;
	SINT32   drv_sel;

	BOOL     force_ready;
} INFO_FDC;


// --------------------------------------------------------------------------
//   �e�[�u��
// --------------------------------------------------------------------------
// �R�}���h���Ƃ̃p�����[�^��
static const UINT8 PRM_SIZE_TABLE[32] = {
	0, 0, 8, 2, 1, 8, 8, 1, 0, 8, 1, 0, 8, 5, 0, 2,
    0, 8, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0
};

// �R�}���h���Ƃ̉����f�[�^��
static const UINT8 RSP_SIZE_TABLE[32] = {
	0, 0, 7, 0, 1, 7, 7, 0, 2, 7, 7, 0, 7, 7, 0, 0,
	0, 7, 0, 0, 1, 0, 0, 0, 0, 7, 0, 0, 0, 7, 0, 0
};


// --------------------------------------------------------------------------
//   ��`��
// --------------------------------------------------------------------------
#define US(p)  ( p->us & 0x03 )
#define HD(p)  ( ( p->us >> 2 ) & 1 )
#define SK(p)  ( ( p->us >> 5 ) & 1 )
#define MF(p)  ( ( p->us >> 6 ) & 1 )
#define MT(p)  ( ( p->us >> 7 ) & 1 )

#define SECTOR_ID_PTR(p)  (X68_SECTOR_ID*)&p->c


// --------------------------------------------------------------------------
//   �����֐�
// --------------------------------------------------------------------------
static void FdcPhaseEnd(INFO_FDC* fdc)
{
	FDCRSP* rsp = (FDCRSP*)fdc->rsp_buf;
	X68FDD_ReadID(fdc->fdd, fdc->drv_sel, SECTOR_ID_PTR(rsp), TRUE);
	rsp->st0 = fdc->st0;
	rsp->st1 = fdc->st1;
	rsp->st2 = fdc->st2;
	fdc->bufcnt = 0;
	fdc->rdpos = 0;
	fdc->wexec = 0;
	X68IOC_SetIrq(fdc->ioc, IOCIRQ_FDC, TRUE);
}

static void FdcExecCmd(INFO_FDC* fdc)
{
	FDCPRM0* prm0 = (FDCPRM0*)fdc->prm_buf;
	FDCPRM1* prm1 = (FDCPRM1*)fdc->prm_buf;
	FDCPRM2* prm2 = (FDCPRM2*)fdc->prm_buf;
	FDCRSP* rsp   = (FDCRSP*)fdc->rsp_buf;

	switch ( fdc->cmd )
	{
	case 0x02:  // ReadDiagnostic
		fdc->st0 = prm1->us&7;
		if ( ( X68FDD_IsDriveReady(fdc->fdd, fdc->drv_sel) ) || ( fdc->force_ready ) ) {
			if ( !X68FDD_Seek(fdc->fdd, fdc->drv_sel, (fdc->cyl<<1)+HD(prm0)) ) {
				fdc->st0 |= 0x40;
				fdc->st1 |= 0x04;
			} else {
				UINT32 ret = X68FDD_Read(fdc->fdd, fdc->drv_sel, SECTOR_ID_PTR(prm0), fdc->data_buf, TRUE);
				fdc->bufcnt = ( prm0->n ) ? (128<<prm0->n) : prm0->dtl;
				if ( ret & X68_SECTOR_STAT_NO_DATA                                 ) fdc->st1 |= 0x04;  // No Data
				if ( ret & X68_SECTOR_STAT_DEL                                     ) fdc->st2 |= 0x40;  // ControlMark (DDAM���o)
				if ( ret & (X68_SECTOR_STAT_DT_CRC_ERR|X68_SECTOR_STAT_ID_CRC_ERR) ) fdc->st1 |= 0x20;  // Data Error
				if ( ret & X68_SECTOR_STAT_DT_CRC_ERR                              ) fdc->st2 |= 0x20;  // Data CRC Error
				if ( ret & X68_SECTOR_STAT_NO_ADDR_MARK                            ) fdc->st2 |= 0x01;  // No Addr Mark
			}
		} else {
			fdc->st0 |= 0x48;
		}
//if ( fdc->drv_sel==1 ) LOG(("ReadDiag : track=%d, st=$%02X/%02X/%02X", (fdc->cyl<<1)+HD(prm0), fdc->st0, fdc->st1, fdc->st2));
		if ( fdc->st0 & 0x40 ) FdcPhaseEnd(fdc);
		break;

	case 0x03:  // Specify
		/* Nothing to do */
		break;

	case 0x04:  // SenseDeviceStatus
		fdc->st0 = prm1->us & 0x07;
		rsp->st0 = fdc->st0;
		if ( fdc->cyl == 0 ) {
			rsp->st0 |= 0x10;
		}
		if ( ( X68FDD_IsDriveReady(fdc->fdd, fdc->drv_sel) ) || ( fdc->force_ready ) ) {
			rsp->st0 |= 0x20;
		}
		if ( X68FDD_IsReadOnly(fdc->fdd, fdc->drv_sel) ) {
			rsp->st0 |= 0x40;
		}
		break;

	case 0x05:  // WriteData
	case 0x09:  // WriteDeletedData
		fdc->st0 = prm1->us&7;
		if (  X68FDD_IsReadOnly(fdc->fdd, fdc->drv_sel) ) {
			fdc->st0 |= 0x40;
			fdc->st1 |= 0x02;
		} else if ( ( X68FDD_IsDriveReady(fdc->fdd, fdc->drv_sel) ) || ( fdc->force_ready ) ) {
			if ( !X68FDD_Seek(fdc->fdd, fdc->drv_sel, (fdc->cyl<<1)+HD(prm0)) ) {
				fdc->st0 |= 0x40;
				fdc->st1 |= 0x04;
			} else {
				fdc->bufcnt = ( prm0->n ) ? ( 128 << prm0->n ) : prm0->dtl;
				fdc->wexec = 1;
			}
		} else {
			fdc->st0 |= 0x48;
		}
		if ( fdc->st0 & 0x40 ) FdcPhaseEnd(fdc);
		break;

	case 0x06:  // ReadData
	case 0x0C:  // ReadDeletedData
	case 0x11:  // ScanEqual
	case 0x19:  // ScanLowOrEqual
	case 0x1D:  // ScanHighOrEqual
		fdc->st0 = prm1->us&7;
		if ( ( X68FDD_IsDriveReady(fdc->fdd, fdc->drv_sel) ) || ( fdc->force_ready ) ) {
			BOOL is_read = ( (fdc->cmd==0x06) || (fdc->cmd==0x0C) ) ? TRUE : FALSE;
			if ( !X68FDD_Seek(fdc->fdd, fdc->drv_sel, (fdc->cyl<<1)+HD(prm0)) ) {
				fdc->st0 |= 0x40;
				fdc->st1 |= 0x04;
			} else {
				UINT32 ret = X68FDD_Read(fdc->fdd, fdc->drv_sel, SECTOR_ID_PTR(prm0), (is_read)?fdc->data_buf:fdc->scan_buf, FALSE);
				if ( !ret ) {
					fdc->st0 |= 0x40;
					fdc->st1 |= 0x04;
				} else {
					fdc->bufcnt = ( prm0->n ) ? ( 128 << prm0->n ) : prm0->dtl;
					if ( !is_read ) fdc->wexec = 1;
					if ( ( (ret&X68_SECTOR_STAT_DEL) && (fdc->cmd!=0x0C) ) || ( (!(ret&X68_SECTOR_STAT_DEL)) && (fdc->cmd==0x0C) ) ) {
						fdc->st2 |= 0x40;  // ControlMark (DDAM���o)
					}
					if ( ret & (X68_SECTOR_STAT_NO_DATA|X68_SECTOR_STAT_DT_CRC_ERR|X68_SECTOR_STAT_ID_CRC_ERR|X68_SECTOR_STAT_NO_ADDR_MARK) ) {
						fdc->st0 |= 0x40;  // �G���[�I��
					}
					if ( ret & X68_SECTOR_STAT_NO_DATA                                 ) fdc->st1 |= 0x04;  // No Data
					if ( ret & (X68_SECTOR_STAT_DT_CRC_ERR|X68_SECTOR_STAT_ID_CRC_ERR) ) fdc->st1 |= 0x20;  // Data Error
					if ( ret & X68_SECTOR_STAT_DT_CRC_ERR                              ) fdc->st2 |= 0x20;  // Data CRC Error
					if ( ret & X68_SECTOR_STAT_NO_ADDR_MARK                            ) fdc->st2 |= 0x01;  // No Addr Mark
				}
			}
		} else {
			fdc->st0 |= 0x48;
		}
//if ( fdc->drv_sel==1 ) LOG(("ReadData($%02X) : track=%d, chrn=%d/%d/%d/%d, st=$%02X/%02X/%02X", fdc->cmd, (fdc->cyl<<1)+HD(prm0), (SECTOR_ID_PTR(prm0))->c, (SECTOR_ID_PTR(prm0))->r, (SECTOR_ID_PTR(prm0))->h, (SECTOR_ID_PTR(prm0))->n, fdc->st0, fdc->st1, fdc->st2));
		if ( fdc->st0 & 0x40 ) FdcPhaseEnd(fdc);
		break;

	case 0x07:  // Recalibrate
		fdc->st0 = prm1->us & 0x07;
		fdc->cyl = 0;
		if ( ( !X68FDD_IsDriveReady(fdc->fdd, fdc->drv_sel) ) && ( !fdc->force_ready ) ) {
			fdc->st0 |= 0x48;  // FD�Ȃ�
		} else if ( ( fdc->drv_sel>=0 ) && ( fdc->drv_sel<2 ) ) {
			fdc->st0 |= 0x20;  // FD����A�h���C�u����
		} else {
			fdc->st0 |= 0x50;  // �h���C�u�Ȃ�
		}
		X68IOC_SetIrq(fdc->ioc, IOCIRQ_FDC, TRUE);
		break;

	case 0x08:  // SenseIntStatus
		rsp->st0 = fdc->st0;
		rsp->st1 = fdc->cyl;
		fdc->st0 = 0x80;
		break;

	case 0x0A:  // ReadID
		fdc->st0 = prm1->us & 0x07;
		memset(SECTOR_ID_PTR(rsp), 0, sizeof(X68_SECTOR_ID));
		if ( ( X68FDD_IsDriveReady(fdc->fdd, fdc->drv_sel) ) || ( fdc->force_ready ) ) {
			if ( !X68FDD_Seek(fdc->fdd, fdc->drv_sel, (fdc->cyl<<1)+HD(prm0)) ) {
				// �G���[
				fdc->st0 |= 0x40;
				fdc->st1 |= 0x04;
			} else {
				// ����
				UINT32 ret = X68FDD_ReadID(fdc->fdd, fdc->drv_sel, SECTOR_ID_PTR(rsp), FALSE);
				if ( !ret ) {
					// �G���[
					fdc->st0 |= 0x40;
					fdc->st1 |= 0x04;
				} else {
					if ( ret & X68_SECTOR_STAT_DEL ) fdc->st2 |= 0x40;
				}
			}
		} else {
			fdc->st0 |= 0x48;
		}
		rsp->st0 = fdc->st0;
		rsp->st1 = fdc->st1;
		rsp->st2 = fdc->st2;
//if ( fdc->drv_sel==1 ) LOG(("ReadID : track=%d, st=$%02X/%02X/%02X", (fdc->cyl<<1)+HD(prm0), fdc->st0, fdc->st1, fdc->st2));
		X68IOC_SetIrq(fdc->ioc, IOCIRQ_FDC, TRUE);
		break;

	case 0x0D:  // WriteID
		fdc->st0 = prm1->us & 0x07;
		if ( X68FDD_IsReadOnly(fdc->fdd, fdc->drv_sel) ) {
			fdc->st0 |= 0x40;
			fdc->st1 |= 0x02;
		} else if ( ( X68FDD_IsDriveReady(fdc->fdd, fdc->drv_sel) ) || ( fdc->force_ready ) ) {
			fdc->bufcnt = prm2->sc << 2;
			fdc->wexec = 1;
		} else {
			fdc->st0 |= 0x48;
		}
		if ( fdc->st0 & 0x40 ) FdcPhaseEnd(fdc);
		break;

	case 0x0F:  // Seek
		fdc->st0 = prm1->us & 0x03;
		if ( X68FDD_IsDriveReady(fdc->fdd, fdc->drv_sel) ) {
			fdc->cyl = prm1->n;
			fdc->st0 |= 0x20;
		} else {
			fdc->st0 |= 0x48;
		}
//if ( fdc->drv_sel==1 ) LOG(("Seek : cyl=%d (track=%d), st=$%02X/%02X/%02X", fdc->cyl, fdc->cyl<<1, fdc->st0, fdc->st1, fdc->st2));
		X68IOC_SetIrq(fdc->ioc, IOCIRQ_FDC, TRUE);
		break;

	default:
		LOG(("FDC command error : cmd=$%02X", fdc->cmd));
		break;
	}
}

static void FdcNextTrack(INFO_FDC* fdc)
{
	FDCPRM0* prm0 = (FDCPRM0*)fdc->prm_buf;
	if ( prm0->r == prm0->eot ) {
		if ( ( MT(prm0) ) && ( !HD(prm0) ) && ( !prm0->h ) ) {
			prm0->us |= 4;
			prm0->h = 1;
			prm0->r = 1;
		} else {
			 FdcPhaseEnd(fdc);
			return;
		}
	} else {
		prm0->r++;
	}
	FdcExecCmd(fdc);
}

static void FdcWriteBuffer(INFO_FDC* fdc)
{
	FDCPRM0* prm0 = (FDCPRM0*)fdc->prm_buf;
	FDCPRM2* prm2 = (FDCPRM2*)fdc->prm_buf;
	UINT32 i;

	switch ( fdc->cmd )
	{
	case 0x05:  // WriteData
	case 0x09:  // WriteDeletedData
		{
			UINT32 ret = X68FDD_Write(fdc->fdd, fdc->drv_sel, SECTOR_ID_PTR(prm0), fdc->data_buf, (fdc->cmd==0x09)?TRUE:FALSE);
			if ( ret != X68_SECTOR_STAT_OK ) {
				fdc->st0 |= 0x40;
				fdc->st1 |= 0x04;
				X68IOC_SetIrq(fdc->ioc, IOCIRQ_FDC, TRUE);
			} else {
				FdcNextTrack(fdc);
			}
		}
		break;

	case 0x0D:  // WriteID
		fdc->data_buf[fdc->wrpos] = prm2->d;
		if ( !X68FDD_WriteID(fdc->fdd, fdc->drv_sel, (fdc->cyl<<1)+HD(prm2), fdc->data_buf, prm2->sc) ) {
			fdc->st0 |= 0x40;
			fdc->st1 |= 0x04;
		}
		FdcPhaseEnd(fdc);
		break;

	case 0x11:  // ScanEqual
		for (i=0; i<fdc->bufcnt; i++) {
			if ( ( fdc->data_buf[i] != 0xFF ) && ( fdc->scan_buf[i] != fdc->data_buf[i] ) ) {
				fdc->st0 |= 0x40;
				fdc->st2 &= 0xF7;
				fdc->st2 |= 0x04;
				FdcPhaseEnd(fdc);
				break;
			}
		}
		FdcNextTrack(fdc);
		break;

	case 0x19:  // ScanLowOrEqual
		for (i=0; i<fdc->bufcnt; i++) {
			if ( fdc->data_buf[i] != 0xFF ) {
				if ( fdc->scan_buf[i] > fdc->data_buf[i] ) {
					fdc->st0 |= 0x40;
					fdc->st2 &= 0xF7;
					fdc->st2 |= 0x04;
					FdcPhaseEnd(fdc);
					break;
				} else if ( fdc->scan_buf[i] < fdc->data_buf[i] ) {
					break;
				}
			}
		}
		FdcNextTrack(fdc);
		break;

	case 0x1D:  // ScanHighOrEqual
		for (i=0; i<fdc->bufcnt; i++) {
			if ( fdc->data_buf[i] != 0xFF ) {
				if ( fdc->scan_buf[i] < fdc->data_buf[i] ) {
					fdc->st0 |= 0x40;
					fdc->st2 &= 0xF7;
					fdc->st2 |= 0x04;
					FdcPhaseEnd(fdc);
					break;
				} else if ( fdc->scan_buf[i] > fdc->data_buf[i] ) {
					break;
				}
			}
		}
		FdcNextTrack(fdc);
		break;
	}
}



// --------------------------------------------------------------------------
//   ���J�֐�
// --------------------------------------------------------------------------
X68FDC X68FDC_Init(X68IOC ioc, X68FDD fdd)
{
	INFO_FDC* fdc = NULL;
	do {
		fdc = (INFO_FDC*)_MALLOC(sizeof(INFO_FDC), "FDC struct");
		if ( !fdc ) break;
		memset(fdc, 0, sizeof(INFO_FDC));
		fdc->ioc = ioc;
		fdc->fdd = fdd;
		X68FDC_Reset((X68FDC)fdc);
		LOG(("FDC : initialize OK"));
		return (X68FDC)fdc;
	} while ( 0 );

	X68FDC_Cleanup((X68FDC)fdc);
	return NULL;
}

void X68FDC_Cleanup(X68FDC hdl)
{
	INFO_FDC* fdc = (INFO_FDC*)hdl;
	if ( fdc ) {
		_MFREE(fdc);
	}
}

void X68FDC_Reset(X68FDC hdl)
{
	INFO_FDC* fdc = (INFO_FDC*)hdl;
	if ( fdc ) {
		fdc->cmd = 0;
		fdc->wrcnt = 0;
		fdc->rdcnt = 0;
		fdc->bufcnt = 0;
		fdc->wexec = 0;
		fdc->drv_ctrl = 0;
		fdc->drv_sel = -1;
		fdc->force_ready = FALSE;
	}
}

MEM16W_HANDLER(X68FDC_Write)
{
	INFO_FDC* fdc = (INFO_FDC*)prm;
//LOG(("FDC_W $%06X = %02X", adr, data));
	switch ( adr & 0x007 )
	{
	case 0x001:  // 0xE94001  FDC�R�}���h
		// ���Z�b�g�n�R�}���h�i0x34�`0x36�j�̂ݎ󂯕t����
		// C-PHASE �݂̂ň������Ȃ��̂ŃX���[
		break;
	case 0x003:  // 0xE94003  FDC�R�}���h
		if ( fdc->bufcnt ) {
			// �f�[�^�������ݒ�
			fdc->data_buf[fdc->wrpos++] = (UINT8)data;
			if ( fdc->wrpos >= fdc->bufcnt ) {
				FdcWriteBuffer(fdc);
				fdc->wrpos = 0;
			}
		} else {
			if ( fdc->wrcnt ) {
				// �p�����[�^��t��
				fdc->prm_buf[fdc->wrpos++] = (UINT8)data;
				fdc->wrcnt--;
			} else {
				// �R�}���h��t
				fdc->cmd = data & 0x1F;
				fdc->rdpos = 0;
				fdc->wrpos = 0;
				fdc->rdcnt = 0;
				fdc->wrcnt = PRM_SIZE_TABLE[fdc->cmd];
				fdc->prm_buf[fdc->wrpos++] = (UINT8)data;
			}
			// �p�����[�^��t���I�������A�R�}���h�������s��
			if ( fdc->wrcnt==0 ) {
				fdc->wrpos = 0;
				fdc->rdcnt = RSP_SIZE_TABLE[fdc->cmd];
				fdc->st1 = 0;
				fdc->st2 = 0;
				if ( (fdc->cmd==17/*ScanEqual*/) || (fdc->cmd==25/*ScanLowOrEqual*/) || (fdc->cmd==29/*ScanHighOrEqual*/) ) fdc->st2 |= 0x08;
				FdcExecCmd(fdc);
			}
		}
		break;
	case 0x005:  // 0xE94005  �h���C�u�I�v�V�����M��
		/*
			L-------  LED CTRL�i�f�B�X�N���Ȃ����ɃA�N�Z�X�����v�� 1:�_�� 0:�����j
			-M------  EJECT MASK�i1:�C�W�F�N�g���� 0:�L���j �����������ۂ̓C�W�F�N�g�{�^����LED��������
			--E-----  EJECT�i1:�C�W�F�N�g���� 0:���Ȃ��j
			----3210  1��0 �G�b�W�ŁA��L3�̋@�\��K�p�i0�`3:�h���C�u�ԍ��j
		*/
		{
			UINT32 drive_flag = ( fdc->drv_ctrl ^ data ) & fdc->drv_ctrl;
			UINT32 drive;
			fdc->drv_ctrl = data;
			for (drive=0; drive<=1; drive++) {
				if ( drive_flag & (1<<drive) ) {  // �h���C�u#n���Ώۂ��ǂ���
					// �C�W�F�N�g
					if ( data & 0x20 ) {
						X68FDD_EjectDisk(fdc->fdd, drive, TRUE);
					}
					// �C�W�F�N�g�}�X�N
					X68FDD_SetEjectMask(fdc->fdd, drive, ( data & 0x40 ) ? TRUE : FALSE );
					// LED�R���g���[��
					X68FDD_SetAccessBlink(fdc->fdd, drive, ( data & 0x80 ) ? TRUE : FALSE );
				}
			}
		}
		break;
	case 0x007:  // 0xE94007  �h���C�u�Z���N�g
		fdc->drv_sel = ( data & 0x80 ) ? (data&3) : -1;
		break;
	default:
		break;
	}
}

MEM16R_HANDLER(X68FDC_Read)
{
	INFO_FDC* fdc = (INFO_FDC*)prm;
	UINT32 ret = 0;
	switch ( adr & 0x007 )
	{
	case 0x001:  // 0xE94001  FDC�X�e�[�^�X
		ret = 0x80;
		// �ǂݍ��ݒ��Ȃ� DIO=1�iFDC->�z�X�g�j
		ret |= ( (fdc->rdcnt!=0) && (fdc->wexec==0) ) ? 0x40 : 0x00;
		// �������݃p�����[�^�E�ǂݍ��݃��X�|���X���c���Ă���ꍇ�� BUSY
		ret |= ( (fdc->wrcnt!=0) || (fdc->rdcnt!=0) ) ? 0x10 : 0x00;
		// SenseIntStatus ���� DIO �� BUSY �̓N���A����i����ҁ[�������^���R�s���j
		ret &= ( (fdc->rdcnt==1) && (fdc->cmd==8)   ) ? 0xAF : 0xFF;
		break;
	case 0x003:  // 0xE94003  FDC�f�[�^
		if ( fdc->bufcnt ) {
			ret = fdc->data_buf[fdc->rdpos++];
			if ( fdc->rdpos>=fdc->bufcnt ) {
				fdc->rdpos = 0;
				FdcNextTrack(fdc);
			}
		} else if ( fdc->rdcnt ) {
			ret = fdc->rsp_buf[fdc->rdpos++];
			fdc->rdcnt--;
		}
		break;
	case 0x005:  // 0xE94005  �h���C�u�X�e�[�^�X
		// fdc->drv_ctrl&0x01 ���h���C�u1�Ƀf�B�X�N�������Ă���A�������� fdc->drv_ctrl&0x02 ���h���C�u2�Ƀf�B�X�N�������Ă���Ȃ� 0x80 ��Ԃ�
		ret  = ( ( fdc->drv_ctrl&0x01 ) && ( X68FDD_IsDriveReady(fdc->fdd, 0) ) ) ? 0x80 : 0x00;
		ret |= ( ( fdc->drv_ctrl&0x02 ) && ( X68FDD_IsDriveReady(fdc->fdd, 1) ) ) ? 0x80 : 0x00;
		break;
	default:
		break;
	}
//LOG(("FDC_R $%06X ret=$%02X", adr, ret));
	return ret;
}

int X68FDC_IsDataReady(X68FDC hdl)
{
	INFO_FDC* fdc = (INFO_FDC*)hdl;
	return ( fdc->bufcnt ) ? 1 : 0;
}

void X68FDC_SetForceReady(X68FDC hdl, BOOL sw)
{
	INFO_FDC* fdc = (INFO_FDC*)hdl;
	if ( fdc ) {
		fdc->force_ready = sw;
	}
}


void X68FDC_LoadState(X68FDC hdl, STATE* state, UINT32 id)
{
	INFO_FDC* fdc = (INFO_FDC*)hdl;
	if ( fdc && state ) {
		X68IOC ioc = fdc->ioc;
		X68FDD fdd = fdc->fdd;
		ReadState(state, id, MAKESTATEID('S','T','R','C'), fdc, sizeof(INFO_FDC));
		fdc->ioc = ioc;
		fdc->fdd = fdd;
	}
}

void X68FDC_SaveState(X68FDC hdl, STATE* state, UINT32 id)
{
	INFO_FDC* fdc = (INFO_FDC*)hdl;
	if ( fdc && state ) {
		WriteState(state, id, MAKESTATEID('S','T','R','C'), fdc, sizeof(INFO_FDC));
	}
}
