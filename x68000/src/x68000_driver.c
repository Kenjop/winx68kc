/* -----------------------------------------------------------------------------------
  "SHARP X68000" System Driver
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	����ҁ[�iWinX68k�j�x�[�X�ɐV�K�ɋN�������������́B
	��{�I�Ɍ��`�͂قڂƂǂ߂Ă��Ȃ��A�Ƃ���������ҁ[�̃R�[�h����������B�����ł��ǂ߂Ȃ��B

	TODO:
	�Z �D��x��
	- �\�[�X�p�h�L�������g
	- MUSASHI�ȊO��CPU�R�A������
	- �N�����E�B���h�E�\���ʒu�L��
	- SI.X �̃��U���g�������������@�ɋ߂��Ȃ�悤�E�F�C�g
	- windrv�̑g�ݍ��݂͂ǂ����悤�BWin���̃R�[�h��X68000�̃R�[�h�����R�Ƃ��Ă�̂�˂���c

	�Z �D��x��
	- DMA����͊��Ƃ���ҁ[���̂܂܁B�������̂ŏ�����������
	- �f�B�X�N����iFDC/FDD�j�̃R�[�h������ҁ[������������Ă����̂őS�̓I�ɉ����B������������
	- ATARI�d�l�ȊO�̃W���C�p�b�h�Ή�
	- VDP�f�o�b�O�p�̕\�����C�����Ƃ�ON/OFF�@�\
	- �f�o�b�O�p�̊e�탌�W�X�^���\���@�\

	�Z �D��x��
	- D88 �f�B�X�N�C���[�W�Ή��i�߂�ǂ��̂ł�肽���Ȃ��B�K�v�ȃ^�C�g�����������ꍇ�j
	- GVRAM �����N���A�̏������K���Ȃ̂𒼂��i�����߂�ǂ��̂ŁA�s��̏o��^�C�g����������΁j
	- FDD�̃A�N�Z�X�����v��ON/OFF��FDC������iSRT/HUT/HLT�l�����āj�ʒm�����������m�ɂȂ�H
	- FM�R�A�ύX�iymfm�j�� �������������������d���̂Ńy���f�B���O
	- Win32�����ȊO��C++���i�\���I�ɂ͂���ȂɎ�Ԃ͂�����Ȃ��C������A���A�^�C�}�Ǘ��ƃr�f�I��
	  ��A����FDD�ӂ��C++�ɍ��킹�đ啝�����������Ȃ��j
*/

#include "osconfig.h"
#include "mem_manager.h"

#include "x68000_driver.h"
#include "x68000_video.h"
#include "x68000_cpu.h"
#include "x68000_opm.h"
#include "x68000_adpcm.h"
#include "x68000_mfp.h"
#include "x68000_dma.h"
#include "x68000_ioc.h"
#include "x68000_fdc.h"
#include "x68000_fdd.h"
#include "x68000_rtc.h"
#include "x68000_scc.h"
#include "x68000_midi.h"
#include "x68000_sasi.h"

#include <math.h>


// --------------------------------------------------------------------------
//   Defines
// --------------------------------------------------------------------------
// �{�����[���o�����X�̓t�B���^�K�p��Ŏ���Ă�̂ŁA�t�B���^�ύX����ꍇ�͒���
// �G�g�v��SE�A�h���L����BGM�ӂ�Œ���
// OPM��0.9���炢����ɂ���ƁAHEAVY NOVA��OP�i�c�X�N���[���ŗv�ǂ��ۂ��̂��o��Ƃ��j�ŉ��������
#define VOLUME_OPM          0.80f  // 1.00f
#define VOLUME_ADPCM        1.45f  // 1.80f

// 68000�̓���N���b�N�i�N�����ݒ�\�j
#define X68000_CPU_CLK      (drv->cpu_clk)

// �ő僁�C��RAM�e�� 12Mb�i���T�C�Y�͋N�����ݒ�j
#define MAIN_RAM_SIZE_MAX   (12*1024*1024)
#define DEFAULT_RAM_SIZE    (2*1024*1024)


// --------------------------------------------------------------------------
//   enums
// --------------------------------------------------------------------------
typedef enum {
	H_EV_SYNCE = 0,  // ���������p���X�I���ʒu
	H_EV_START,      // �����\�����ԊJ�n�ʒu
	H_EV_END,        // �����\�����ԏI���ʒu
	H_EV_TOTAL,      // �����g�[�^��
	H_EV_MAX
} CRTC_H_EV;

typedef enum {
	V_EV_SYNCE = 0,  // ��L�̐���������
	V_EV_START,
	V_EV_END,
	V_EV_TOTAL,
	V_EV_MAX
} CRTC_V_EV;

typedef enum {
	FASTCLR_IDLE = 0,
	FASTCLR_WAIT,
	FASTCLR_EXEC
} FASTCTR_STATE;

typedef enum {
	DRAW_LINE_DOUBLE = 0,
	DRAW_LINE_NORMAL = 1,
	DRAW_LINE_HALF   = 2,
} DRAW_LINE_MODE;


// --------------------------------------------------------------------------
//   structs
// --------------------------------------------------------------------------
typedef struct {
	float          hsync_hz;
	float          vsync_hz;
//	float          h_step;   // �C�x���g�쓮�ɂȂ����̂Ō��݂͎g���ĂȂ�
//	float          h_count;  // ����
	UINT32         v_count;
	UINT32         h_ev_pos[H_EV_MAX];
	UINT32         v_ev_pos[V_EV_MAX];
	UINT32         next_v_ev;
	UINT32         vint_line;
	ST_RECT        scrn_area;
	DRAW_LINE_MODE line_shift;
	BOOL           update;
	BOOL           drawskip;
} INFO_TIMING;

typedef struct {
	UINT32         portc;
	UINT32         ctrl;
} INFO_PPI;

typedef struct {
	UINT8          reg;
	UINT32         fastclr_mask;
	UINT32         fastclr_state;
	BOOL           do_raster_copy;
} INFO_CRTC_OP;

typedef struct {
	SINT32         x;
	SINT32         y;
	UINT32         stat;
} INFO_MOUSE;

typedef struct {
	UINT32         key_state[4];
	UINT32         key_req[4];
} INFO_KEYBUF;

typedef struct {
	// �e�h���C�o���ʃf�[�^�iC++�ɂ�����x�[�X�N���X�����j
	EMUDRIVER      drv;
	// �f�o�C�X��
	MEM16HDL       mem;
	CPUDEV*        cpu;
	TIMER_ID       tm_hsync[H_EV_MAX]; // �����p
	TIMER_ID       tm_contrast;        // �R���g���X�g�ω��p
	TIMER_ID       tm_keybuf;          // �L�[�o�b�t�@��[�p
	SNDFILTER      hpf_adpcm;
	SNDFILTER      lpf_adpcm;
	SNDFILTER      lpf_opm;
	// X68k�f�o�C�X�Q
	X68000_VIDEO*  vid;
	X68OPMHDL      opm;
	X68ADPCM       adpcm;
	X68IOC         ioc;
	X68MFP         mfp;
	X68DMA         dma;
	X68FDC         fdc;
	X68FDD         fdd;
	X68RTC         rtc;
	X68SCC         scc;
	X68MIDI        midi;
	X68SASI        sasi;
	// ������
	UINT8          ram[MAIN_RAM_SIZE_MAX];  // �m�ۂ����͍ő�T�C�Y�ōs��
	UINT8          sram[0x4000];
	// �\���̌^���
	INFO_TIMING    t;
	INFO_PPI       ppi;
	INFO_CRTC_OP   crtc_op;
	INFO_MOUSE     mouse;
	INFO_KEYBUF    keybuf;
	// ���̑��̏��
	UINT8          sysport[8];
	UINT8          joyport[2];
	// �N�����I�v�V����
	UINT32         cpu_clk;
	UINT32         ram_size;
	BOOL           fast_fdd;
} X68000_DRIVER;

// ���s�v���b�g�t�H�[�����̃G���f�B�A���ɍĔz�u���ꂽROM�R�s�[
static UINT8 ipl_endian [0x20000];
static UINT8 font_endian[0xC0000];


// --------------------------------------------------------------------------
//   Internal
// --------------------------------------------------------------------------
#define SET_GPIP(a,b)  X68MFP_SetGPIP(drv->mfp,a,b)
#define CRTC(r)        (READBEWORD(&drv->vid->crtc[(r)*2]))

static void UpdateSpOffset(X68000_DRIVER* drv)
{
	const UINT32 bgsp_reso = drv->vid->bgram[0x810/2];  // bg_reso & 0x01 �� 512 �h�b�g���[�h
	const UINT32 crtc_reso = CRTC(20);

	// BG/SP�̃Y���v�Z�i�A�g�~�b�N���{�L�b�h�Ȃǂ��g�p�j
	drv->vid->sp_ofs_x = ( (SINT32)(drv->vid->bgram[0x80C/2]&0x03F) - (SINT32)(CRTC(2)+4) ) << 3;
	drv->vid->sp_ofs_y = (SINT32)(drv->vid->bgram[0x80E/2]&0x0FF) - (SINT32)CRTC(6);

	// ��{�I�ɃJ�v�R���n��CRTC��BGSP�Ԃ̐ݒ��1�h�b�g����������̂������i�t�@�C�i���t�@�C�g�A�喂�E���A�X�g���C�_�[�򗳂Ȃǁj
	// 1���X�^���̎��Ԃ��҂��邩��H
	drv->vid->sp_ofs_y &= ~1;
		
	// �Y����CRTC�̍��𑜓x/�W���𑜓x�x�[�X�ŕ␳�i�K�v�ȃ^�C�g������̂��s���j
	if ( ( crtc_reso ^ (bgsp_reso<<2) ) & 0x10 ) {  // CRTC�̉𑜓x�ݒ�̊�{���������ƁABGSP�̐����𑜓x�ݒ�̊ԂɈႢ������ꍇ
		if ( crtc_reso & 0x10 ) {
			// ���𑜓x����CRTC�l��512���C���x�[�X�Ȃ̂ŁABGSP=256���C���Ȃ甼���Ŏg�p
			drv->vid->sp_ofs_y >>= 1;
		} else {
			// �W���𑜓x����CRTC�l��256���C���x�[�X�Ȃ̂ŁABGSP=512���C���Ȃ�{�Ŏg�p
			drv->vid->sp_ofs_y <<= 1;
		}
	}
}

