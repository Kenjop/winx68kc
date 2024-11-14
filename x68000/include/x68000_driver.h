/* -----------------------------------------------------------------------------------
  "SHARP X68000" System Driver
                                                      (c) 2005-11 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_driver_h_
#define _x68000_driver_h_

#include "emu_driver.h"

// --------------------------------------------------------------------------
//   �萔�ޗ�
// --------------------------------------------------------------------------
#define X68K_BASE_SCREEN_W  768
#define X68K_BASE_SCREEN_H  512


// --------------------------------------------------------------------------
//   �N�����I�v�V������
// --------------------------------------------------------------------------
#define X68K_BOOT_PARAMS_SHIFT_CLK   0

enum {
	X68K_CLK_10MHZ = 0,
	X68K_CLK_16MHZ,
	X68K_CLK_24MHZ,
	X68K_CLK_INDEX_MAX  // �C���f�b�N�X���擾�p�A�I��s��
};


// --------------------------------------------------------------------------
//   �f�B�X�N�C���[�W�^�C�v
// --------------------------------------------------------------------------
// XXX ���� XDF �� DIM�i�s���S�ȉ\������j�����Ή����Ă��Ȃ�
typedef enum {
	X68K_DISK_XDF = 0,
	X68K_DISK_DIM,
	X68K_DISK_D88,
	X68K_DISK_MAX
} X68K_DISK_TYPE;

typedef void (CALLBACK *DISKEJECTCB)(void* prm, UINT32 drive, const UINT8* p_image, UINT32 image_sz);


// --------------------------------------------------------------------------
//   FDD �� LED ���
// --------------------------------------------------------------------------
typedef enum {
	X68FDD_LED_OFF = 0,
	X68FDD_LED_GREEN,
	X68FDD_LED_RED
} X68FDD_LED_STATE;

typedef struct {
	X68FDD_LED_STATE  access[2];   // �A�N�Z�X�����v��ԁiDrive#0/#1�j
	X68FDD_LED_STATE  eject[2];    // �C�W�F�N�g�{�^���̃����v��ԁiDrive#0/#1�j
	BOOL              inserted[2]; // �f�B�X�N�������Ă邩�ǂ���
} INFO_X68FDD_LED;


// --------------------------------------------------------------------------
//   �L�[�R�[�h
// --------------------------------------------------------------------------
enum {
	// $00
	X68K_KEY_NONE = 0x00,
	X68K_KEY_ESC,
	X68K_KEY_1,
	X68K_KEY_2,
	X68K_KEY_3,
	X68K_KEY_4,
	X68K_KEY_5,
	X68K_KEY_6,
	X68K_KEY_7,
	X68K_KEY_8,
	X68K_KEY_9,
	X68K_KEY_0,
	X68K_KEY_MINUS,     // �t���L�[�� -
	X68K_KEY_EXP,       // ^
	X68K_KEY_YEN,       // �~�L���E�o�b�N�X���b�V��
	X68K_KEY_BS,        // BackSpace
	// $10
	X68K_KEY_TAB,
	X68K_KEY_Q,
	X68K_KEY_W,
	X68K_KEY_E,
	X68K_KEY_R,
	X68K_KEY_T,
	X68K_KEY_Y,
	X68K_KEY_U,
	X68K_KEY_I,
	X68K_KEY_O,
	X68K_KEY_P,
	X68K_KEY_AT,        // @
	X68K_KEY_OPENSB,    // [
	X68K_KEY_ENTER,     // �t���L�[�� Enter
	X68K_KEY_A,
	X68K_KEY_S,
	// $20
	X68K_KEY_D,
	X68K_KEY_F,
	X68K_KEY_G,
	X68K_KEY_H,
	X68K_KEY_J,
	X68K_KEY_K,
	X68K_KEY_L,
	X68K_KEY_SEMICOLON, // ;
	X68K_KEY_COLON,     // :
	X68K_KEY_CLOSESB,   // ]
	X68K_KEY_Z,
	X68K_KEY_X,
	X68K_KEY_C,
	X68K_KEY_V,
	X68K_KEY_B,
	X68K_KEY_N,
	// $30
	X68K_KEY_M,
	X68K_KEY_COMMA,     // ,
	X68K_KEY_PERIOD,    // .
	X68K_KEY_SLASH,     // /
	X68K_KEY_BACKSLASH, // �o�b�N�X���b�V���E�A���_�[�o�[�i�L�[�{�[�h�E���j
	X68K_KEY_SPACE,     // �X�y�[�X�o�[
	X68K_KEY_HOME,
	X68K_KEY_DEL,
	X68K_KEY_ROLLUP,
	X68K_KEY_ROLLDOWN,
	X68K_KEY_UNDO,
	X68K_KEY_LEFT,      // �J�[�\���L�[ ��
	X68K_KEY_UP,        // �J�[�\���L�[ ��
	X68K_KEY_RIGHT,     // �J�[�\���L�[ ��
	X68K_KEY_DOWN,      // �J�[�\���L�[ ��
	X68K_KEY_CLR,
	// $40
	X68K_KEY_NUMDIV,    // �e���L�[ /
	X68K_KEY_NUMMUL,    // �e���L�[ *
	X68K_KEY_NUMMINUS,  // �e���L�[ -
	X68K_KEY_NUM7,      // �e���L�[ 7
	X68K_KEY_NUM8,      // �e���L�[ 8
	X68K_KEY_NUM9,      // �e���L�[ 9
	X68K_KEY_NUMPLUS,   // �e���L�[ +
	X68K_KEY_NUM4,      // �e���L�[ 4
	X68K_KEY_NUM5,      // �e���L�[ 5
	X68K_KEY_NUM6,      // �e���L�[ 6
	X68K_KEY_NUMEQUAL,  // �e���L�[ =
	X68K_KEY_NUM1,      // �e���L�[ 1
	X68K_KEY_NUM2,      // �e���L�[ 2
	X68K_KEY_NUM3,      // �e���L�[ 3
	X68K_KEY_NUMENTER,  // �e���L�[ Enter
	X68K_KEY_NUM0,      // �e���L�[ 0
	// $50
	X68K_KEY_NUMCOMMA,  // �e���L�[ ,
	X68K_KEY_NUMPERIOD, // �e���L�[ .
	X68K_KEY_KIGOU,     // �u�L�����́v
	X68K_KEY_TOUROKU,   // �u�o�^�v
	X68K_KEY_HELP,
	X68K_KEY_XF1,
	X68K_KEY_XF2,
	X68K_KEY_XF3,
	X68K_KEY_XF4,
	X68K_KEY_XF5,
	X68K_KEY_KANA,      // �u���ȁv
	X68K_KEY_ROMAJI,    // �u���[�}���v
	X68K_KEY_CODEIN,    // �u�R�[�h���́v Inside X68000 �́u�J�i���́v�L�q�̓~�X
	X68K_KEY_CAPS,
	X68K_KEY_INS,
	X68K_KEY_HIRAGANA,  // �u�Ђ炪�ȁv
	// $60
	X68K_KEY_ZENKAKU,   // �u�S�p�v
	X68K_KEY_BREAK,
	X68K_KEY_COPY,
	X68K_KEY_F1,
	X68K_KEY_F2,
	X68K_KEY_F3,
	X68K_KEY_F4,
	X68K_KEY_F5,
	X68K_KEY_F6,
	X68K_KEY_F7,
	X68K_KEY_F8,
	X68K_KEY_F9,
	X68K_KEY_F10,
	X68K_KEY_UNUSED_6D, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	X68K_KEY_UNUSED_6E, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	X68K_KEY_UNUSED_6F, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	// $70
	X68K_KEY_SHIFT,     // �ESHIFT�������i��SHIFT�Ƌ�ʂȂ��j
	X68K_KEY_CTRL,
	X68K_KEY_OPT1,
	X68K_KEY_OPT2,
	X68K_KEY_UNUSED_74, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	X68K_KEY_UNUSED_75, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	X68K_KEY_UNUSED_76, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	X68K_KEY_UNUSED_77, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	X68K_KEY_UNUSED_78, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	X68K_KEY_UNUSED_79, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	X68K_KEY_UNUSED_7A, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	X68K_KEY_UNUSED_7B, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	X68K_KEY_UNUSED_7C, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	X68K_KEY_UNUSED_7D, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	X68K_KEY_UNUSED_7E, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�
	X68K_KEY_UNUSED_7F, // ���̃R�[�h��Ԃ��L�[�͂Ȃ�

	X68K_KEY_MAX_CODE
};

// �L�[���������̂��������̂��̃t���O�i�L�[�R�[�h�� OR �œn���j
#define X68K_KEYFLAG_PRESS    0x00
#define X68K_KEYFLAG_RELEASE  0x80


// --------------------------------------------------------------------------
//   �W���C�X�e�B�b�N���
// --------------------------------------------------------------------------
enum {
	X68K_JOY_UP      = 0x01,
	X68K_JOY_DOWN    = 0x02,
	X68K_JOY_LEFT    = 0x04,
	X68K_JOY_RIGHT   = 0x08,
	X68K_JOY_EX1     = 0x10,
	X68K_JOY_BUTTON1 = 0x20,
	X68K_JOY_BUTTON2 = 0x40,
	X68K_JOY_EX2     = 0x80
};


// --------------------------------------------------------------------------
//   �}�E�X�{�^�����
// --------------------------------------------------------------------------
enum {
	X68K_MOUSE_BTN_L = 0x01,
	X68K_MOUSE_BTN_R = 0x02
};


// --------------------------------------------------------------------------
//   �`��͈͏��
// --------------------------------------------------------------------------
typedef struct {
	SINT32 x1, y1;
	SINT32 x2, y2;
} ST_RECT;

typedef struct {
	ST_RECT  scrn;  // ������ۂ����߂�CRT�̕\���\�̈�
	ST_RECT  disp;  // ���݂�CRTC�ݒ�ł̎��ۂ̕\���̈�
} ST_DISPAREA;


// --------------------------------------------------------------------------
//   �`��͈͏��
// --------------------------------------------------------------------------
typedef enum {
	X68K_SOUND_OPM = 0,
	X68K_SOUND_ADPCM,

	X68K_SOUND_MAX
} X68K_SOUND_DEVICE;


// --------------------------------------------------------------------------
//   MIDI�֘A
// --------------------------------------------------------------------------
typedef enum {
	MIDIFUNC_RESET = 0,
	MIDIFUNC_DATAOUT,
} MIDI_FUNCTIONS;

typedef void (CALLBACK *MIDIFUNCCB)(void* prm, MIDI_FUNCTIONS func, UINT8 data);


// --------------------------------------------------------------------------
//   MIDI�֘A
// --------------------------------------------------------------------------
typedef enum {
	SASIFUNC_IS_READY = 0,
	SASIFUNC_READ,
	SASIFUNC_WRITE,
} SASI_FUNCTIONS;

typedef BOOL (CALLBACK *SASIFUNCCB)(void* prm, SASI_FUNCTIONS func, UINT32 devid, UINT32 pos, UINT8* data, UINT32 size);


// --------------------------------------------------------------------------
//   �G�~��I/F��`
// --------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

// ��{I/F
EMUDRIVER* X68kDriver_Initialize(const UINT8* rom_ipl, const UINT8* rom_font, UINT32 sndfreq);
void X68kDriver_Cleanup(EMUDRIVER* __drv);
UINT32 X68kDriver_Exec(EMUDRIVER* __drv, TUNIT period);
void X68kDriver_LoadState(EMUDRIVER* __drv, STATE* state);
void X68kDriver_SaveState(EMUDRIVER* __drv, STATE* state);

// �{�̃��Z�b�g
void X68kDriver_Reset(EMUDRIVER* __drv);

// �N���b�N�؂�ւ�
void X68kDriver_SetCpuClock(EMUDRIVER* __drv, UINT32 clk_idx);

// RAM�T�C�Y�ύX
void X68kDriver_SetMemorySize(EMUDRIVER* __drv, UINT32 sz_mb);

// SRAM�擾
UINT8* X68kDriver_GetSramPtr(EMUDRIVER* __drv, UINT32* p_sz);

// �������݂̋N�������t���b�s�[�f�B�X�N���C�W�F�N�g�����یĂ΂��R�[���o�b�N��o�^
void X68kDriver_SetEjectCallback(EMUDRIVER* prm, DISKEJECTCB cb, void* cbprm);

// �t���b�s�[�f�B�X�N�̃h���C�u�ւ̑}��
void X68kDriver_SetDisk(EMUDRIVER* prm, UINT32 drive, const UINT8* image, UINT32 image_sz, X68K_DISK_TYPE type, BOOL wr_protect);

// �t���b�s�[�f�B�X�N�̃h���C�u����̎��o���iforce=TRUE �̎��̓C�W�F�N�g�֎~�ݒ�𖳎����ċ����r�o�j
void X68kDriver_EjectDisk(EMUDRIVER* prm, UINT32 drive, BOOL force);

// �t���b�s�[�f�B�X�N�̃C���[�W�f�[�^���擾�i�f�[�^���������܂ꂽ�f�B�X�N��ۑ��������ꍇ�ȂǗp�j
// XXX ���s�ł̓C�W�F�N�g���i�A�v���I�����܂ށj�ɃR�[���o�b�N�����ł���̂ŁA����I�Ɏ��ɍs���K�v�͂Ȃ��͂�
const UINT8* X68kDriver_GetDiskImage(EMUDRIVER* prm, UINT32 drive, UINT32* p_imagesz);

// �t���b�s�[�h���C�u��LED���\���̂ւ̃|�C���^�𓾂�i��ʑw��FDD�A�N�Z�X�\���Ȃǂ������������ꍇ�Ɏg���j
const INFO_X68FDD_LED* X68kDriver_GetDriveLED(EMUDRIVER* prm);

// �t���b�s�[�f�B�X�N�A�N�Z�X�̍������ݒ�
void X68kDriver_SetFastFddAccess(EMUDRIVER* prm, BOOL fast);

// HDD�h���C�u��LED���𓾂�
X68FDD_LED_STATE X68kDriver_GetHddLED(EMUDRIVER* prm);

// �L�[���́i���j
void X68kDriver_KeyInput(EMUDRIVER* __drv, UINT32 key);
void X68kDriver_KeyClear(EMUDRIVER* __drv);

// �W���C�p�b�h����
void X68kDriver_JoyInput(EMUDRIVER* __drv, UINT32 joy1, UINT32 joy2);

// �}�E�X����
void X68kDriver_MouseInput(EMUDRIVER* __drv, SINT32 dx, SINT32 dy, UINT32 btn);

// CRTC�x�[�X�̕`����擾
BOOL X68kDriver_GetDrawInfo(EMUDRIVER* __drv, ST_DISPAREA* area);

// CRT���g�����擾
float X68kDriver_GetHSyncFreq(EMUDRIVER* __drv);
float X68kDriver_GetVSyncFreq(EMUDRIVER* __drv);

// �T�E���h�ݒ�
void X68kDriver_SetVolume(EMUDRIVER* __drv, X68K_SOUND_DEVICE device, float db);
void X68kDriver_SetFilter(EMUDRIVER* __drv, X68K_SOUND_DEVICE device, UINT32 filter_idx);

// MIDI���M�R�[���o�b�N�o�^
void X68kDriver_SetMidiCallback(EMUDRIVER* __drv, MIDIFUNCCB func, void* cbprm);

// SASI�t�@�C���A�N�Z�X�R�[���o�b�N�o�^
void X68kDriver_SetSasiCallback(EMUDRIVER* __drv, SASIFUNCCB func, void* cbprm);

#ifdef __cplusplus
}
#endif


#endif // of _x68000_driver_h_
