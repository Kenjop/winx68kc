/* -----------------------------------------------------------------------------------
  "SHARP X68000" FDD
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "x68000_fdd.h"


// --------------------------------------------------------------------------
//   ��`��
// --------------------------------------------------------------------------
// FDD�A�N�Z�X�������Ă���iLED���ԂɂȂ��Ă���j�A�N�Z�X��Ԃ����������܂ŁiLED���΂ɖ߂�܂Łj�̎��ԁi�K���j
#define LED_ACCESS_DELAY       TIMERPERIOD_MS(100)

// �A�N�Z�X�����v�_�Ŏw�����̑��x�i�K���j
#define LED_BLINK_SPEED        TIMERPERIOD_MS(375)


typedef struct {
	BOOL             eject_mask[2];
	BOOL             access_blink[2];
	TUNIT            access_count[2];
	BOOL             blink_state;
	TUNIT            blink_count;
} INFO_LED_CTRL;

typedef struct {
	UINT8*           image;
	UINT32           size;
	UINT32           type;
	BOOL             protect;
	BOOL             modified;
	struct stX68DISK_FUNCS* funcs;
	void*            prm;
} INFO_DISK;

typedef struct {
	X68IOC           ioc;
	INFO_DISK        disk[2];
	INFO_X68FDD_LED  led;
	INFO_LED_CTRL    led_ctrl;

	DISKEJECTCB      eject_cb;
	void*            eject_cbprm;
} INFO_FDD;

typedef struct stX68DISK_FUNCS {
	BOOL      (*SetDisk)(INFO_DISK* disk);
	BOOL      (*EjectDisk)(INFO_DISK* disk);
	BOOL      (*Seek)(INFO_DISK* disk, UINT32 sector);
	UINT32    (*Read)(INFO_DISK* disk, const X68_SECTOR_ID* id, UINT8* buf, BOOL is_diag);
	BOOL      (*ReadID)(INFO_DISK* disk, X68_SECTOR_ID* retid, BOOL for_error);
	UINT32    (*Write)(INFO_DISK* disk, const X68_SECTOR_ID* id, UINT8* buf, BOOL del);
	BOOL      (*WriteID)(INFO_DISK* disk, UINT32 track, UINT8* buf, UINT32 num);
	BOOL      (*RebuildImage)(INFO_DISK* disk);
	UINT32    (*GetPrmSize)(void);
} X68DISK_FUNCS;


// --------------------------------------------------------------------------
//   �����֐�
// --------------------------------------------------------------------------
#include "x68000_disk_xdf.h"
#include "x68000_disk_dim.h"
//#include "x68000_disk_d88.h"

static X68DISK_FUNCS DISC_FUNC[] = {
	{  // XDF
		XDF_SetDisk,
		XDF_EjectDisk,
		XDF_Seek,
		XDF_Read,
		XDF_ReadID,
		XDF_Write,
		XDF_WriteID,
		XDF_RebuildImage,
		XDF_GetPrmSize,
	},
	{  // DIM  XXX ������
		DIM_SetDisk,
		DIM_EjectDisk,
		DIM_Seek,
		DIM_Read,
		DIM_ReadID,
		DIM_Write,
		DIM_WriteID,
		DIM_RebuildImage,
		DIM_GetPrmSize,
	},
	{  // D88  XXX ������
		XDF_SetDisk,
		XDF_EjectDisk,
		XDF_Seek,
		XDF_Read,
		XDF_ReadID,
		XDF_Write,
		XDF_WriteID,
		XDF_RebuildImage,
		XDF_GetPrmSize,
	},
};


#define ACCESS_LED_ON(drive)   fdd->led_ctrl.access_count[drive] = LED_ACCESS_DELAY
#define LED_CTRL_CLEAR(drive)  { INFO_LED_CTRL* ctrl = &fdd->led_ctrl; ctrl->eject_mask[drive] = FALSE; ctrl->access_blink[drive] = FALSE; ctrl->access_count[drive] = TUNIT_ZERO; }


static void CALLBACK DummyEjectCallback(void* prm, UINT32 drive, const UINT8* p_image, UINT32 image_sz)
{
}


// --------------------------------------------------------------------------
//   ���J�֐�
// --------------------------------------------------------------------------
X68FDD X68FDD_Init(X68IOC ioc)
{
	INFO_FDD* fdd = NULL;
	do {
		fdd = (INFO_FDD*)_MALLOC(sizeof(INFO_FDD), "FDD struct");
		if ( !fdd ) break;
		memset(fdd, 0, sizeof(INFO_FDD));
		fdd->ioc = ioc;
		fdd->eject_cb = &DummyEjectCallback;
		fdd->disk[0].type = X68K_DISK_MAX;
		fdd->disk[1].type = X68K_DISK_MAX;
		LED_CTRL_CLEAR(0);
		LED_CTRL_CLEAR(1);
		fdd->led_ctrl.blink_count = TUNIT_ZERO;
		LOG(("FDD : initialize OK"));
		return (X68FDD)fdd;
	} while ( 0 );

	X68FDD_Cleanup((X68FDD)fdd);
	return NULL;
}

void X68FDD_Cleanup(X68FDD hdl)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd ) {
		_MFREE(fdd);
	}
}

void X68FDD_SetEjectCallback(X68FDD hdl, DISKEJECTCB cb, void* cbprm)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd ) {
		fdd->eject_cb = (cb) ? cb : &DummyEjectCallback;
		fdd->eject_cbprm = cbprm;
	}
}

BOOL X68FDD_SetDisk(X68FDD hdl, SINT32 drive, const UINT8* img, UINT32 imgsz, X68K_DISK_TYPE type)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	BOOL ret = FALSE;
	if ( fdd && drive>=0 && drive<2 && img!=NULL && imgsz>0 && (UINT32)type<X68K_DISK_MAX )
	{
		INFO_DISK* disk = &fdd->disk[drive];
		do {
			BOOL ret;
			disk->image = _MALLOC(imgsz, "disk image");
			if ( !disk->image ) break;
			memcpy(disk->image, img, imgsz);
			disk->size = imgsz;
			disk->type = type;
			disk->funcs = &DISC_FUNC[disk->type];
			disk->protect = FALSE;
//			LED_CTRL_CLEAR(drive);
			// funcs->SetDisk() �Ăяo�����_�� disk �p�����[�^�͗p�ӂ���Ă���K�v������
			ret = disk->funcs->SetDisk(disk);
			if ( ret ) {
				// IOC���荞��
				// XXX �i����ւ����Ɂj�x�����K�v�H
				X68IOC_SetIrq(fdd->ioc, IOCIRQ_FDD, TRUE);
			} else {
				break;
			}
			return ret;
		} while ( 0 );

		if ( disk->image ) _MFREE(disk->image);
		disk->image = NULL;
		disk->size = 0;
		disk->type = X68K_DISK_MAX;
		disk->funcs = NULL;
		disk->protect = FALSE;
//		LED_CTRL_CLEAR(drive);
	}
	return FALSE;
}

BOOL X68FDD_EjectDisk(X68FDD hdl, SINT32 drive, BOOL force)
{
	// XXX �C�W�F�N�g�O�̃f�[�^�ۑ��ǂ�����H
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && drive>=0 && drive<2 ) {
		INFO_DISK* disk = &fdd->disk[drive];
		if ( !fdd->led_ctrl.eject_mask[drive] || force ) {  // �C�W�F�N�g�}�X�N����ĂȂ����A�\�t�g����̃C�W�F�N�g
			if ( disk->modified ) {
				// �ύX����Ă���΃C���[�W�č\�z���s��
				if ( disk->funcs ) {
					disk->funcs->RebuildImage(disk);
				}
				// �ύX����Ă���΃C�W�F�N�g�R�[���o�b�N���Ă�
				fdd->eject_cb(fdd->eject_cbprm, drive, disk->image, disk->size);
				disk->modified = FALSE;
			}
			if ( disk->funcs ) {
				disk->funcs->EjectDisk(disk);
			}
			if ( disk->image ) _MFREE(disk->image);
			disk->image = NULL;
			disk->size = 0;
			disk->type = X68K_DISK_MAX;
			disk->funcs = NULL;
			disk->protect = FALSE;
//			LED_CTRL_CLEAR(drive);

			// IOC���荞��
			X68IOC_SetIrq(fdd->ioc, IOCIRQ_FDD, TRUE);
			return TRUE;
		}
	}
	return FALSE;
}

BOOL X68FDD_Seek(X68FDD hdl, SINT32 drive, UINT32 track)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && drive>=0 && drive<2 ) {
		INFO_DISK* disk = &fdd->disk[drive];
		if ( disk->funcs ) {
			ACCESS_LED_ON(drive);
			return disk->funcs->Seek(disk, track);
		}
	}
	return FALSE;
}

UINT32 X68FDD_Read(X68FDD hdl, SINT32 drive, const X68_SECTOR_ID* id, UINT8* buf, BOOL is_diag)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && drive>=0 && drive<2 ) {
		INFO_DISK* disk = &fdd->disk[drive];
		if ( disk->funcs ) {
			ACCESS_LED_ON(drive);
			return disk->funcs->Read(disk, id, buf, is_diag);
		}
	}
	return FALSE;
}

BOOL X68FDD_ReadID(X68FDD hdl, SINT32 drive, X68_SECTOR_ID* retid, BOOL for_error)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && drive>=0 && drive<2 ) {
		INFO_DISK* disk = &fdd->disk[drive];
		if ( disk->funcs ) {
			ACCESS_LED_ON(drive);
			return disk->funcs->ReadID(disk, retid, for_error);
		}
	}
	return FALSE;
}

UINT32 X68FDD_Write(X68FDD hdl, SINT32 drive, const X68_SECTOR_ID* id, UINT8* buf, BOOL del)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && drive>=0 && drive<2 ) {
		INFO_DISK* disk = &fdd->disk[drive];
		if ( disk->funcs && !disk->protect ) {
			ACCESS_LED_ON(drive);
			disk->modified = TRUE;
			return disk->funcs->Write(disk, id, buf, del);
		}
	}
	return 0;
}

BOOL X68FDD_WriteID(X68FDD hdl, SINT32 drive, UINT32 track, UINT8* buf, UINT32 num)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && drive>=0 && drive<2 ) {
		INFO_DISK* disk = &fdd->disk[drive];
		if ( disk->funcs && !disk->protect ) {
			ACCESS_LED_ON(drive);
			disk->modified = TRUE;
			return disk->funcs->WriteID(disk, track, buf, num);
		}
	}
	return FALSE;
}


BOOL X68FDD_IsDriveReady(X68FDD hdl, SINT32 drive)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && drive>=0 && drive<2 ) {
		INFO_DISK* disk = &fdd->disk[drive];
		if ( disk->funcs ) {
			return TRUE;
		}
	}
	return FALSE;
}

BOOL X68FDD_IsReadOnly(X68FDD hdl, SINT32 drive)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && drive>=0 && drive<2 ) {
		INFO_DISK* disk = &fdd->disk[drive];
		return disk->protect;
	}
	return FALSE;
}

void X68FDD_SetWriteProtect(X68FDD hdl, SINT32 drive, BOOL sw)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && drive>=0 && drive<2 ) {
		INFO_DISK* disk = &fdd->disk[drive];
		if ( disk->image ) {  // �f�B�X�N�������Ă�Ƃ������ݒ�\
			disk->protect = sw;
		}
	}
}

void X68FDD_SetEjectMask(X68FDD hdl, SINT32 drive, BOOL sw)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && drive>=0 && drive<2 ) {
		INFO_DISK* disk = &fdd->disk[drive];
		if ( disk->image ) {  // �f�B�X�N�������Ă�Ƃ������ݒ�\
			fdd->led_ctrl.eject_mask[drive] = sw;
		}
	}
}

void X68FDD_SetAccessBlink(X68FDD hdl, SINT32 drive, BOOL sw)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && drive>=0 && drive<2 ) {
		INFO_DISK* disk = &fdd->disk[drive];
		if ( !disk->image ) {  // �f�B�X�N�������ĂȂ��Ƃ������ݒ�\
			fdd->led_ctrl.access_blink[drive] = sw;
		}
	}
}

const UINT8* X68FDD_GetDiskImage(X68FDD hdl, SINT32 drive, UINT32* p_size)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && drive>=0 && drive<2 ) {
		INFO_DISK* disk = &fdd->disk[drive];
		// XXX D88�Ȃǂł͂����Ńf�B�X�N�C���[�W�̍č\�z���K�v�H
		if ( p_size ) *p_size = disk->size;
		return disk->image;
	}
	return NULL;
}


// --------------------------------------------------------------------------
//   FDD �� LED �Ǘ�����
// --------------------------------------------------------------------------
static void UpdateDriveLED(INFO_FDD* fdd, UINT32 drive)
{
	INFO_LED_CTRL* ctrl = &fdd->led_ctrl;
	INFO_X68FDD_LED* led = &fdd->led;
	if ( !fdd->disk[drive].image ) {
		// �f�B�X�N����
		// �A�N�Z�X�����v
		if ( ctrl->access_blink[drive] ) {
			led->access[drive] = ( ctrl->blink_state ) ? X68FDD_LED_GREEN : X68FDD_LED_OFF;
		} else {
			led->access[drive] = X68FDD_LED_OFF;
		}
		// �C�W�F�N�g�{�^��
		led->eject[drive] = X68FDD_LED_OFF;
		led->inserted[drive] = FALSE;
	} else {
		// �f�B�X�N����
		// �A�N�Z�X�����v
		if ( ctrl->access_count[drive] > TUNIT_ZERO ) {
			led->access[drive] = X68FDD_LED_RED;
		} else {
			led->access[drive] = X68FDD_LED_GREEN;
		}
		// �C�W�F�N�g�{�^��
		if ( ctrl->eject_mask[drive] ) {
			led->eject[drive] = X68FDD_LED_OFF;
		} else {
			led->eject[drive] = X68FDD_LED_GREEN;
		}
		led->inserted[drive] = TRUE;
	}
}

void X68FDD_UpdateLED(X68FDD hdl, TUNIT t)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd ) {
		INFO_LED_CTRL* ctrl = &fdd->led_ctrl;
		// �_�ŏ�Ԃ̓���ւ�
		ctrl->blink_count += t;
		if ( ctrl->blink_count > LED_BLINK_SPEED ) {
			ctrl->blink_state = !ctrl->blink_state;
			ctrl->blink_count = TUNIT_ZERO;
		}
		// �A�N�Z�X��ԁi�ԓ_���j���΂ɖ߂�܂ł̒x������
		if ( ctrl->access_count[0] > TUNIT_ZERO ) {
			ctrl->access_count[0] -= t;
			if ( ctrl->access_count[0] < TUNIT_ZERO ) ctrl->access_count[0] = TUNIT_ZERO;
		}
		if ( ctrl->access_count[1] > TUNIT_ZERO ) {
			ctrl->access_count[1] -= t;
			if ( ctrl->access_count[1] < TUNIT_ZERO ) ctrl->access_count[1] = TUNIT_ZERO;
		}
		// �A�N�Z�X�����v�E�C�W�F�N�g�{�^��
		UpdateDriveLED(fdd, 0);
		UpdateDriveLED(fdd, 1);
	}
}

const INFO_X68FDD_LED* X68FDD_GetInfoLED(X68FDD hdl)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd ) {
		return &fdd->led;
	}
	return NULL;
}


// --------------------------------------------------------------------------
//   �X�e�[�g�֘A����
// --------------------------------------------------------------------------
// XXX �X�e�[�g�ۑ����A�e�f�B�X�N�̏��idisk->prm�j���ۑ����Ȃ��Ƃ����Ȃ�
static void LoadStateByDisk(INFO_FDD* fdd, STATE* state, UINT32 id, UINT32 drive)
{
	INFO_DISK* disk = &fdd->disk[drive];
	UINT32 old_size = disk->size;
	UINT32 old_type = disk->type;
	UINT32 new_size = 0;
	UINT32 new_type = 0;
	BOOL new_prot = 0;
	BOOL new_mod = 0;

	// Type �ƃC���[�W�T�C�Y��ǂݍ���
	// ���̃C�W�F�N�g�� disk ���g�p����̂ŁA�ꎞ�ϐ��ɓǂݍ���
	ReadState(state, id, MAKESTATEID('D','0'+drive,'T','P'), &new_type, sizeof(disk->type));
	ReadState(state, id, MAKESTATEID('D','0'+drive,'S','Z'), &new_size, sizeof(disk->size));
	ReadState(state, id, MAKESTATEID('D','0'+drive,'P','R'), &new_prot, sizeof(disk->protect));
	ReadState(state, id, MAKESTATEID('D','0'+drive,'M','D'), &new_mod,  sizeof(disk->modified));

	// �C���[�W�^�C�v���ω����Ă�Ȃ�A����̃f�B�X�N���C�W�F�N�g
	if ( old_type != new_type ) {
		if ( disk->funcs ) disk->funcs->EjectDisk(disk);
		disk->funcs = NULL;
	}

	// �^�C�v�ƃT�C�Y��ǂݍ���
	disk->type = new_type;
	disk->size = new_size;
	disk->protect = new_prot;
	disk->modified = new_mod;
	if ( disk->type < X68K_DISK_MAX ) disk->funcs = &DISC_FUNC[disk->type];

	// �C���[�W�T�C�Y���ς���Ă�Ȃ�A��U�������j��
	if ( old_size != disk->size ) {
		if ( disk->image ) _MFREE(disk->image);
		disk->image = NULL;
	}
	// �C���[�W�ǂݍ��݁i�C���[�W���������Ȃ��ꍇ�͊m�ۂ��j
	if ( disk->size ) {
		if ( !disk->image ) disk->image = _MALLOC(disk->size, "disk image");
		ReadState(state, id, MAKESTATEID('D','0'+drive,'I','M'), disk->image, disk->size);
	}

	// �C���[�W�^�C�v���ω����Ă�Ȃ�f�B�X�N�Z�b�g�A�y�ьʃp�����[�^�̓ǂݍ���
	if ( old_type != new_type ) {
		if ( disk->funcs ) disk->funcs->SetDisk(disk);
	}
	if ( disk->funcs ) {
		ReadState(state, id, MAKESTATEID('D','0'+drive,'P','R'), disk->prm, disk->funcs->GetPrmSize());
	}
}

static void SaveStateByDisk(INFO_FDD* fdd, STATE* state, UINT32 id, UINT32 drive)
{
	INFO_DISK* disk = &fdd->disk[drive];

	// Type �ƃC���[�W�T�C�Y��ۑ�
	WriteState(state, id, MAKESTATEID('D','0'+drive,'T','P'), &disk->type, sizeof(disk->type));
	WriteState(state, id, MAKESTATEID('D','0'+drive,'S','Z'), &disk->size, sizeof(disk->size));
	WriteState(state, id, MAKESTATEID('D','0'+drive,'P','R'), &disk->protect, sizeof(disk->size));
	WriteState(state, id, MAKESTATEID('D','0'+drive,'M','D'), &disk->modified, sizeof(disk->modified));
	// �C���[�W�ۑ�
	if ( disk->size && disk->image ) {
		WriteState(state, id, MAKESTATEID('D','0'+drive,'I','M'), disk->image, disk->size);
	}
	// �ʃp�����[�^�̕ۑ�
	if ( disk->funcs ) {
		WriteState(state, id, MAKESTATEID('D','0'+drive,'P','R'), disk->prm, disk->funcs->GetPrmSize());
	}
}

void X68FDD_LoadState(X68FDD hdl, STATE* state, UINT32 id)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && state ) {
		LoadStateByDisk(fdd, state, id, 0);
		LoadStateByDisk(fdd, state, id, 1);
		ReadState(state, id, MAKESTATEID('L','E','D','S'), &fdd->led, sizeof(fdd->led));
		ReadState(state, id, MAKESTATEID('L','E','D','C'), &fdd->led_ctrl, sizeof(fdd->led_ctrl));
	}
}

void X68FDD_SaveState(X68FDD hdl, STATE* state, UINT32 id)
{
	INFO_FDD* fdd = (INFO_FDD*)hdl;
	if ( fdd && state ) {
		SaveStateByDisk(fdd, state, id, 0);
		SaveStateByDisk(fdd, state, id, 1);
		WriteState(state, id, MAKESTATEID('L','E','D','S'), &fdd->led, sizeof(fdd->led));
		WriteState(state, id, MAKESTATEID('L','E','D','C'), &fdd->led_ctrl, sizeof(fdd->led_ctrl));
	}
}