static void UpdateCrtTiming(X68000_DRIVER* drv)
{
	#define HF0_OSC_CLK  38864000  // �W���𑜓x OSC ���g��
	#define HF1_OSC_CLK  69552000  // ���𑜓x OSC ���g��

	// �ȉ��̃e�[�u�����̐��l�͑S�Ċ���؂�Đ����ɂȂ�͂�
	static const UINT32 DOT_CLOCK_TABLE[2][8] = {
		{  // HRL=0
			HF0_OSC_CLK/8, HF0_OSC_CLK/4, HF0_OSC_CLK/8, HF0_OSC_CLK/4,  // �W���𑜓x 256 / 512 /  ?  /  ?   XXX �H�������������������s��
			HF1_OSC_CLK/6, HF1_OSC_CLK/3, HF1_OSC_CLK/2, HF1_OSC_CLK/3   // ���𑜓x   256 / 512 / 768 / 512  ���@�Ŏ��������肱���H
		},
		{  // HRL=1
			HF0_OSC_CLK/8, HF0_OSC_CLK/4, HF0_OSC_CLK/8, HF0_OSC_CLK/4,  // �W���𑜓x 256 / 512 /  ?  /  ?
			HF1_OSC_CLK/8, HF1_OSC_CLK/4, HF1_OSC_CLK/2, HF1_OSC_CLK/4   // ���𑜓x   192 / 384 / 768 / 384  ���@�Ŏ��������肱���H
		}
	};
	const UINT32 r20 = CRTC(20);
	const UINT32 idxh = ( ( r20 & 0x10/*HF*/ ) >> 2 ) | ( r20 & 0x03/*HD*/ );
	const UINT32 idxv = ( r20 & 0x10/*HF*/ ) >> 4;
	const UINT32 hrl = ( drv->sysport[4] & 0x02 ) >> 1;
	const UINT32 old_h_st = drv->t.h_ev_pos[H_EV_START];
	const UINT32 old_h_ed = drv->t.h_ev_pos[H_EV_END];

	// �^�C�~���O�f�[�^�擾
	drv->t.h_ev_pos[H_EV_SYNCE] = (CRTC(1)+1) << 3;  // H-SYNC END
	drv->t.h_ev_pos[H_EV_START] = (CRTC(2)+5) << 3;  // H-START
	drv->t.h_ev_pos[H_EV_END  ] = (CRTC(3)+5) << 3;  // H-END
	drv->t.h_ev_pos[H_EV_TOTAL] = (CRTC(0)+1) << 3;  // H-TOTAL
	drv->t.v_ev_pos[V_EV_SYNCE] = (CRTC(5)+1);       // V-SYNC END
	drv->t.v_ev_pos[V_EV_START] = (CRTC(6)+1);       // V-START
	drv->t.v_ev_pos[V_EV_END  ] = (CRTC(7)+1);       // V-END
	drv->t.v_ev_pos[V_EV_TOTAL] = (CRTC(4)+1);       // V-TOTAL

	// HEAVY NOVA�ȂǁAV-END��V-TOTAL�����傫���^�C�g���p�̏����iQuickHack�I�ŃA���j
	if ( drv->t.h_ev_pos[H_EV_END] > drv->t.h_ev_pos[H_EV_TOTAL] ) drv->t.h_ev_pos[H_EV_END] = drv->t.h_ev_pos[H_EV_TOTAL];
	if ( drv->t.v_ev_pos[V_EV_END] > drv->t.v_ev_pos[V_EV_TOTAL] ) drv->t.v_ev_pos[V_EV_END] = drv->t.v_ev_pos[V_EV_TOTAL];

	// HDISP/VDISP �̃T�C�Y�i���\����ʃT�C�Y�j���擾
	drv->vid->h_sz = drv->t.h_ev_pos[H_EV_END] - drv->t.h_ev_pos[H_EV_START];
	drv->vid->v_sz = drv->t.v_ev_pos[V_EV_END] - drv->t.v_ev_pos[V_EV_START];

	// hstep �� M68000 �� 1clk �Ői�ރh�b�g�N���b�N��
	// �C�x���g�쓮���ɂȂ����̂Ŏg��Ȃ��Ȃ���
//	drv->t.h_step = (float)DOT_CLOCK_TABLE[hrl][idx] / (float)X68000_CPU_CLK;

	// VSYNC ���g���A�v�Z�͂��Ă邪�g���ĂȂ��i�C�x���g�����Ƒ��h�b�g���Ŏ����I�ɂ��̒l�ɂȂ�͂��j
	// ��ʑw�ŕ\���������ꍇ�̂��߂Ɍv�Z�͎c��
	drv->t.hsync_hz = (float)DOT_CLOCK_TABLE[hrl][idxh] / (float)(drv->t.h_ev_pos[H_EV_TOTAL]);
	drv->t.vsync_hz = (float)DOT_CLOCK_TABLE[hrl][idxh] / (float)(drv->t.h_ev_pos[H_EV_TOTAL]*drv->t.v_ev_pos[V_EV_TOTAL]);

	// 2�d�`��/�����`�撲���p
	switch ( r20 & 0x14 ) {
		case 0x04:  // �W���𑜓x�E����512�h�b�g
			drv->t.line_shift = DRAW_LINE_DOUBLE;
			break;
		case 0x10:  // ���𑜓x�E����256�h�b�g
			drv->t.line_shift = DRAW_LINE_HALF;
			break;
		case 0x00:  // �W���𑜓x�E����256�h�b�g
		case 0x14:  // ���𑜓x�E����512�h�b�g
		default:
			drv->t.line_shift = DRAW_LINE_NORMAL;
			break;
	}
	drv->vid->v_sz <<= 1;
	drv->vid->v_sz >>= drv->t.line_shift;

	// �h���A�[�K��560���C���ݒ�i�̔����X�^280���C�����[�h�j�ɐݒ肷��̂ŁA�ŏI�𑜓x�̒i�K�Ń`�F�b�N����
	// �i�łȂ��ƌ��s�̍ő�512���C���`�F�b�N�Ɉ����������ăt���T�C�Y�\������Ȃ��j
	if ( drv->vid->h_sz > X68_MAX_VSCR_WIDTH ) {
		LOG(("### UpdateCrtTiming : CRTC setting error, H-SIZE=%d is too large.", drv->vid->h_sz));
		drv->vid->h_sz = X68_MAX_VSCR_WIDTH;
	}
	if ( drv->vid->v_sz > X68_MAX_VSCR_HEIGHT ) {
		LOG(("### UpdateCrtTiming : CRTC setting error, V-SIZE=%d is too large.", drv->vid->v_sz));
		drv->vid->v_sz = X68_MAX_VSCR_HEIGHT;
	}

	// �`��̈�擾
	{
		typedef struct {
			UINT32  total;
			UINT32  pulse;
			UINT32  start;
			UINT32  end;
		} CRTC_DEFAULT_TIMING;

		// �抸�����킩���Ă镪�������߂Ƃ��^�H�t�����͌���s�� or ���e�X�g
		static const CRTC_DEFAULT_TIMING DEF_CRTC_H[2][8] = {
			{  // HRL=0
				{ 0x25, 0x01, 0x00, 0x20 }, { 0x4B, 0x03, 0x05, 0x45 }, { 0x84, 0x12, 0x1D, 0x7D }, { 0x4B, 0x03, 0x05, 0x45 }, // 256 / 512 / 768?/ 512?
				{ 0x2D, 0x04, 0x06, 0x26 }, { 0x5B, 0x09, 0x11, 0x51 }, { 0x89, 0x0E, 0x1C, 0x7C }, { 0x5B, 0x09, 0x11, 0x51 }  // 256 / 512 / 768 / 512
			},
			{  // HRL=1
				{ 0x25, 0x01, 0x00, 0x20 }, { 0x4B, 0x03, 0x05, 0x45 }, { 0x84, 0x12, 0x1D, 0x7D }, { 0x4B, 0x03, 0x05, 0x45 }, // 256?/ 512?/ 768?/ 512?
				{ 0x21, 0x08, 0x08, 0x20 }, { 0x44, 0x06, 0x0B, 0x3B }, { 0x89, 0x0E, 0x1C, 0x7C }, { 0x44, 0x06, 0x0B, 0x3B }  // 192?/ 384 / 768 / 384
			}
		};
		static const CRTC_DEFAULT_TIMING DEF_CRTC_V[2][2] = {
			{  // HRL=0
				{ 0x103, 0x02, 0x10, 0x100 }, { 0x237, 0x05, 0x28, 0x228 }  // Lo Reso / High Reso
			},
			{  // HRL=1
				{ 0x103, 0x02, 0x10, 0x100 }, { 0x237, 0x05, 0x28, 0x228 }  // Lo Reso / High Reso
			}
		};
		const CRTC_DEFAULT_TIMING* dt_h = &DEF_CRTC_H[hrl][idxh];
		const CRTC_DEFAULT_TIMING* dt_v = &DEF_CRTC_V[hrl][idxv];

		if ( (CRTC(0))==dt_h->total && (CRTC(1))==dt_h->pulse && (CRTC(4))==dt_v->total && (CRTC(5))==dt_v->pulse ) {
			// TOTAL�Ɠ����p���X�����W���ݒ�̏ꍇ�́A�f�t�H���g�̈�ɌŒ�
			drv->t.scrn_area.x1 = (dt_h->start+5) << 3;
			drv->t.scrn_area.x2 = (dt_h->end  +5) << 3;
			drv->t.scrn_area.y1 = (dt_v->start+1);
			drv->t.scrn_area.y2 = (dt_v->end  +1);
		} else {
			// ����ȊO�̐ݒ�̏ꍇ�A�������Ȍv�Z
			// ��{�I�ɂ́u�W���ݒ��START/END���A���݂�CRTC�ݒ�ɂ�����ǂ̕ӂ�ɗ��邩�v���A�����p���X���������������̔��
			// �v�Z���ċ��߂Ă���B�v�Z���̂̑Ó����͔���
			float h_base = ( (float)dt_h->total - (float)dt_h->pulse ) * 8;
			float h_this = (float)drv->t.h_ev_pos[H_EV_TOTAL] - (float)drv->t.h_ev_pos[H_EV_SYNCE];
			float h_rate = (float)h_this / (float)h_base;
			float v_base = ( (float)dt_v->total - (float)dt_v->pulse );
			float v_this = (float)drv->t.v_ev_pos[V_EV_TOTAL] - (float)drv->t.v_ev_pos[V_EV_SYNCE];
			float v_rate = (float)v_this / (float)v_base;
			float o_x1 = (((float)(dt_h->start+5)-(float)(dt_h->pulse+1))*8*h_rate) + (float)drv->t.h_ev_pos[H_EV_SYNCE];
			float o_x2 = (((float)(dt_h->end  +5)-(float)(dt_h->pulse+1))*8*h_rate) + (float)drv->t.h_ev_pos[H_EV_SYNCE];
			float o_y1 = (((float)(dt_v->start+1)-(float)(dt_v->pulse+1))  *v_rate) + (float)drv->t.v_ev_pos[V_EV_SYNCE];
			float o_y2 = (((float)(dt_v->end  +1)-(float)(dt_v->pulse+1))  *v_rate) + (float)drv->t.v_ev_pos[V_EV_SYNCE];
			float fixed_sz_x = o_x2 - o_x1;
			float fixed_sz_y = o_y2 - o_y1;
			float org_sz_x = (float)drv->t.h_ev_pos[H_EV_END] - (float)drv->t.h_ev_pos[H_EV_START];
			float org_sz_y = (float)drv->t.v_ev_pos[V_EV_END] - (float)drv->t.v_ev_pos[V_EV_START];
			// �␳��T�C�Y�����̕\���G���A��菬�����Ȃ�A�c������ێ������܂܌��T�C�Y������悤�ɍL����
			if ( fixed_sz_x < org_sz_x ) {
				fixed_sz_y = (fixed_sz_y*org_sz_x) / fixed_sz_x;
				fixed_sz_x = org_sz_x;
			}
			if ( fixed_sz_y < org_sz_y ) {
				fixed_sz_x = (fixed_sz_x*org_sz_y) / fixed_sz_y;
				fixed_sz_y = org_sz_y;
			}
			drv->t.scrn_area.x1 = (SINT32)drv->t.h_ev_pos[H_EV_START] - (SINT32)((fixed_sz_x-org_sz_x)/2);
			drv->t.scrn_area.x2 = drv->t.scrn_area.x1 + (SINT32)fixed_sz_x;
			drv->t.scrn_area.y1 = (SINT32)drv->t.v_ev_pos[V_EV_START] - (SINT32)((fixed_sz_y-org_sz_y)/2);
			drv->t.scrn_area.y2 = drv->t.scrn_area.y1 + (SINT32)fixed_sz_y;
		}
	}

	// �X�N���[���o�b�t�@�̃N���b�v�̈�ύX�iWin���`��Ȃǂ̏�ʑw�p�j
	do {
		UINT32 w = _MIN(drv->vid->h_sz, drv->vid->vscr_w);
		UINT32 h = _MIN(drv->vid->v_sz, drv->vid->vscr_h);
		DRIVER_SETSCREENCLIP(0, 0, w, h);
	} while ( 0 );

	// H�����̕`��G���A���L�������t���[���̕`��S�~����iAQUALES�Ȃǁj
	if ( ( old_h_st > drv->t.h_ev_pos[H_EV_START] ) || ( old_h_ed < drv->t.h_ev_pos[H_EV_END] ) ) {
		drv->t.drawskip = TRUE;
	}

	// HSYNC�^�C�}�X�V
	{
	TUNIT hsync = TIMERPERIOD_HZ(drv->t.hsync_hz);
	double total = (double)drv->t.h_ev_pos[H_EV_TOTAL];
//	Timer_ChangeStartAndPeriod(_MAINTIMER_, drv->tm_hsync[0], DBL2TUNIT((double)(drv->t.h_ev_pos[H_EV_SYNCE]*hsync)/total), hsync);  // ���󓯊��p���X�I���^�C�~���O�͎g���ĂȂ�
	Timer_ChangeStartAndPeriod(_MAINTIMER_, drv->tm_hsync[1], DBL2TUNIT((double)(drv->t.h_ev_pos[H_EV_START]*hsync)/total), hsync);
	Timer_ChangeStartAndPeriod(_MAINTIMER_, drv->tm_hsync[2], DBL2TUNIT((double)(drv->t.h_ev_pos[H_EV_END  ]*hsync)/total), hsync);
	Timer_ChangeStartAndPeriod(_MAINTIMER_, drv->tm_hsync[3], hsync, hsync);
	}

	// SP�␳�X�V
	UpdateSpOffset(drv);

	// �X�V�t���O�𗎂Ƃ�
	drv->t.update = FALSE;

//LOG(("UpdateCrtTiming : HRL=%d, H=%d/%d/%d/%d(%d), V=%d/%d/%d/%d, %dx%d %.3fkHz/%.3fHz", hrl, drv->t.h_ev_pos[0], drv->t.h_ev_pos[1], drv->t.h_ev_pos[2], drv->t.h_ev_pos[3], CRTC(8), drv->t.v_ev_pos[0], drv->t.v_ev_pos[1], drv->t.v_ev_pos[2], drv->t.v_ev_pos[3], drv->vid->h_sz, drv->vid->v_sz, drv->t.hsync_hz/1000, drv->t.vsync_hz));
}

static void DoFastClear(X68000_DRIVER* drv)
{
	/*
		FastClr ����̒��ӓ_�iPITAPAT/DYNAMITE DUKE �ӂ�̋������j

		1. CRTC����|�[�g �� 0x02 �𗧂Ă�ƋN���ҋ@��Ԃɓ���i���̎��_�ł̓��[�h�o�b�N���Ă� 0x02 ���Ԃ�Ȃ��j
		2. VDISP �J�n���_�œ��쒆�ɂȂ�iCRTC����|�[�g�̃��[�h�o�b�N�� 0x02 �����j
		3. VDISP ���ԓ��ɏ��������s���A���� VBLANK �˓����_�ŏI������i0x02 ��������j

		���m�ɂ�1�t���[�������Ė����X�^�N���A���Ă����̂��������B
		�܂��AVDP �� GrDrawLine �̂悤�ɁA�e GP ���Ƃ̃X�N���[�����W�X�^�𔽉f������̂��������Ǝv����B
		���A�߂�ǂ��̂œK���ɊȈՉ� �� �����ňꊇ��������B
	*/
	static UINT32 FAST_CLEAR_MASK[16] = {
		0xFFFF, 0xFFF0, 0xFF0F, 0xFF00, 0xF0FF, 0xF0F0, 0xF00F, 0xF000,
		0x0FFF, 0x0FF0, 0x0F0F, 0x0F00, 0x00FF, 0x00F0, 0x000F, 0x0000
	};
	const UINT32 mask = FAST_CLEAR_MASK[ drv->vid->crtc[0x2B] & 0x0F ];
	const UINT32 r20 = CRTC(20);
	const UINT32 w = ( r20 & 0x03 ) ? 512 : 256;
	const UINT32 h = ( r20 & 0x04 ) ? 512 : 256;
	const UINT32 scrx0 = CRTC(12);
	UINT32 scry = CRTC(13);
	UINT16* vram = drv->vid->gvram;
	UINT32 y = h;
	while ( y-- ) {
		UINT16* ptr = vram + ( ( scry & 0x1FF ) << 9 );  // 512 words / line
		UINT32 scrx = scrx0;
		UINT32 x = w;
		while ( x-- ) {
			ptr[ scrx & 0x1FF ] &= mask;
			scrx++;
		}
		scry++;
	}
//LOG(("FastClear : w=%d, h=%d, state=%d, mask=$%04X", w, h, drv->crtc_op.fastclr_state, mask));
}

static void DoRasterCopy(X68000_DRIVER* drv)
{
	UINT32 src = drv->vid->crtc[0x2C];
	UINT32 dst = drv->vid->crtc[0x2D];
	if ( src != dst ) {
		UINT32 plane = drv->vid->crtc[0x2B];
		if ( plane & 0x01 ) { UINT8* p = ( (UINT8*)drv->vid->tvram ) + 0x00000; memcpy(p+(dst<<9), p+(src<<9), 0x200); }
		if ( plane & 0x02 ) { UINT8* p = ( (UINT8*)drv->vid->tvram ) + 0x20000; memcpy(p+(dst<<9), p+(src<<9), 0x200); }
		if ( plane & 0x04 ) { UINT8* p = ( (UINT8*)drv->vid->tvram ) + 0x40000; memcpy(p+(dst<<9), p+(src<<9), 0x200); }
		if ( plane & 0x08 ) { UINT8* p = ( (UINT8*)drv->vid->tvram ) + 0x60000; memcpy(p+(dst<<9), p+(src<<9), 0x200); }
		if ( plane ) {
			UINT32 l = dst << 2;
			drv->vid->tx_dirty[l>>5] |= 0x0F << (l&31);
		}
//LOG(("RasterCopy : $%02X -> $%02X, plane=$%02X", src, dst, plane));
	}
	drv->crtc_op.do_raster_copy = FALSE;
}


// --------------------------------------------------------------------------
//   Callbacks
// --------------------------------------------------------------------------
static int CALLBACK X68000_IrqVectorCb(void* prm, unsigned int irq)
{
	/*
		Level7 : NMI�X�C�b�`�i�I�[�g�x�N�^�j
		Level6 : MFP
		Level5 : SCC
		Level4 : MIDI (�{�[�h��̃X�C�b�`�őI��)
		Level3 : DMAC
		Level2 : MIDI (�{�[�h��̃X�C�b�`�őI��)
		Level1 : I/O�R���g���[��
	*/
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	int ret = -1;
	switch ( irq )
	{
		case 6:  // MFP
			ret = X68MFP_GetIntVector(drv->mfp);
			break;
		case 5:  // SCC
			ret = X68SCC_GetIntVector(drv->scc);
			break;
		case 4:  // MIDI
			ret = X68MIDI_GetIntVector(drv->midi);
			break;
		case 3:  // DMAC
			ret = X68DMA_GetIntVector(drv->dma);
			break;
		case 2:  // MIDI
			ret = X68MIDI_GetIntVector(drv->midi);
			break;
		case 1:  // IOC
			ret = X68IOC_GetIntVector(drv->ioc);
			break;
		default:
			break;
	}
	return ret;
}

static X68000_OpmIntCb(void* prm, BOOL line)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	if ( line ) {
		SET_GPIP(GPIP_BIT_FMIRQ, 0);  // ActiveLow
	} else {
		SET_GPIP(GPIP_BIT_FMIRQ, 1);
	}
}

static void X68000_OpmCtCb(void* prm, UINT8 data)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	/*
		-------2   CT2�FFDC�̋���READY
		------1-   CT1�FADPCM��{�N���b�N�i0:8MHz�A1:4MHz�j
	*/
	X68ADPCM_SetBaseClock(drv->adpcm, ( data & 2 ) ? 4000000 : 8000000);
	X68FDC_SetForceReady(drv->fdc, ( data & 1 ) ? TRUE : FALSE);
}

static BOOL X68000_AdpcmUpdateCb(void* prm)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	BOOL ret =X68DMA_Exec(drv->dma, 3);
	return ret;
}

static void X68000_MouseStatusCb(void* prm, SINT8* px, SINT8* py, UINT8* pstat)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	UINT32 st = drv->mouse.stat;
	SINT32 x = drv->mouse.x;
	SINT32 y = drv->mouse.y;
	if ( x >  127 ) { x =  127; st |= 0x10; }
	if ( x < -128 ) { x = -128; st |= 0x20; }
	if ( y >  127 ) { y =  127; st |= 0x40; }
	if ( y < -128 ) { y = -128; st |= 0x80; }
	*px = (SINT8)x;
	*py = (SINT8)y;
	*pstat = (SINT8)st;
	drv->mouse.x = 0;
	drv->mouse.y = 0;
}

static TIMER_HANDLER(X68000_ContrastCb)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	// XXX �ω����x�s���A����� 1/40 �b��1�i�K�ω��Ƃ��Ă���
	if ( drv->vid->contrast < drv->sysport[1] ) {
		drv->vid->contrast++;
	} else if ( drv->vid->contrast > drv->sysport[1] ) {
		drv->vid->contrast--;
	} else {
		// �ω��I��������^�C�}���~�߂Ă���
		Timer_ChangePeriod(_MAINTIMER_, drv->tm_contrast, TIMERPERIOD_NEVER);
	}
}

static TIMER_HANDLER(X68000_KeyBufCb)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	UINT32 i;
	for (i=0; i<4; i++) {
		UINT32 chg = drv->keybuf.key_state[i] ^ drv->keybuf.key_req[i];
		if ( chg ) {
			UINT32 bit;
			for (bit=0; bit<32; bit++) {
				UINT32 flag = 1 << bit;
				if ( chg & flag ) {
					UINT32 code = i*32 + bit;
					if ( drv->keybuf.key_state[i] & flag ) code |= X68K_KEYFLAG_RELEASE;
					drv->keybuf.key_state[i] ^= flag;
					X68MFP_SetKeyData(drv->mfp, code);
					return;
				}
			}
		}
	}
	Timer_ChangePeriod(_MAINTIMER_, drv->tm_keybuf, TIMERPERIOD_NEVER);
}


// --------------------------------------------------------------------------
//   H/Vsync
// --------------------------------------------------------------------------
static TIMER_HANDLER(X68000_SyncCb)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;

	/*
		�u�A���S�X�̐�m�v��u�N���C�W�[�N���C�}�[�v�Ȃǂ���~������F

		GPIP����������VBLANK��҂��Ă��邪�AVBLANK Int���L���ł����炪��ɑ��違���荞�ݏ���������
		VBLANK���Ԃɏ������I���Ȃ����߁A�i�v��VBLANK�t���O���������Ȃ��Ȃ�A�Ƃ������B
		���@�ł͒��xVBLANK�ɓ������u�Ԃ�GPIP��ǂ݁A����Ɋ��荞�݂�����A�Ƃ����^�C�~���O�����݂�
		��Ǝv���邪�A�G�~�����Ƃ��ꂪ�������Ȃ������i���̃C�x���g������VBLANK�ɓ���Ɠ����Ɋ���
		���ݔ���������OP�R�[�h�O�Ɋ��荞�ݏ����ɓ����Ă��܂��A�Ƃ��������̂��߁AVBLANK�ɓ������u��
		����荞�ރ^�C�~���O�����݂��Ȃ��j�B

		����Ă��̃G�~���ł́A�����E�����J�E���^�C�x���g�Ŕ�������MFP�̊��荞�݂�������ԃy���f�B��
		�O���Ă����Ă��犄�荞�݂��グ�邱�ƂŁA��L���������悤�ɂ��Ă���iIRQ�^�C�}�̓��AMFP�p
		��6�Ԃ���0.5us�قǂ̒x�������Ă���j�B

		KLAX�A���^���I�����WEX�ӂ�̒�~�������B
	*/

	switch ( opt )
	{
	default:
	case H_EV_SYNCE:  // H-TOP -> H-SYNC END
		break;

	case H_EV_START:  // H-SYNC END -> H-START
		SET_GPIP(GPIP_BIT_HSYNC, 0);
		break;

	case H_EV_END:    // H-START -> H-END
		SET_GPIP(GPIP_BIT_HSYNC, 1);
		if ( drv->crtc_op.do_raster_copy ) DoRasterCopy(drv);

		// ���X�^���荞�݂͓����C���̕`��Ɠ������A������x���^�C�~���O�łȂ��ƁA�����w�����C�Y�ŕ\���S�~���o��
		// �[�r�E�X��MFP��CIRQ��������̂ŁA�����Ƃ�����x�̊���Low���o�Ȃ��ƃ_��
		if ( drv->t.v_count == drv->t.vint_line ) {
			SET_GPIP(GPIP_BIT_CIRQ, 0);  // ActiveLow
		}

		// ���̃��C����`��i��L�ɍ��킹��HDISP�I���^�C�~���O�Ɉړ��j
		// 10MHz���쎞�̃h���L�����̃X�e�[�^�X�����ɂ܂�ɂ�������o�邪�A�^�C�~���O�I�ɂ͂������]�܂���
		if ( drv->t.next_v_ev == V_EV_END )  // �� EV �� V-END�A�܂茻�� V-DISP ����
		{
			// VDISP���ԂȂ̂�1���C���`��
			UINT32 line = drv->t.v_count - drv->t.v_ev_pos[V_EV_START];
			// �\�����[�h�ʂɕK�v�ȃ��X�^��`��
			switch ( drv->t.line_shift )
			{
			case DRAW_LINE_DOUBLE:  // �W���𑜓x�E512���C���i�C���^���[�X�j
				X68Video_LineUpdate(drv->vid, (line<<1)+0);
				X68Video_LineUpdate(drv->vid, (line<<1)+1);
				break;
			case DRAW_LINE_HALF:    // ���𑜓x�E256���C���i����C���̎��̂ݕ`���j
				if ( line & 1 ) {
					X68Video_LineUpdate(drv->vid, line>>1);
				}
				break;
			case DRAW_LINE_NORMAL:  // �W���i�����C���`���j
			default:
				X68Video_LineUpdate(drv->vid, line);
				break;
			}
		}
		break;

	case H_EV_TOTAL:  // H-END -> H-TOTAL
		// ���X�^�J�E���g�̐i�s
		drv->t.v_count++;
//		drv->t.h_count -= drv->t.h_ev_pos[H_EV_TOTAL];  // �C�x���g�쓮�ɂȂ����̂Ō��݂͎g���ĂȂ�

		// ���X�^���荞��OFF�i���݂̏�ԂɊ֌W�Ȃ��j
		SET_GPIP(GPIP_BIT_CIRQ, 1);

		// ���X�^�C�x���g�i�s
		while ( drv->t.v_count >= drv->t.v_ev_pos[drv->t.next_v_ev] )
		{
			switch ( drv->t.next_v_ev )
			{
			default:
			case V_EV_SYNCE:  // V-TOP -> V-SYNC END
				drv->t.next_v_ev++;
				break;

			case V_EV_START:  // V-SYNC END -> V-START
				drv->t.next_v_ev++;
				SET_GPIP(GPIP_BIT_VDISP, 1);
				// �����N���A���s�҂���ԂȂ�J�n��ԂɈڍs����i���s�������ōs���j
				if  ( drv->crtc_op.fastclr_state == FASTCLR_WAIT ) {
					drv->crtc_op.fastclr_state = FASTCLR_EXEC;
					DoFastClear(drv);
				}
				break;

			case V_EV_END:    // V-START -> V-END
				drv->t.next_v_ev++;
				SET_GPIP(GPIP_BIT_VDISP, 0);
				// �����N���A�J�n��ԂȂ�I����ԂɈڍs�i�v���ҋ@��Ԃɖ߂�j
				if  ( drv->crtc_op.fastclr_state == FASTCLR_EXEC ) {
					drv->crtc_op.fastclr_state = FASTCLR_IDLE;
				}
				// �����ŉ�ʊm��iVDP���z�X�N���[������\���e�N�X�`���֓]���j
				X68Video_Update(drv->vid, drv->drv.scr);
				drv->drv.scr->frame++;
				drv->t.drawskip = FALSE;
				if ( drv->t.update ) UpdateCrtTiming(drv);  // ������������ɖ߂�u�ԂɃ^�C�~���O�p�����[�^���X�V����
				break;

			case V_EV_TOTAL:  // V-END -> V-TOTAL
				drv->t.next_v_ev = 0;
				drv->t.v_count = 0;
				break;
			}
		}
		break;
	}

	// DMA#0�`#2���s�i#3��ADPCM����T���v����[���K�v�Ȏ��ɌĂяo���j
	// DMA#0�iFDC�j
	if ( drv->fast_fdd ) {
		while ( X68DMA_Exec(drv->dma, 0) ) {
			// �\�Ȍ���A���Ăяo�����Ă݂�
		}
	} else {
		// �uNAIOUS�v��u�a�vOP�A�u�t�@�����N�X�vOP�ȂǁAFDD�A�N�Z�X����������ƃV�[�P���X�������^�C�g���p�ɁA
		// �f�t�H���g�ł͒x�߁i1���X�^��2��j
		if ( opt & 1 ) X68DMA_Exec(drv->dma, 0);
	}
	// ���̑��͐����C�x���g���ƂɁi1���X�^��3��j�Ă�ł݂�
	X68DMA_Exec(drv->dma, 1);
	X68DMA_Exec(drv->dma, 2);
}


// --------------------------------------------------------------------------
//   Open Bus Access
// --------------------------------------------------------------------------
static MEM16R_HANDLER(X68000_ReadOpenBus)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
//LOG(("### BusErr(R) : $%06X", adr));
	X68DMA_BusErr(drv->dma, adr, TRUE);   // DMA�����E��Ȃ��������́AX68DMA_BusErr() ����CPU���֒ʒm�����
	return 0xFF;
}

static MEM16W_HANDLER(X68000_WriteOpenBus)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
//LOG(("### BusErr(W) : $%06X", adr));
	X68DMA_BusErr(drv->dma, adr, FALSE);  // DMA�����E��Ȃ��������́AX68DMA_BusErr() ����CPU���֒ʒm�����
}

static MEM16R_HANDLER(X68000_DUMMY_R)
{
	// �o�X�G���[�̋N���Ȃ��󂫃A�h���X��ԗp
	return 0xFF;
}

static MEM16W_HANDLER(X68000_DUMMY_W)
{
	// �o�X�G���[�̋N���Ȃ��󂫃A�h���X��ԗp
}


// --------------------------------------------------------------------------
//   I/O Read
// --------------------------------------------------------------------------
static MEM16R_HANDLER(X68000_GVRAM_R)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	UINT32 reg28 = drv->vid->crtc[0x28];
	UINT8* gvram = (UINT8*)(drv->vid->gvram);
	UINT32 ret = 0;

	adr &= 0x1FFFFF;

	// GVRAM �̃A�h���X���Z�̍l�����ɂ��Ă� X68000_GVRAM_W �̃R�����g�Q��
	switch ( reg28 & 0x0B )
	{
		case 0x00:  // 16�F
			if ( adr & 1 ) {
				if ( reg28 & 0x04 ) { 
					// 1024 dot
					UINT16* wp = (UINT16*)(&gvram[((adr&0xFF800)>>1)+(adr&0x3FE)]);
					UINT32 sft = ( (adr>>17) & 0x08 ) + ( (adr>>8)&0x04 );
					ret = ( *wp >> sft ) & 0x0F;
				} else { 
					// 512 dot
					UINT16* wp = (UINT16*)(&gvram[adr&0x7FFFE]);
					UINT32 sft = ( adr>>17 ) & 0x0C;
					ret = ( *wp >> sft ) & 0x0F;
				}
			}
			break;

		case 0x01:  // 256�F
		case 0x02:  // ����`�i256�F�Ɠ�������H �� �O��0x80000��256�F�Ɠ����A�㔼��0���Ԃ�H�j
			if ( ( adr < 0x100000 ) && ( adr & 1 ) ) {
				adr ^= (adr>>19) & 1;
				adr = (adr^LE_ADDR_SWAP) & 0x7FFFF;
				ret = gvram[adr];
			}
			break;

		default:   // 65536�F  0x08 �������Ă���ꍇ�����̃������z�u���ۂ��iNEMESIS/��ݓ��ߕ����j
			if ( adr < 0x080000 ) {
				adr ^= LE_ADDR_SWAP;
				ret = gvram[adr];
			}
			break;
	}

	return ret;
}

static MEM16R_HANDLER(X68000_CRTC_R)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	adr &= 0x7FF;
	if ( adr<0x400 ) {
		adr &= 0x3F;
		if ( adr>=0x28 && adr<=0x2B ) {
			// ���[�h�o�b�N�ł���̂� R20/R21 �̂݁H
			return drv->vid->crtc[adr&0x3F];
		} else if ( adr<0x30 ) {
			// ���̑���CRTC���W�X�^��0���Ԃ�
			return 0x00;
		}
	} else if ( adr>=0x480 && adr<=0x4FF ) {
		UINT32 ret = 0x00;
		if ( adr & 1 ) {
			// ���X�^�R�s�[�r�b�g�A�y�э����N���A���쒆�t���O�������Ԃ�
			ret = drv->crtc_op.reg;
			if  ( drv->crtc_op.fastclr_state == FASTCLR_EXEC ) {
				ret |= 0x02;
			}
		}
		return ret;
	}
	return 0xFF;
}

static MEM16R_HANDLER(X68000_VCTRL_R)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	UINT32 ret = 0;  // ��f�R�[�h�̈�̕Ԓl��0

	adr &= 0x0FFF;  // $E82000-$E83FFF / �㔼�̓~���[

	if ( adr < 0x400 ) {
		// �p���b�g�G���A
		const UINT8* p = (const UINT8*)drv->vid->pal;
		ret = p[adr^LE_ADDR_SWAP];
	} else {
		// 0x0400�`0x0FFF �̓��[�h�P�ʂŃ~���[
		// 0x0800�ȍ~�ւ̃A�N�Z�X�́A�f�R�[�h�͂���Ă��Ȃ����o�X�G���[�͏o�Ȃ��i�C���[�W�t�@�C�g �f���J�n���O�j
		switch ( adr&0x701 )
		{
			case 0x400:  // $E82400
				ret = (UINT8)(drv->vid->vctrl0>>8);
				break;
			case 0x401:  // $E82401
				ret = (UINT8)(drv->vid->vctrl0>>0);
				break;
			case 0x500:  // $E82500
				ret = (UINT8)(drv->vid->vctrl1>>8);
				break;
			case 0x501:  // $E82501
				ret = (UINT8)(drv->vid->vctrl1>>0);
				break;
			case 0x600:  // $E82600
				ret = (UINT8)(drv->vid->vctrl2>>8);
				break;
			case 0x601:  // $E82601
				ret = (UINT8)(drv->vid->vctrl2>>0);
				break;
		}
	}

	return ret;
}

static MEM16R_HANDLER(X68000_SYSPORT_R)
{
	// ���g�p�r�b�g��1��Ԃ��悤�ύX
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	UINT8 ret = 0;
	switch ( adr & 0x0F )
	{
		case 0x01:  // SysPort #1
			ret = drv->sysport[1] | 0xF0;
			break;
		case 0x03:  // SysPort #2
			ret = drv->sysport[2] | 0xF4;
			ret &= ~0x08;  // �f�B�X�v���CON�ɌŒ�
			break;
		case 0x05:  // SysPort #3
			ret = drv->sysport[3] | 0xE0;
			break;
		case 0x07:  // SysPort #4
			ret = drv->sysport[4] | 0xF1;
			ret |= 0x08;   // �L�[�{�[�h�ڑ���ԂɌŒ�
			break;
		case 0x0B:  // 10MHz:0xFF�A16MHz:0xFE�A030(25MHz):0xDC�����ꂼ��Ԃ��炵��
			ret = ( X68000_CPU_CLK < 16000000 ) ? 0xFF : 0xFE;
			break;
		case 0x0D:  // SysPort #5
			ret = drv->sysport[5] | 0x00;
			break;
		case 0x0F:  // SysPort #6
			ret = drv->sysport[6] | 0xF0;
			break;
		default:
			break;
	}
	return ret;
}

static MEM16R_HANDLER(X68000_PPI_R)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	UINT8 ret = 0xFF;
	switch ( adr & 0x07 )
	{
		case 0x01:  // JoyStick #1
			ret = drv->joyport[0];
			break;
		case 0x03:  // JoyStick #2
			ret = drv->joyport[1];
			break;
		case 0x05:  // PortC
			ret = drv->ppi.portc;
			break;
		default:
			break;
	}
	return ret;
}

static MEM16R_HANDLER(X68000_SRAM_R)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	return drv->sram[adr&0x3FFF];
}



// --------------------------------------------------------------------------
//   I/O Write
// --------------------------------------------------------------------------
static MEM16W_HANDLER(X68000_GVRAM_W)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	UINT32 reg28 = drv->vid->crtc[0x28];
	UINT8* gvram = (UINT8*)(drv->vid->gvram);

	adr &= 0x1FFFFF;

	switch ( reg28 & 0x0B )
	{
		case 0x00:  // 16�F
			if ( adr & 1 ) {
				if ( reg28 & 0x04 ) { 
					// 1024 dot
					/*
						Inside X68000 p.192 ���

						1024 x 1024 ���[�h�͎��ۂɂ͈ȉ��̂悤�Ȕz�u�ɂȂ��Ă���A
						GD0 �� GVRAM 16bit �� bit0�`3�AGD1 �� bit4�`7�AGD2 �� bit8�`11�A
						GD2 �� bit12�`15 �Ƃ������蓖�ĂɂȂ�B

						       1024dot
						    <----------->
						    -------------  -
						    | GD0 | GD1 |  |
						    -------------  | 1024dot
						    | GD2 | GD3 |  |
						    -------------  -

						����āAGD0�`GD3 �� 65536 �F���[�h�̃A�h���X�z�u�Ɋ��Z������ŁA
						16bit �̓���L�ɑΉ����� 4bit ������������΂悢�B
					*/
					UINT16* wp = (UINT16*)(&gvram[((adr&0xFF800)>>1)+(adr&0x3FE)]);
					UINT32 sft = ( (adr>>17) & 0x08 ) + ( (adr>>8)&0x04 );
					*wp = ( *wp & ~(0x0F<<sft) ) | ( (data&0x0F)<<sft );
				} else { 
					// 512 dot
					/*
						������͒P���ɁA�y�[�W0�`�y�[�W3�̏��� 0/4/8/12 bit �ځ`�� 4bit
						�Ɋ��蓖�āB
					*/
					UINT16* wp = (UINT16*)(&gvram[adr&0x7FFFE]);
					UINT32 sft = ( adr>>17 ) & 0x0C;
					*wp = ( *wp & ~(0x0F<<sft) ) | ( (data&0x0F)<<sft );
				}
			}
			break;

		case 0x01:  // 256�F
		case 0x02:  // ����`�i256�F�Ɠ�������H �� �O��0x80000��256�F�Ɠ����A�㔼�͖��������H�j
			/*
				16�F���͒P���B
				16bit �P�ʂ� GVRAM �ɂ����āA�y�[�W0�����ʃo�C�g�A�y�[�W1����ʃo�C�g�ɂȂ�B
			*/
			if ( ( adr < 0x100000 ) && ( adr & 1 ) ) {
				adr ^= (adr>>19) & 1;
				adr = (adr^LE_ADDR_SWAP) & 0x7FFFF;
				gvram[adr] = (UINT8)data;
			}
			break;

		default:   // 65536�F  0x08 �������Ă���ꍇ�����̃������z�u���ۂ��iNEMESIS/��ݓ��ߕ����j
			if ( adr < 0x080000 ) {
				adr ^= LE_ADDR_SWAP;
				gvram[adr] = (UINT8)data;
			}
			break;
	}
}

static MEM16W_HANDLER(X68000_TVRAM_W)
{
	// �����A�N�Z�X/�}�X�N�ɂ���Ċ֐��ŏ�����������������H
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	UINT8* tvram = (UINT8*)drv->vid->tvram;
	UINT32 reg2A = drv->vid->crtc[0x2A];
	adr ^= LE_ADDR_SWAP;

	// �e�L�X�gVRAM��1���C�� 0x80 �o�C�g�Ȃ̂ŁAadr>>7 �����C���ԍ��ɂȂ�
	#define SET_DIRTY                     { UINT32 l = (adr>>7) & 1023; drv->vid->tx_dirty[l>>5] |= (1<<(l&31)); }
	#define MASK_DATA(_ofs_)              ( tvram[adr+_ofs_] & m ) | ( data & ~m );
	#define WRITE_TX(_ofs_,_dt_,_dirty_)  if ( tvram[adr+_ofs_] != (UINT8)_dt_ ) { tvram[adr+_ofs_] = (UINT8)_dt_; _dirty_; }

	if ( reg2A & 0x01 ) {
		// �����A�N�Z�XON
		UINT32 reg2B = drv->vid->crtc[0x2B];
		UINT32 dirty = 0;
		adr &= 0x1FFFF;
		if ( reg2A & 0x02 ) {
			// �}�X�N����
			UINT32 m = drv->vid->crtc[0x2E+((adr&1)^LE_ADDR_SWAP)];
			if ( reg2B & 0x10 ) { UINT32 d = MASK_DATA(0x00000); WRITE_TX(0x00000, d, dirty=1); }
			if ( reg2B & 0x20 ) { UINT32 d = MASK_DATA(0x20000); WRITE_TX(0x20000, d, dirty=1); }
			if ( reg2B & 0x40 ) { UINT32 d = MASK_DATA(0x40000); WRITE_TX(0x40000, d, dirty=1); }
			if ( reg2B & 0x80 ) { UINT32 d = MASK_DATA(0x60000); WRITE_TX(0x60000, d, dirty=1); }
		} else {
			if ( reg2B & 0x10 ) { WRITE_TX(0x00000, data, dirty=1); }
			if ( reg2B & 0x20 ) { WRITE_TX(0x20000, data, dirty=1); }
			if ( reg2B & 0x40 ) { WRITE_TX(0x40000, data, dirty=1); }
			if ( reg2B & 0x80 ) { WRITE_TX(0x60000, data, dirty=1); }
		}
		if ( dirty ) {
			SET_DIRTY;
		}
	} else {
		// �����A�N�Z�XOFF
		adr &= 0x7FFFF;
		if ( reg2A & 0x02 ) {
			// �}�X�N����
			UINT32 m = drv->vid->crtc[0x2E+((adr&1)^LE_ADDR_SWAP)];
			data = MASK_DATA(0);
		}
		WRITE_TX(0, data, SET_DIRTY);
	}
}

static MEM16W_HANDLER(X68000_BGRAM_W)
{
	// XXX �S�Ă�BGRAM�A�N�Z�X���t�b�N����̂ƁA�����C���ȉ��̌v�Z������̂Ƃł͂ǂ������y�����͔����H
	//     �抸��������͑O�҂̕����}�V�A�Ƃ������f
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;

	// ��ʃ��[�h���W�X�^�ύX���ɁABG/SP�`��p�p�����[�^���X�V����
	if ( adr>=0xEB080A && adr<=0xEB0811 && adr&1 )
	{
		UpdateSpOffset(drv);
	}
}

static MEM16W_HANDLER(X68000_CRTC_W)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	adr &= 0x7FF;

	if ( adr<0x400 ) {
		// CRTC�G���A 0x40�u���b�N���[�v
		adr &= 0x3F;
		switch ( adr )
		{
			// �L���r�b�g������荞�ނ悤�ɕύX�iSTAR MOBILE�j
			// ��ʖ���
			case 0x00:  // H-TOTAL   (H)
			case 0x02:  // H-SYNCEND (H)
			case 0x04:  // H-DISP    (H)
			case 0x06:  // H-DISPEND (H)
			case 0x10:  // H-ADJUST  (H)
				break;

			// �^�C�~���O�X�V�K�v ���2bit�̂ݗL��
			case 0x08:  // V-TOTAL   (H)
			case 0x0A:  // V-SYNCEND (H)
			case 0x0C:  // V-DISP    (H)
			case 0x0E:  // V-DISPEND (H)
				data &= 0x03;
				if ( drv->vid->crtc[adr] != data ) drv->t.update = TRUE;
				drv->vid->crtc[adr] = (UINT8)data;
				break;

			// �^�C�~���O�X�V�K�v �Sbit�L��
			case 0x01:  // H-TOTAL   (L)
			case 0x03:  // H-SYNCEND (L)
			case 0x05:  // H-DISP    (L)
			case 0x07:  // H-DISPEND (L)
			case 0x09:  // V-TOTAL   (L)
			case 0x0B:  // V-SYNCEND (L)
			case 0x0D:  // V-DISP    (L)
			case 0x0F:  // V-DISPEND (L)
			case 0x11:  // H-ADJUST  (L)
			case 0x29:  // ���������[�h/�\�����[�h����(L)
				if ( drv->vid->crtc[adr] != data ) drv->t.update = TRUE;
				drv->vid->crtc[adr] = (UINT8)data;
				break;

			// ���X�^���荞��
			// ���ꃉ�C���ŕ���������Ă͂����Ȃ����ۂ�
			// �Ȃ̂ł����ł̊��荞�݃`�F�b�N�͍s��Ȃ��i�����w�����C�Y�AKnightArms�Ȃǁj
			case 0x12:  // ��ʂ�2bit�̂ݗL��
				data &= 0x03;
			case 0x13:
				drv->vid->crtc[adr] = (UINT8)data;
				drv->t.vint_line = CRTC(9);
#if 0
				if ( drv->t.v_count == drv->t.vint_line ) {
					SET_GPIP(GPIP_BIT_CIRQ, 0);  // ActiveLow
				}
#endif
//LOG(("%03d : RasterInt=$%04X (%03d)", drv->t.v_count, CRTC(9), CRTC(9)));
				break;

			// �X�N���[�����W�X�^�� ���2bit�̂ݗL��
			case 0x14:  // TX scroll X  (H)
			case 0x16:  // TX scroll Y  (H)
			case 0x18:  // GR0 scroll X (H)
			case 0x1A:  // GR0 scroll Y (H)
				data &= 0x03;
				drv->vid->crtc[adr] = (UINT8)data;
				break;

			// �X�N���[�����W�X�^�� ���1bit�̂ݗL��
			case 0x1C:  // GR1 scroll X (H)
			case 0x1E:  // GR1 scroll Y (H)
			case 0x20:  // GR2 scroll X (H)
			case 0x22:  // GR2 scroll Y (H)
			case 0x24:  // GR3 scroll X (H)
			case 0x26:  // GR3 scroll Y (H)
				data &= 0x01;
				drv->vid->crtc[adr] = (UINT8)data;
				break;

			// ���X�^�R�s�[ �Sbit�L��
			case 0x2C: // ���X�^�R�s�[src
			case 0x2D: // ���X�^�R�s�[dst
				if ( ( drv->vid->crtc[adr] != data ) && ( drv->crtc_op.reg & 0x08 ) ) {
					// $E80481 �̃��X�^�R�s�[�t���O��ON�ɂ��Ă����� src/dst ������ύX���ĘA�����s���邱�Ƃ��\�炵���i�h���L�����Ȃǁj
					// ���m�ȓ���Ƃ��ẮAHBLANK �˓����_�Ń��X�^�R�s�[ON�Ȃ�1�񕪂����s�H
					drv->crtc_op.do_raster_copy = TRUE;
				}
				drv->vid->crtc[adr] = (UINT8)data;
				break;

			// ���̑� ��{�I�ɑSbit��荞��
			default:
				drv->vid->crtc[adr] = (UINT8)data;
				break;
		}
	} else if ( adr>=0x480 && adr<=0x4FF ) {
		if ( adr & 1 ) {
			drv->crtc_op.reg = data & 0x08;  // ���X�^�R�s�[�t���O�����ۑ�
			if ( drv->crtc_op.reg ) {
				// TXRAM���X�^�R�s�[ON
				// XXX AQUALES��OP��1���X�^��2��src/dst���W�X�^������������̂ŁA�t���OON���ɂ͑������s���Ă���
				DoRasterCopy(drv);
//LOG(("%03d : RasterCopy enable", drv->t.v_count));
			} else if ( ( data & 0x02 ) && ( drv->crtc_op.fastclr_state == FASTCLR_IDLE ) ) {
				// GVRAM�����N���A
				// ���X�^�R�s�[�Ƃ̓���ON�ł̓��X�^�R�s�[�D��A�����N���A�͎��s����Ȃ��B
				drv->crtc_op.fastclr_state = FASTCLR_WAIT;
			}
		}
	}
}

static MEM16W_HANDLER(X68000_VCTRL_W)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;

	adr &= 0x0FFF;  // $E82000-$E83FFF / �㔼�̓~���[

	if ( adr < 0x400 ) {
		// �p���b�g�G���A
		UINT8* p = (UINT8*)drv->vid->pal;
		adr ^= LE_ADDR_SWAP;
		p[adr] = (UINT8)data;
		// �e�L�X�g�p���b�g�͈͂̏ꍇ��dirty�t���O�𗧂ĂĂ���
		if ( (UINT32)(adr-0x200) < 0x20 ) {
			drv->vid->txpal_dirty = TRUE;
		}
	} else {
		// 0x0400�`0x0FFF �̓��[�h�P�ʂŃ~���[�i�h���X�s�����ʃ��[�h�Ƀf�[�^������LONG�A�N�Z�X���Ă�j
		UINT32 old;
		switch ( adr&0x701 )
		{
			case 0x400:  // $E82400
				drv->vid->vctrl0 = (drv->vid->vctrl0 & 0x00FF) | ((data&0xFF)<<8);
				break;
			case 0x401:  // $E82401
				drv->vid->vctrl0 = (drv->vid->vctrl0 & 0xFF00) | ((data&0xFF)<<0);
				break;
			case 0x500:  // $E82500
				old = drv->vid->vctrl1;
				drv->vid->vctrl1 = (drv->vid->vctrl1 & 0x00FF) | ((data&0xFF)<<8);
				if ( old != drv->vid->vctrl1 ) {
					X68Video_UpdateMixFunc(drv->vid);  // MIX�֐��X�V
				}
				break;
			case 0x501:  // $E82501
				drv->vid->vctrl1 = (drv->vid->vctrl1 & 0xFF00) | ((data&0xFF)<<0);
				break;
			case 0x600:  // $E82600
				old = drv->vid->vctrl2;
				drv->vid->vctrl2 = (drv->vid->vctrl2 & 0x00FF) | ((data&0xFF)<<8);
				if ( old != drv->vid->vctrl2 ) {
					X68Video_UpdateMixFunc(drv->vid);  // MIX�֐��X�V
				}
				break;
			case 0x601:  // $E82601
				drv->vid->vctrl2 = (drv->vid->vctrl2 & 0xFF00) | ((data&0xFF)<<0);
				break;
		}
	}
}

static MEM16W_HANDLER(X68000_SYSPORT_W)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	switch ( adr & 0x0F )
	{
		case 0x01:  // SysPort #1  ��ʃR���g���X�g
			drv->sysport[1] = data & 0x0F;
			// �R���g���X�g�́A�Ⴆ�� 0 �� 15 �ƕω��������ꍇ�A�ݒ�シ���ɂ��̖��邳�ɂȂ�̂ł͂Ȃ��A���X��
			// �ω�������ۂ��iKnightArms�j
			if ( drv->sysport[1] != drv->vid->contrast ) {
				// �R���g���X�g�ω��p�^�C�}�N��
				// �i���݂�1/40�b���Ƃ�1�i�K�ω��A�x���ƃt�@���^�W�[�]�[���̃��[�f�B���O�O�̂��݂��ڗ��j
				Timer_ChangePeriod(_MAINTIMER_, drv->tm_contrast, TIMERPERIOD_HZ(40));
			}
			break;
		case 0x03:  // SysPort #2  �e���r�^3D�X�R�[�v����
			drv->sysport[2] = data & 0x0B;
			break;
		case 0x05:  // SysPort #3  �J���[�C���[�W���j�b�g�p
			drv->sysport[3] = data & 0x1F;
			break;
		case 0x07:  // SysPort #4  �L�[�{�[�h�R���g���[���^NMI ack
			if ( ( drv->sysport[4] ^ data ) & 0x02 ) {
				// HRL�ύX���̓^�C�~���O�p�����[�^�X�V
				drv->t.update = TRUE;
			}
			drv->sysport[4] = data & 0x0E;
			break;
		case 0x0D:  // SysPort #5  SRAM�������ݐ���
			drv->sysport[5] = data & 0xFF;
			break;
		case 0x0F:  // SysPort #6  �d��OFF����
			drv->sysport[6] = data & 0x0F;
			break;
		default:
			break;
	}
}

static MEM16W_HANDLER(X68000_PPI_W)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	switch ( adr & 0x07 )
	{
		case 0x01:  // JoyStick #1
			break;
		case 0x03:  // JoyStick #2
			break;
		case 0x05:  // PortC (JoyStick disable / ADPCM rate / ADPCM pan)
			/*
				JJJJ----  JoyStick control
				----SS--  ADPCM Sampling Rate (0-3:1/1024,1/768,1/512,Unknown)
				------L-  ADPCM L disable (0:enable 1:disable)
				-------R  ADPCM R disable (0:enable 1:disable)

				Inside X68000 p.295-296 �̃p���Ɋւ���L�q��L/R�t���Ǝv��
			*/
			drv->ppi.portc = (UINT8)data;
			X68ADPCM_SetChannelVolume(drv->adpcm, (drv->ppi.portc&2)?0.0f:1.0f/*L*/, (drv->ppi.portc&1)?0.0f:1.0f/*R*/);
			X68ADPCM_SetPrescaler(drv->adpcm, (drv->ppi.portc>>2)&3);
			break;
		case 0x07:  // Control
			drv->ppi.ctrl  = (UINT8)data;
			if ( !(drv->ppi.ctrl & 0x80) ) {  // �ŏ�ʂ�0�Ȃ�PortC�r�b�g�R���g���[��
				UINT32 bit = (drv->ppi.ctrl>>1) & 7;
				drv->ppi.portc = ( drv->ppi.portc & (~(1<<bit)) ) | ( (drv->ppi.ctrl&1)<<bit );
				X68ADPCM_SetChannelVolume(drv->adpcm, (drv->ppi.portc&2)?0.0f:1.0f/*L*/, (drv->ppi.portc&1)?0.0f:1.0f/*R*/);
				X68ADPCM_SetPrescaler(drv->adpcm, (drv->ppi.portc>>2)&3);
			}
			break;
		default:
			break;
	}
}

static MEM16W_HANDLER(X68000_SRAM_W)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	// SRAM�������݋֎~�t���O�̃`�F�b�N���K�v
	if ( drv->sysport[5] == 0x31 ) {
		drv->sram[adr&0x3FFF] = (UINT8)data;
	}
}



// --------------------------------------------------------------------------
//   �e��f�o�C�X����������
// --------------------------------------------------------------------------
static void InitDevices(X68000_DRIVER* drv)
{
	UINT32 i;

	// CRTC�����l
	static const UINT8 _CRTC_DEFAULT_[] = {
		0x00, 0x89, 0x00, 0x0E, 0x00, 0x1C, 0x00, 0x7C, 0x02, 0x37, 0x00, 0x05, 0x00, 0x28, 0x02, 0x28,
		0x00, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	for (i=0; i<sizeof(_CRTC_DEFAULT_); i++) {
		drv->vid->crtc[i] = _CRTC_DEFAULT_[i];
	}

	// BGRAM�̖��g�p�̈� 0xFF ���߂��Ƃ��Ȃ��ƁA���Z�b�g��ɂȂ����t�@�����N�XOP�̃Y�[�����S�������
	memset((UINT8*)drv->vid->bgram+0x400, 0xFF, 0x400);
	memset((UINT8*)drv->vid->bgram+0x800, 0x00, 0x12);  // ������K�v�H
	memset((UINT8*)drv->vid->bgram+0x812, 0xFF, 0x800-0x012);

	// BGSP�Y�����N���A
	drv->vid->sp_ofs_x = 0;
	drv->vid->sp_ofs_y = 0;
	
	// �V�X�e���|�[�g#4�iHRL�j
	drv->sysport[4] = 0x00;

	// PPI
	X68000_PPI_W((void*)drv, 0xE9A005, 0x0B);  // ADPCM rate & pan

	// IRQ�n���h��
	IRQ_Reset(drv->cpu);

	// �^�C�~���O�p�����[�^�Čv�Z
	UpdateCrtTiming(drv);
}

static void InitSRAM(X68000_DRIVER* drv)
{
	static const UINT8 _SRAM_DEFAULT_[] = {
		0x82, 0x77, 0x36, 0x38, 0x30, 0x30, 0x30, 0x57, 0x00, 0x20, 0x00, 0x00, 0x00, 0xBF, 0xFF, 0xFC,
		0x00, 0xED, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x4E, 0x07, 0x00, 0x10, 0x00, 0x00,
		0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x07, 0x00, 0x0E, 0x00, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xF8, 0x3E, 0xFF, 0xC0, 0xFF, 0xFE, 0xDE, 0x6C, 0x40, 0x22, 0x03, 0x02, 0x00, 0x08, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xDC, 0x00, 0x04, 0x00, 0x01, 0x01,
		0x00, 0x00, 0x00, 0x20, 0x00, 0x09, 0xF9, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x56,
		0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	UINT32 i;
	for (i=0; i<sizeof(_SRAM_DEFAULT_); i++) {
		drv->sram[i] = _SRAM_DEFAULT_[i];
	}
}

static void CALLBACK ResetInternal(X68000_DRIVER* drv)
{
	// MUSASHI����̃��Z�b�g���߃n���h��
	// CPU�ȊO�����Z�b�g����iCPU�܂Ń��Z�b�g����ƁA���Z�b�g�R�[���o�b�N�������ɔ��ł���j
	InitDevices(drv);
	X68OPM_Reset(drv->opm);
	X68ADPCM_Reset(drv->adpcm);
	X68MFP_Reset(drv->mfp);
	X68DMA_Reset(drv->dma);
	X68IOC_Reset(drv->ioc);
//	X68FDD_Reset(drv->fdd);  // �h���C�u�͂��̂܂�
	X68FDC_Reset(drv->fdc);
	X68RTC_Reset(drv->rtc);
	X68SCC_Reset(drv->scc);
	X68MIDI_Reset(drv->midi);
	X68SASI_Reset(drv->sasi);
}


// --------------------------------------------------------------------------
//   �������}�b�v��`
// --------------------------------------------------------------------------
#define MEMMAP(st,ed,rm,wm,rh,wh,hp)  Mem16_SetHandler(drv->mem,st,ed,rm,wm,rh,wh,hp)

static void SetupRamMap(X68000_DRIVER* drv)
{
	 MEMMAP( 0x000000, drv->ram_size-1, drv->ram, drv->ram, NULL, NULL, NULL );
	 if (  drv->ram_size < 0xC00000 ) {
		 MEMMAP( drv->ram_size, 0xBFFFFF, NULL, NULL, NULL, NULL, NULL );
	 }
}

static void SetupMemoryMap(X68000_DRIVER* drv)
{
	/*       �A�h���X�͈�        RD�|�C���^       WR�|�C���^       RD�n���h��        WR�n���h��        �n���h��prm */
	 MEMMAP( 0xC00000, 0xDFFFFF, NULL,            NULL,            X68000_GVRAM_R,   X68000_GVRAM_W,   drv        );
	 MEMMAP( 0xE00000, 0xE7FFFF, drv->vid->tvram, NULL,            NULL,             X68000_TVRAM_W,   drv        );
	 MEMMAP( 0xE80000, 0xE81FFF, NULL,            NULL,            X68000_CRTC_R,    X68000_CRTC_W,    drv        );
	 MEMMAP( 0xE82000, 0xE83FFF, NULL,            NULL,            X68000_VCTRL_R,   X68000_VCTRL_W,   drv        );
	 MEMMAP( 0xE84000, 0xE85FFF, NULL,            NULL,            X68DMA_Read,      X68DMA_Write,     drv->dma   );
	 MEMMAP( 0xE86000, 0xE87FFF, NULL,            NULL,            NULL,             X68000_DUMMY_W,   drv        );  /* XXX Area set  ���[�h���̓o�X�G���[ */
	 MEMMAP( 0xE88000, 0xE89FFF, NULL,            NULL,            X68MFP_Read,      X68MFP_Write,     drv->mfp   );
	 MEMMAP( 0xE8A000, 0xE8BFFF, NULL,            NULL,            X68RTC_Read,      X68RTC_Write,     drv->rtc   );
	 MEMMAP( 0xE8C000, 0xE8DFFF, NULL,            NULL,            X68000_DUMMY_R,   X68000_DUMMY_W,   drv        );  /* XXX Printer  W/O �o�X�G���[�͏o�Ȃ� */
	 MEMMAP( 0xE8E000, 0xE8FFFF, NULL,            NULL,            X68000_SYSPORT_R, X68000_SYSPORT_W, drv        );
	 MEMMAP( 0xE90000, 0xE91FFF, NULL,            NULL,            X68OPM_Read,      X68OPM_Write,     drv->opm   );
	 MEMMAP( 0xE92000, 0xE93FFF, NULL,            NULL,            X68ADPCM_Read,    X68ADPCM_Write,   drv->adpcm );
	 MEMMAP( 0xE94000, 0xE95FFF, NULL,            NULL,            X68FDC_Read,      X68FDC_Write,     drv->fdc   );
	 MEMMAP( 0xE96000, 0xE97FFF, NULL,            NULL,            X68SASI_Read,     X68SASI_Write,    drv->sasi  );
	 MEMMAP( 0xE98000, 0xE99FFF, NULL,            NULL,            X68SCC_Read,      X68SCC_Write,     drv->scc   );
	 MEMMAP( 0xE9A000, 0xE9BFFF, NULL,            NULL,            X68000_PPI_R,     X68000_PPI_W,     drv        );
	 MEMMAP( 0xE9C000, 0xE9DFFF, NULL,            NULL,            X68IOC_Read,      X68IOC_Write,     drv->ioc   );
	 MEMMAP( 0xEAE000, 0xEAFFFF, NULL,            NULL,            X68MIDI_Read,     X68MIDI_Write,    drv->midi  );  /* MIDI  $EAE000-EAFFFF�����A�f�R�[�h����Ă���ӏ��i0xEAFA00-0xEAFA0F�Ȃǁj�ȊO�̓o�X�G���[ */
	 MEMMAP( 0xEB0000, 0xEBFFFF, drv->vid->bgram, drv->vid->bgram, NULL,             X68000_BGRAM_W,   drv        );  /* BG�Y�����X�V�̂��߂ɏ������ݑ��̓n���h���ł��t�b�N */
	 MEMMAP( 0xED0000, 0xED3FFF, NULL,            NULL,            X68000_SRAM_R,    X68000_SRAM_W,    drv        );
	 MEMMAP( 0xF00000, 0xFBFFFF, font_endian,     NULL,            NULL,             NULL,             NULL       );
	 MEMMAP( 0xFC0000, 0xFDFFFF, ipl_endian,      NULL,            NULL,             NULL,             NULL       );  /* SASI���f���͂�����IPL�̃~���[���ǂ߂�i�R�����X�j */
	 MEMMAP( 0xFE0000, 0xFFFFFF, ipl_endian,      NULL,            NULL,             NULL,             NULL       );
}


// --------------------------------------------------------------------------
//   �V�X�e���h���C�o��`
// --------------------------------------------------------------------------
static void SetCpuClock(X68000_DRIVER* drv, UINT32 clkidx)
{
	static const UINT32 CLOCK_TABLE[X68K_CLK_INDEX_MAX] = { 10000000, 16666667, 24000000 };
	if ( clkidx >= X68K_CLK_INDEX_MAX ) {
		LOG(("### SetCpuClock : invalid clock setting (idx=%d)", clkidx));
		clkidx = 0;
	}
	drv->cpu_clk = CLOCK_TABLE[clkidx];
}

EMUDRIVER* X68kDriver_Initialize(const UINT8* rom_ipl, const UINT8* rom_font, UINT32 sndfreq)
{
	DRIVER_INIT_START(X68000, X68000_DRIVER);

	if ( !rom_ipl || !rom_font ) {
		DRIVER_INIT_ERROR("### X68kDriver_Initialize : IPL/FONT ROM error");
	}

	// Video
	drv->vid = X68Video_Init();
	if ( !drv->vid ) {
		DRIVER_INIT_ERROR("### X68kDriver_Initialize : VDP initialization error");
	}

	// Boot parameters
	drv->vid->vscr_w = X68_MAX_VSCR_WIDTH;
	drv->vid->vscr_h = X68_MAX_VSCR_HEIGHT;
	SetCpuClock(drv, X68K_CLK_16MHZ);
	drv->ram_size = DEFAULT_RAM_SIZE;

	// ROM setup
	{
		UINT32 i;
		for (i=0; i<sizeof(ipl_endian); i++) {
			ipl_endian[i^LE_ADDR_SWAP] = rom_ipl[i];
		}
		for (i=0; i<sizeof(font_endian); i++) {
			font_endian[i^LE_ADDR_SWAP] = rom_font[i];
		}
	}

	// Memory
	// ��{�I��13bit�u���b�N�i0x2000�j�P�ʂł悢
	drv->mem = Mem16_Init((void*)drv, &X68000_ReadOpenBus, &X68000_WriteOpenBus);
	if ( !drv->mem ) {
		DRIVER_INIT_ERROR("Init Error : Memory Handler");
		break;
	}

	// CPU�p��RAM�͐�Ƀ}�b�s���O����
	SetupRamMap(drv);

	// �N������ SP/PC �� IPL ����R�s�[
	memcpy(drv->ram, ipl_endian+0x010000, 8);

	// CPU
	drv->cpu = X68CPU_Init(_MAINTIMER_, drv->mem, X68000_CPU_CLK, X68CPU_68000, MAKESTATEID('C','P','U','1'));
	if ( !drv->cpu ) {
		DRIVER_INIT_ERROR("M68000 core : initialization failed");
	}
	DRIVER_ADDCPU(X68CPU_Exec, drv->cpu, MAKESTATEID('C','P','U','1'));
	X68CPU_SetIrqCallback(drv->cpu, &X68000_IrqVectorCb, (void*)drv);

	// ���Z�b�g���߂��t�b�N���ăf�o�C�X�ނ����������Ȃ���IPL�́u�G���[���������܂����v���o��ꍇ������
	X68CPU_SetResetCallback(drv->cpu, &ResetInternal, (void*)drv);
	// IRQ6�̒x���ݒ�iX68000_SyncCb() �`���̃R�����g�Q�Ɓj
	IRQ_SetIrqDelay(drv->cpu, 6/*MFP*/, TIMERPERIOD_US(0.5));

	// Sound
	drv->opm = X68OPM_Init(_MAINTIMER_, drv->drv.sound, 4000000, VOLUME_OPM);
	if ( !drv->opm ) {
		DRIVER_INIT_ERROR("OPM core : initialization failed");
	}
	drv->adpcm = X68ADPCM_Init(_MAINTIMER_, drv->drv.sound, 8000000, VOLUME_ADPCM);
	if ( !drv->adpcm ) {
		DRIVER_INIT_ERROR("ADPCM core : initialization failed");
	}

	// �T�E���h�t�B���^
	drv->hpf_adpcm = SndFilter_Create(drv->drv.sound);
	drv->lpf_adpcm = SndFilter_Create(drv->drv.sound);
	drv->lpf_opm   = SndFilter_Create(drv->drv.sound);
	SndFilter_SetPrmHighPass(drv->hpf_adpcm,   115/*Hz*/, 0.7f/*Q*/);  // DC�J�b�g�p�i�G�g�v��SE65�̔g�`����B�v�Z���280Hz�炵���H�j
	SndFilter_SetPrmLowPass (drv->lpf_adpcm,  3700/*Hz*/, 0.7f/*Q*/);  // ���[�p�X�i�W�����_�l3.7kHz�炵���A�^���Ɣ�r���Ă���̂悳���j
	SndFilter_SetPrmLowPass (drv->lpf_opm,   16000/*Hz*/, 0.6f/*Q*/);  // �m�C�Y���͂ł̃X�y�N�g�������A��̂��̂��炢�H
	SndStream_AddFilter(drv->drv.sound, drv->hpf_adpcm, drv->adpcm);
	SndStream_AddFilter(drv->drv.sound, drv->lpf_adpcm, drv->adpcm);
	SndStream_AddFilter(drv->drv.sound, drv->lpf_opm,   drv->opm);

	// Screen Buffer
	DRIVER_SETSCREEN(X68_MAX_VSCR_WIDTH, X68_MAX_VSCR_HEIGHT);
//	DRIVER_SETSCREENCLIP(0, 0, drv->vid->vscr_w, drv->vid->vscr_h);

	{
	UINT32 i;
	for (i=1; i<H_EV_MAX; i++) {
		drv->tm_hsync[i] = Timer_CreateItem(_MAINTIMER_, TIMER_NORMAL, TIMERPERIOD_NEVER, &X68000_SyncCb, (void*)drv, i, MAKESTATEID('S','T','M','0'+i));
	}
	}

	// �R���g���X�g�i�K�ω��p�^�C�} �ω�������Ƃ������s���I�h�ς��ċN������
	drv->tm_contrast = Timer_CreateItem(_MAINTIMER_, TIMER_NORMAL, TIMERPERIOD_NEVER, &X68000_ContrastCb, (void*)drv, 0, MAKESTATEID('C','T','T','M'));
	// �L�[�o�b�t�@��[�p�^�C�}
	drv->tm_keybuf = Timer_CreateItem(_MAINTIMER_, TIMER_NORMAL, TIMERPERIOD_NEVER, &X68000_KeyBufCb, (void*)drv, 0, MAKESTATEID('K','B','T','M'));

	// ���̑��̃f�o�C�X
	drv->mfp = X68MFP_Init(drv->cpu, _MAINTIMER_);
	if ( !drv->mfp ) {
		DRIVER_INIT_ERROR("MFP : initialization failed");
	}
	drv->dma = X68DMA_Init(drv->cpu, drv->mem);
	if ( !drv->dma ) {
		DRIVER_INIT_ERROR("DMA : initialization failed");
	}
	drv->ioc = X68IOC_Init(drv->cpu);
	if ( !drv->ioc ) {
		DRIVER_INIT_ERROR("IOC : initialization failed");
	}
	drv->fdd = X68FDD_Init(drv->ioc);
	if ( !drv->fdd ) {
		DRIVER_INIT_ERROR("FDD : initialization failed");
	}
	drv->fdc = X68FDC_Init(drv->ioc, drv->fdd);
	if ( !drv->fdc ) {
		DRIVER_INIT_ERROR("FDC : initialization failed");
	}
	drv->rtc = X68RTC_Init();
	if ( !drv->rtc ) {
		DRIVER_INIT_ERROR("RTC : initialization failed");
	}
	drv->scc = X68SCC_Init(drv->cpu, _MAINTIMER_);
	if ( !drv->scc ) {
		DRIVER_INIT_ERROR("SCC : initialization failed");
	}
	drv->midi = X68MIDI_Init(drv->cpu, _MAINTIMER_);
	if ( !drv->midi ) {
		DRIVER_INIT_ERROR("MIDI : initialization failed");
	}
	drv->sasi = X68SASI_Init(drv->ioc);
	if ( !drv->sasi ) {
		DRIVER_INIT_ERROR("SASI : initialization failed");
	}

	// �f�o�C�X���������烁�������}�b�s���O
	SetupMemoryMap(drv);

	// �e��f�o�C�X������
	InitDevices(drv);
	InitSRAM(drv);

	// SCC�̃R�[���o�b�N
	X68SCC_SetMouseCallback(drv->scc, &X68000_MouseStatusCb, (void*)drv);

	// �T�E���h�f�o�C�X�n�R�[���o�b�N
	X68OPM_SetIntCallback(drv->opm, &X68000_OpmIntCb, (void*)drv);
	X68OPM_SetPort(drv->opm, &X68000_OpmCtCb, (void*)drv);
	X68ADPCM_SetCallback(drv->adpcm, &X68000_AdpcmUpdateCb, (void*)drv);

	// DMA�pREADY�R�[���o�b�N
	// XXX #2 �͊g���X���b�g�p
	X68DMA_SetReadyCb(drv->dma, 0, &X68FDC_IsDataReady,   drv->fdc   );  // FDC
	X68DMA_SetReadyCb(drv->dma, 1, &X68SASI_IsDataReady,  drv->sasi  );  // SASI
	X68DMA_SetReadyCb(drv->dma, 3, &X68ADPCM_IsDataReady,  drv->adpcm);  // ADPCM

	DRIVER_INIT_END(X68k);
}

void X68kDriver_Cleanup(EMUDRIVER* __drv)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);

	// �f�B�X�N�C���[�W�͓����Ń������m�ۂ��ăR�s�[����Ă�̂ŁA�����j������K�v������
	// ���荞�݂Ȃǂő��f�o�C�X���Ăԉ\��������̂ŁA�f�o�C�X�j�������O�ɍŏ��ɌĂԕK�v����
	X68FDD_EjectDisk(drv->fdd, 0, TRUE);
	X68FDD_EjectDisk(drv->fdd, 1, TRUE);

	X68CPU_Cleanup(drv->cpu);
	Mem16_Cleanup(drv->mem);
	X68OPM_Cleanup(drv->opm);
	X68ADPCM_Cleanup(drv->adpcm);
	SndFilter_Destroy(drv->hpf_adpcm);
	SndFilter_Destroy(drv->lpf_adpcm);
	SndFilter_Destroy(drv->lpf_opm);
	X68Video_Cleanup(drv->vid);
	X68MFP_Cleanup(drv->mfp);
	X68DMA_Cleanup(drv->dma);
	X68IOC_Cleanup(drv->ioc);
	X68FDD_Cleanup(drv->fdd);
	X68FDC_Cleanup(drv->fdc);
	X68RTC_Cleanup(drv->rtc);
	X68SCC_Cleanup(drv->scc);
	X68MIDI_Cleanup(drv->midi);
	X68SASI_Cleanup(drv->sasi);

	DRIVER_CLEAN_END();
}

UINT32 X68kDriver_Exec(EMUDRIVER* __drv, TUNIT period)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER) 0;
	DRIVER_EXEC_END();  // �������^�C�}�쓮���Ă�ł܂�
	X68FDD_UpdateLED(drv->fdd, period);
	return X68CPU_GetExecuteClocks(drv->cpu);
}

void X68kDriver_LoadState(EMUDRIVER* __drv, STATE* state)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	// Timer
	Timer_LoadState(_MAINTIMER_, state, MAKESTATEID('T','I','M','R'));
	// Video
	X68Video_LoadState(drv->vid, state, MAKESTATEID('V','D','E','O'));
	// Devices
	X68IOC_LoadState(drv->ioc, state, MAKESTATEID('X','I','O','C'));
	X68MFP_LoadState(drv->mfp, state, MAKESTATEID('X','M','F','P'));
	X68DMA_LoadState(drv->dma, state, MAKESTATEID('X','D','M','A'));
	X68FDC_LoadState(drv->fdc, state, MAKESTATEID('X','F','D','C'));
	X68FDD_LoadState(drv->fdd, state, MAKESTATEID('X','F','D','D'));
	X68RTC_LoadState(drv->rtc, state, MAKESTATEID('X','R','T','C'));
	X68SCC_LoadState(drv->scc, state, MAKESTATEID('X','S','C','C'));
	X68MIDI_LoadState(drv->midi, state, MAKESTATEID('X','M','I','D'));
	X68SASI_LoadState(drv->sasi, state, MAKESTATEID('X','S','A','S'));
	// Memory
	ReadState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('S','R','M','1'), drv->sram, sizeof(drv->sram));
	ReadState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('R','A','M','1'), drv->ram, sizeof(drv->ram));
	ReadState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('T','I','N','G'), &drv->t, sizeof(drv->t));
	ReadState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('P','P','I','I'), &drv->ppi, sizeof(drv->ppi));
	ReadState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('F','C','L','R'), &drv->crtc_op, sizeof(drv->crtc_op));
	ReadState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('S','Y','S','P'), drv->sysport, sizeof(drv->sysport));
	ReadState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('J','O','Y','P'), drv->joyport, sizeof(drv->joyport));
	ReadState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('M','O','U','S'), &drv->mouse, sizeof(drv->mouse));
	// CPU
	X68CPU_LoadState(drv->cpu, state, MAKESTATEID('C','P','U','1'));
	// Sound
	X68OPM_LoadState(drv->opm, state, MAKESTATEID('O','P','M','1'));
	X68ADPCM_LoadState(drv->adpcm, state, MAKESTATEID('P','C','M','1'));

	// �X�N���[���o�b�t�@�̃N���b�v����
	do {
		UINT32 w = _MIN(drv->vid->h_sz, drv->vid->vscr_w);
		UINT32 h = _MIN(drv->vid->v_sz, drv->vid->vscr_h);
		DRIVER_SETSCREENCLIP(0, 0, w, h);
	} while ( 0 );
}

void X68kDriver_SaveState(EMUDRIVER* __drv, STATE* state)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	// Timer
	Timer_SaveState(_MAINTIMER_, state, MAKESTATEID('T','I','M','R'));
	// Video
	X68Video_SaveState(drv->vid, state, MAKESTATEID('V','D','E','O'));
	// Devices
	X68IOC_SaveState(drv->ioc, state, MAKESTATEID('X','I','O','C'));
	X68MFP_SaveState(drv->mfp, state, MAKESTATEID('X','M','F','P'));
	X68DMA_SaveState(drv->dma, state, MAKESTATEID('X','D','M','A'));
	X68FDC_SaveState(drv->fdc, state, MAKESTATEID('X','F','D','C'));
	X68FDD_SaveState(drv->fdd, state, MAKESTATEID('X','F','D','D'));
	X68RTC_SaveState(drv->rtc, state, MAKESTATEID('X','R','T','C'));
	X68SCC_SaveState(drv->scc, state, MAKESTATEID('X','S','C','C'));
	X68MIDI_SaveState(drv->midi, state, MAKESTATEID('X','M','I','D'));
	X68SASI_SaveState(drv->sasi, state, MAKESTATEID('X','S','A','S'));
	// Memory
	WriteState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('S','R','M','1'), drv->sram, sizeof(drv->sram));
	WriteState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('R','A','M','1'), drv->ram, sizeof(drv->ram));
	WriteState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('T','I','N','G'), &drv->t, sizeof(drv->t));
	WriteState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('P','P','I','I'), &drv->ppi, sizeof(drv->ppi));
	WriteState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('F','C','L','R'), &drv->crtc_op, sizeof(drv->crtc_op));
	WriteState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('S','Y','S','P'), drv->sysport, sizeof(drv->sysport));
	WriteState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('J','O','Y','P'), drv->joyport, sizeof(drv->joyport));
	WriteState(state, MAKESTATEID('D','R','I','V'), MAKESTATEID('M','O','U','S'), &drv->mouse, sizeof(drv->mouse));
	// CPU
	X68CPU_SaveState(drv->cpu, state, MAKESTATEID('C','P','U','1'));
	// Sound
	X68OPM_SaveState(drv->opm, state, MAKESTATEID('O','P','M','1'));
	X68ADPCM_SaveState(drv->adpcm, state, MAKESTATEID('P','C','M','1'));
}


// --------------------------------------------------------------------------
//   �ǉ���I/F�Q
// --------------------------------------------------------------------------
// �{�̃��Z�b�g
void X68kDriver_Reset(EMUDRIVER* __drv)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	// �f�o�C�X���Z�b�g
	ResetInternal(drv);
	// �N������ SP/PC �� IPL ����R�s�[
	memcpy(drv->ram, ipl_endian+0x010000, 8);
	// CPU���Z�b�g
	X68CPU_Reset(drv->cpu);
}

// �N���b�N�؂�ւ�
void X68kDriver_SetCpuClock(EMUDRIVER* __drv, UINT32 clk_idx)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	if ( clk_idx < X68K_CLK_INDEX_MAX ) {
		SetCpuClock(drv, clk_idx);
		drv->cpu->freq = drv->cpu_clk;
		drv->t.update = TRUE;  // �^�C�~���O�p�����[�^�X�V
	}
}

// RAM�T�C�Y�ύX
void X68kDriver_SetMemorySize(EMUDRIVER* __drv, UINT32 sz_mb)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);

	if ( sz_mb < 2 ) sz_mb = 2;
	if ( sz_mb > 12 ) sz_mb = 12;

	drv->ram_size = sz_mb << 20;
	SetupRamMap(drv);
}

// SRAM�擾
UINT8* X68kDriver_GetSramPtr(EMUDRIVER* __drv, UINT32* p_sz)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER) NULL;
	if ( p_sz ) *p_sz = sizeof(drv->sram);
	return drv->sram;
}

// �������݂̋N�������t���b�s�[�f�B�X�N���C�W�F�N�g�����یĂ΂��R�[���o�b�N��o�^
void X68kDriver_SetEjectCallback(EMUDRIVER* __drv, DISKEJECTCB cb, void* cbprm)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	X68FDD_SetEjectCallback(drv->fdd, cb, cbprm);
}

// �t���b�s�[�f�B�X�N�̃h���C�u�ւ̑}��
void X68kDriver_SetDisk(EMUDRIVER* __drv, UINT32 drive, const UINT8* image, UINT32 image_sz, X68K_DISK_TYPE type, BOOL wr_protect)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	// ��ɃC�W�F�N�g�𔭍s
	X68FDD_EjectDisk(drv->fdd, drive, TRUE);  // XXX �����r�o
	// XXX �C�W�F�N�g���}�����s���Ɗ��荞�݃G���[�ɂȂ�\��������iFDD���Ɋ��荞�ݒx��������̂��ǂ��H�j
	//     �������̓h���C�o���ő}����x�点��H�i�C���[�W�̊Ǘ����̊֌W��A���荞�ݒx���������I�j
	X68FDD_SetDisk(drv->fdd, drive, image, image_sz, type);
	X68FDD_SetWriteProtect(drv->fdd, drive, wr_protect);
}

// �t���b�s�[�f�B�X�N�̃h���C�u����̎��o��
void X68kDriver_EjectDisk(EMUDRIVER* __drv, UINT32 drive, BOOL force)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	X68FDD_EjectDisk(drv->fdd, drive, force);
}

// �}�����̃t���b�s�[�f�B�X�N�̃C���[�W�f�[�^���擾�i�f�[�^���������܂ꂽ�f�B�X�N��ۑ��������ꍇ�ȂǗp�j
// XXX ���s�ł̓C�W�F�N�g���i�A�v���I�����܂ށj�ɃR�[���o�b�N�����ł���̂ŁA����I�Ɏ��ɍs���K�v�͂Ȃ��͂�
const UINT8* X68kDriver_GetDiskImage(EMUDRIVER* __drv, UINT32 drive, UINT32* p_imagesz)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER) NULL;
	return X68FDD_GetDiskImage(drv->fdd, drive, p_imagesz);
}

// �t���b�s�[�h���C�u��LED���\���̂ւ̃|�C���^�𓾂�i��ʑw��FDD�A�N�Z�X�\���Ȃǂ������������ꍇ�Ɏg���j
const INFO_X68FDD_LED* X68kDriver_GetDriveLED(EMUDRIVER* __drv)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER) NULL;
	return X68FDD_GetInfoLED(drv->fdd);
}

// �t���b�s�[�f�B�X�N�A�N�Z�X�̍������ݒ�
void X68kDriver_SetFastFddAccess(EMUDRIVER* __drv, BOOL fast)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	drv->fast_fdd = fast;
}

// HDD�h���C�u��LED���𓾂�
X68FDD_LED_STATE X68kDriver_GetHddLED(EMUDRIVER* __drv)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER) X68FDD_LED_OFF;
	return X68SASI_GetLedState(drv->sasi);
}

// �L�[���́i���j
void X68kDriver_KeyInput(EMUDRIVER* __drv, UINT32 key)
{
	// XXX �{���̓L�[�{�[�h�f�o�C�X��ʓr�p�ӂ��āA�L�[���s�[�g�Ȃǂ̊Ǘ���������ōs���ׂ�
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	UINT32 sw = ( ( key >> 7 ) & 1 ) ^ 1;  // Press/Release
	UINT32 i = ( key >> 5 ) & 3;
	UINT32 bit = key & 31;
	drv->keybuf.key_req[i] &= ~(1 << bit);
	drv->keybuf.key_req[i] |= sw << bit;
	if ( drv->keybuf.key_req[i] != drv->keybuf.key_state[i] ) {
		if ( Timer_GetPeriod(drv->tm_keybuf) == TIMERPERIOD_NEVER ) {
			Timer_ChangePeriod(_MAINTIMER_, drv->tm_keybuf, TIMERPERIOD_HZ(240));  // 2400bps�Ȃ̂ł��ꂭ�炢�H
		}
	}
}
void X68kDriver_KeyClear(EMUDRIVER* __drv)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	memset(drv->keybuf.key_req, 0, sizeof(drv->keybuf.key_req));
	if ( Timer_GetPeriod(drv->tm_keybuf) == TIMERPERIOD_NEVER ) {
		Timer_ChangePeriod(_MAINTIMER_, drv->tm_keybuf, TIMERPERIOD_HZ(240));  // 2400bps�Ȃ̂ł��ꂭ�炢�H
	}
}

// �W���C�p�b�h����
void X68kDriver_JoyInput(EMUDRIVER* __drv, UINT32 joy1, UINT32 joy2)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	drv->joyport[0] = joy1;
	drv->joyport[1] = joy2;
}

// �}�E�X����
void X68kDriver_MouseInput(EMUDRIVER* __drv, SINT32 dx, SINT32 dy, UINT32 btn)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	drv->mouse.x += dx;
	drv->mouse.y += dy;
	drv->mouse.stat = btn;
	// �K���Ƀ��~�b�g���Ă����i�����܂ł͍s���Ȃ��Ǝv�����j
	drv->mouse.x = NUMLIMIT(drv->mouse.x, -65535, 65535);
	drv->mouse.y = NUMLIMIT(drv->mouse.y, -65535, 65535);
}

// CRTC�^�C�~���O�擾
BOOL X68kDriver_GetDrawInfo(EMUDRIVER* __drv, ST_DISPAREA* area)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER) FALSE;
	if ( area ) {
		area->scrn.x1 = drv->t.scrn_area.x1;
		area->scrn.x2 = drv->t.scrn_area.x2;
		area->scrn.y1 = (drv->t.scrn_area.y1                 <<1) >> drv->t.line_shift;
		area->scrn.y2 = (drv->t.scrn_area.y2                 <<1) >> drv->t.line_shift;
		area->disp.x1 = (SINT32)drv->t.h_ev_pos[H_EV_START];
		area->disp.x2 = (SINT32)drv->t.h_ev_pos[H_EV_END  ];
		area->disp.y1 = ((SINT32)drv->t.v_ev_pos[V_EV_START] <<1) >> drv->t.line_shift;
		area->disp.y2 = ((SINT32)drv->t.v_ev_pos[V_EV_END  ] <<1) >> drv->t.line_shift;
	}
	return !drv->t.drawskip;
}

float X68kDriver_GetHSyncFreq(EMUDRIVER* __drv)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER) 0.0f;
	return drv->t.hsync_hz;
}

float X68kDriver_GetVSyncFreq(EMUDRIVER* __drv)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER) 0.0f;
	return drv->t.vsync_hz;
}

void X68kDriver_SetVolume(EMUDRIVER* __drv, X68K_SOUND_DEVICE device, float db)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	float amp = powf(10.0f, db/20.0f);
	switch ( device )
	{
	case X68K_SOUND_OPM:
		X68OPM_SetVolume(drv->opm, VOLUME_OPM*amp);
		break;
	case X68K_SOUND_ADPCM:
		X68ADPCM_SetMasterVolume(drv->adpcm, VOLUME_ADPCM*amp);
		break;
	}
}

void X68kDriver_SetFilter(EMUDRIVER* __drv, X68K_SOUND_DEVICE device, UINT32 filter_idx)
{
	// XXX �t�B���^�l���w��ł���I/F�̕������R�x�͍������A�����܂ŕK�v���ˁc�H
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	static const float X68OPM_FILTER[][2]   = { { 0.0f, 0.0f }, { 16000/*Hz*/, 0.6f/*Q*/ } };
	static const float ADPCM_FILTER[][2] = { { 0.0f, 0.0f }, {  3700/*Hz*/, 0.7f/*Q*/ }, { 16000/*Hz*/, 0.7f/*Q*/ }  };  // HQ�͂قڃt�������W�o�āA���܂�Ԃ��m�C�Y���i���ӂ�ɐݒ�

	switch ( device )
	{
	case X68K_SOUND_OPM:
		if ( filter_idx >= 2 ) {
			filter_idx = 1;
		}
		SndStream_RemoveFilter(drv->drv.sound, drv->lpf_opm, drv->opm);
		if ( filter_idx != 0 ) {
			SndFilter_SetPrmLowPass(drv->lpf_opm, X68OPM_FILTER[filter_idx][0], X68OPM_FILTER[filter_idx][1]);
			SndStream_AddFilter(drv->drv.sound, drv->lpf_opm, drv->opm);
		}
		break;
	case X68K_SOUND_ADPCM:
		if ( filter_idx >= 3 ) {
			filter_idx = 1;
		}
		SndStream_RemoveFilter(drv->drv.sound, drv->lpf_adpcm, drv->adpcm);
		if ( filter_idx != 0 ) {
			SndFilter_SetPrmLowPass(drv->lpf_adpcm, ADPCM_FILTER[filter_idx][0], ADPCM_FILTER[filter_idx][1]);
			SndStream_AddFilter(drv->drv.sound, drv->lpf_adpcm, drv->adpcm);
		}
		break;
	}
}

// MIDI���M�R�[���o�b�N�o�^
void X68kDriver_SetMidiCallback(EMUDRIVER* __drv, MIDIFUNCCB func, void* cbprm)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	X68MIDI_SetCallback(drv->midi, func, cbprm);
}

// SASI�t�@�C���A�N�Z�X�R�[���o�b�N�o�^
void X68kDriver_SetSasiCallback(EMUDRIVER* __drv, SASIFUNCCB func, void* cbprm)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	X68SASI_SetCallback(drv->sasi, func, cbprm);
}
