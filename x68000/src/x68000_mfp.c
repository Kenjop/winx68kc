/* -----------------------------------------------------------------------------------
  "SHARP X68000" MFP
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "x68000_mfp.h"
#include "x68000_cpu.h"


typedef struct {
	CPUDEV*      cpu;
	UINT8        reg[0x30/2];
	UINT8        reload[4];
	float        timer[4];
	TIMERHDL     t;
	TIMER_ID     tid[4];
} INFO_MFP;


// --------------------------------------------------------------------------
//   ��`��
// --------------------------------------------------------------------------
#define MFP_GPIP    0
#define MFP_AER     1
#define MFP_DDR     2
#define MFP_IERA    3
#define MFP_IERB    4
#define MFP_IPRA    5
#define MFP_IPRB    6
#define MFP_ISRA    7
#define MFP_ISRB    8
#define MFP_IMRA    9
#define MFP_IMRB    10
#define MFP_VR      11
#define MFP_TACR    12
#define MFP_TBCR    13
#define MFP_TCDCR   14
#define MFP_TADR    15
#define MFP_TBDR    16
#define MFP_TCDR    17
#define MFP_TDDR    18
#define MFP_SCR     19
#define MFP_UCR     20
#define MFP_RSR     21
#define MFP_TSR     22
#define MFP_UDR     23


// --------------------------------------------------------------------------
//   �����֐�
// --------------------------------------------------------------------------
static void MFP_CheckInt(INFO_MFP* mfp)
{
	UINT32 ipr = READBEWORD(&mfp->reg[MFP_IPRA]);
	UINT32 isr = READBEWORD(&mfp->reg[MFP_ISRA]);
	UINT32 imr = READBEWORD(&mfp->reg[MFP_IMRA]);
	UINT32 res = ( ipr & imr ) & ~isr;
	if ( res ) {
		IRQ_Request(mfp->cpu, IRQLINE_LINE|6, 0);
	} else {
		// ���Ƃ����͑��łȂ��ƁAIPL���N�����ɃG���[�o���c
		IRQ_Clear(mfp->cpu, 6);
	}
}

static void MFP_SetInt(INFO_MFP* mfp, UINT32 n)
{
	UINT32 ier = READBEWORD(&mfp->reg[MFP_IERA]);
	if ( ier & (1<<n) ) {
		UINT32 ipr = READBEWORD(&mfp->reg[MFP_IPRA]) | (1<<n);
		WRITEBEWORD(&mfp->reg[MFP_IPRA], ipr);
		MFP_CheckInt(mfp);
	}
}

static void MFP_TimerA(INFO_MFP* mfp)
{
	// �^�C�}A���C�x���g�J�E���g���[�h�iVDisp�ŃJ�E���g�j�̎��̏���
	if ( (mfp->reg[MFP_TACR]&15)==8 ) {
		mfp->reg[MFP_TADR]--;
		if ( mfp->reg[MFP_TADR]==0 ) {
			mfp->reg[MFP_TADR] = mfp->reload[0];
			MFP_SetInt(mfp, 13);
		}
	}
}

static void MFP_InitReg(INFO_MFP* mfp)
{
	// MFP�����l
	static const UINT8 _MFP_DEFAULT_[] = {
		0x7B, 0x06, 0x00, 0x18, 0x3E, 0x00, 0x00, 0x00,
		0x00, 0x18, 0x3E, 0x40, 0x08, 0x01, 0x77, 0x01,
		0x0D, 0xC8, 0x14, 0x00, 0x88, 0x01, 0x81, 0x00
	};
	UINT32 i;
	for (i=0; i<sizeof(_MFP_DEFAULT_); i++) {
		mfp->reg[i] = _MFP_DEFAULT_[i];
	}
	mfp->timer[0] = 0.0f;
	mfp->timer[1] = 0.0f;
	mfp->timer[2] = 0.0f;
	mfp->timer[3] = 0.0f;
}

static TIMER_HANDLER(MFP_TimerCb)
{
	INFO_MFP* mfp = (INFO_MFP*)prm;
	const UINT32 ch = opt;
	const UINT32 IRQ[4] = { 13, 8, 5, 4 };

	mfp->reg[MFP_TADR+ch]--;
	if ( mfp->reg[MFP_TADR+ch]==0 ) {
		mfp->reg[MFP_TADR+ch] = mfp->reload[ch];
		MFP_SetInt(mfp, IRQ[ch]);
	}
}

static void MFP_UpdateTimerPrm(INFO_MFP* mfp, UINT32 ch)
{
	static const float TIMER_PRESCALER[8] = { 1.0f, 4.0f, 10.0f, 16.0f, 50.0f, 64.0f, 100.0f, 200.0f };
	const UINT32 cr = ( ch==3 ) ?  MFP_TCDCR : (MFP_TACR+ch);
	const UINT32 sft = ( ch==2 ) ?  4 : 0;
	const UINT32 pres = ( mfp->reg[cr] >> sft ) & 7;
	const BOOL add_cond = ( ch==0 ) ? (!(mfp->reg[MFP_TACR]&8)) : TRUE;
	TUNIT period = DBL2TUNIT(0.0);

	if ( (pres) && (add_cond) ) {
		float hz = 4000000.0f / TIMER_PRESCALER[pres];
		period = TIMERPERIOD_HZ(hz);
	}
	if ( period == 0.0 ) {
		Timer_ChangePeriod(mfp->t, mfp->tid[ch], TIMERPERIOD_NEVER);
	} else {
		Timer_ChangePeriod(mfp->t, mfp->tid[ch], period);
	}
}


// --------------------------------------------------------------------------
//   ���J�֐�
// --------------------------------------------------------------------------
X68MFP X68MFP_Init(CPUDEV* cpu, TIMERHDL t)
{
	INFO_MFP* mfp = NULL;
	do {
		UINT32 i;
		mfp = (INFO_MFP*)_MALLOC(sizeof(INFO_MFP), "MFP struct");
		if ( !mfp ) break;
		memset(mfp, 0, sizeof(INFO_MFP));
		mfp->cpu = cpu;
		mfp->t = t;
		for (i=0; i<4; i++) {
			mfp->tid[i] = Timer_CreateItem(mfp->t, TIMER_NORMAL, TIMERPERIOD_NEVER, &MFP_TimerCb, (void*)mfp, i, MAKESTATEID('M','T','M','0'+i));
		}
		X68MFP_Reset((X68MFP)mfp);
		LOG(("MFP : initialize OK"));
		return (X68MFP)mfp;
	} while ( 0 );

	X68MFP_Cleanup((X68MFP)mfp);
	return NULL;
}

void X68MFP_Cleanup(X68MFP hdl)
{
	INFO_MFP* mfp = (INFO_MFP*)hdl;
	if ( mfp ) {
		_MFREE(mfp);
	}
}

void X68MFP_Reset(X68MFP hdl)
{
	INFO_MFP* mfp = (INFO_MFP*)hdl;
	if ( mfp ) {
		UINT32 i;
		for (i=0; i<4; i++) {
			Timer_ChangePeriod(mfp->t, mfp->tid[i], TIMERPERIOD_NEVER);
		}
		MFP_InitReg(mfp);
		IRQ_Clear(mfp->cpu, 6);
	}
}

MEM16W_HANDLER(X68MFP_Write)
{
	INFO_MFP* mfp = (INFO_MFP*)prm;
	if ( adr & 1 ) {
		UINT32 reg = (adr&0x3F) >> 1;
		switch ( reg ) {
			case MFP_GPIP:  // �������ݕs�iDDR �� 1 �̃r�b�g���������߂邪�A��{�I�� DDR=0�j
				break;
			case MFP_IERA:
			case MFP_IERB:
				mfp->reg[reg  ] = data;
				mfp->reg[reg+2] &= data;  // �֎~���ꂽ���̂�IPRA/B�𗎂Ƃ�
				MFP_CheckInt(mfp);
				break;
			case MFP_IPRA:
			case MFP_IPRB:
			case MFP_ISRA:
			case MFP_ISRB:
				mfp->reg[reg] &= data;
				MFP_CheckInt(mfp);
				break;
			case MFP_IMRA:
			case MFP_IMRB:
				mfp->reg[reg] = data;
				MFP_CheckInt(mfp);
				break;
			case MFP_TADR:
			case MFP_TBDR:
			case MFP_TCDR:
			case MFP_TDDR:
				mfp->reg[reg] = data;
				mfp->reload[reg-MFP_TADR] = data;
				break;
			case MFP_TACR:
				mfp->reg[reg] = data;
				MFP_UpdateTimerPrm(mfp, 0);
				break;
			case MFP_TBCR:
				mfp->reg[reg] = data;
				MFP_UpdateTimerPrm(mfp, 1);
				break;
			case MFP_TCDCR:
				mfp->reg[reg] = data;
				MFP_UpdateTimerPrm(mfp, 2);
				MFP_UpdateTimerPrm(mfp, 3);
				break;
			case MFP_RSR:
				// RE�r�b�g�����ύX�ł��Ȃ�
				mfp->reg[reg] = ( mfp->reg[reg] & 0xFE ) | ( data & 0x01 );
				break;
			case MFP_TSR:
				// BE/UE/END�͒��ڕύX�s��
				mfp->reg[reg] = ( mfp->reg[reg] & 0xD0 ) | ( data & 0x2F );
				// TE=1�Ȃ�BE/END���N���A
				if ( data & 0x01 ) {
					mfp->reg[reg] &= ~0x50;
				}
				// XXX ���M�o�b�t�@�͏�ɋ󂫂ɂ��Ă���
				mfp->reg[reg] |= 0x80;
				break;
			case MFP_UDR:
				// XXX ���M�f�[�^�͌��󖳎�
				break;
			default:
				mfp->reg[reg] = data;
				break;
		}
	}

	// MFP�̃f�[�^�V�[�g�������A�������ݑ����ǂݍ��݂Ƃقړ������Ԋ|����
	X68CPU_ConsumeClock(mfp->cpu, (UINT32)((mfp->cpu->freq*2.5f)/4000000.0f));
}

MEM16R_HANDLER(X68MFP_Read)
{
	INFO_MFP* mfp = (INFO_MFP*)prm;
	UINT8 ret = 0xFF;
	if ( adr & 1 ) {
		UINT32 reg = (adr&0x3F) >> 1;
		switch ( reg ) {
			case MFP_GPIP:
				/*
					-------A  ALARM�ɂ��p���[�I���i0:�A���[���N���A1:�ʏ�N���j
					------E-  EXPON�ɂ��p���[�I���i0:�O���N���A1:�ʏ�N���j
					-----P--  �d���X�C�b�`��ԁi0:ON�A1:OFF�j
					----F---  FM�������荞�݁i0:�v�����A1:�Ȃ��j
					---V----  VDISP�i0:�A�����ԁA1:�\�����ԁj
					--1-----  ���1
					-C------  CRTC���荞�݁i0:�v�����A1:�Ȃ��j
					H-------  HSYNC�i0:�\�����ԁA1:��������
				*/
				ret = 0x01 | 0x02 | 0x20;  // ������1�ŌŒ�
				ret |= mfp->reg[MFP_GPIP] & 0xD8;
				break;
			case MFP_TSR:
				ret = mfp->reg[reg];
				mfp->reg[reg] &= ~0x40; // UE�̓��[�h�ŃN���A�����
				break;
			case MFP_UDR:
				// �L�[���̓f�[�^
				if ( mfp->reg[MFP_RSR] & 0x80 ) {
					ret = mfp->reg[reg];
					mfp->reg[MFP_RSR] &= ~0xC0;
				} else {
					ret = 0;
				}
				break;
			default:
				ret = mfp->reg[reg];
				break;
		}
	}

	// OVER TAKE��ZOOM���S��10MHz�Ő��퓮�삳����̂ɕK�v
	// MFP�̃f�[�^�V�[�g�ɂ��ƁA/CS����f�[�^���L���ɂȂ�܂ł��悻2.5(MFP)clk
	X68CPU_ConsumeClock(mfp->cpu, (UINT32)((mfp->cpu->freq*2.5f)/4000000.0f));

	return ret;
}

BOOL X68MFP_SetKeyData(X68MFP hdl, UINT8 key)
{
	INFO_MFP* mfp = (INFO_MFP*)hdl;
	// USART��M�o�b�t�@����Ȃ��t
	if ( !( mfp->reg[MFP_RSR] & 0x80 ) ) {
		mfp->reg[MFP_UDR] = key;
		mfp->reg[MFP_RSR] |= 0x80;
		MFP_SetInt(mfp, 12);  // ��M�o�b�t�@�t��
		return TRUE;
	}
	return FALSE;
}

void X68MFP_SetGPIP(X68MFP hdl, MFP_GPIP_BIT bit, UINT32 n)
{
	static const UINT32 GPIP_TO_INT[] = {
		0, 1, 2, 3, 6, 31, 14, 15   // 5bit�ڂ͖��g�pbit
	};

	INFO_MFP* mfp = (INFO_MFP*)hdl;
	const UINT32 mask = 1 << bit;
	const UINT32 aer = mfp->reg[MFP_AER];
	const UINT32 old_data = mfp->reg[MFP_GPIP] ^ aer;
	UINT32 new_data;

	mfp->reg[MFP_GPIP] = ( mfp->reg[MFP_GPIP] & ~mask ) | ( n << bit );
	new_data = mfp->reg[MFP_GPIP] ^ aer;

	// AER �������Ă���r�b�g�𔽓]�����Ă�̂ŁA��� 1 -> 0 �ω����Ɋ��荞�݂��N����Ƃ��Ĉ�����
	// ���jInside X68000 p.81�̐}�̐����͊ԈႢ�Bp.82 �̖{�����ɂ���悤�� AER=1 �� 0->1 �ω��Ŋ��荞��
	if ( ( (old_data^new_data) & old_data ) & mask ) {
		// ���荞�ݔ���
		MFP_SetInt(mfp, GPIP_TO_INT[bit]);
		if ( bit==GPIP_BIT_VDISP ) {
			MFP_TimerA(mfp);
		}
	}
}

int X68MFP_GetIntVector(X68MFP hdl)
{
	INFO_MFP* mfp = (INFO_MFP*)hdl;
	UINT32 ipr = READBEWORD(&mfp->reg[MFP_IPRA]);
	UINT32 isr = READBEWORD(&mfp->reg[MFP_ISRA]);
	UINT32 imr = READBEWORD(&mfp->reg[MFP_IMRA]);
	UINT32 res = ( ipr & imr ) & ~isr;
	int vect = -1;
	if ( res ) {
		UINT32 vr = mfp->reg[MFP_VR];
		int i;
		// �ł��D�揇�ʂ̍������荞�݂�T��
		for (i=15; i>=0; i--) {
			if ( res & 0x8000 ) break;
			res <<= 1;
		}
		// �x�N�^����
		vect = (vr&0xF0) | i;
		// IPR�̃r�b�g�𗎂Ƃ�
		ipr &= ~(1<<i);
		WRITEBEWORD(&mfp->reg[MFP_IPRA], ipr);
		// �\�t�g�E�F�AEOI�̏ꍇ�AISR�̃r�b�g�𗧂Ă�
		if ( vr & 0x08 ) {
			isr |= (1<<i);
			WRITEBEWORD(&mfp->reg[MFP_ISRA], isr);
		}
		// ���荞�ݏ�ԍĊm�F
		MFP_CheckInt(mfp);
	}
if ( vect < 0 ) LOG(("### MFP : return auto vector"));
	return vect;
}

void X68MFP_LoadState(X68MFP hdl, STATE* state, UINT32 id)
{
	INFO_MFP* mfp = (INFO_MFP*)hdl;
	if ( mfp && state ) {
		ReadState(state, id, MAKESTATEID('R','E','G','S'), mfp->reg,      sizeof(mfp->reg));
		ReadState(state, id, MAKESTATEID('R','E','L','D'), mfp->reload,   sizeof(mfp->reload));
		ReadState(state, id, MAKESTATEID('T','M','I','R'), mfp->timer,    sizeof(mfp->timer));
	}
}

void X68MFP_SaveState(X68MFP hdl, STATE* state, UINT32 id)
{
	INFO_MFP* mfp = (INFO_MFP*)hdl;
	if ( mfp && state ) {
		WriteState(state, id, MAKESTATEID('R','E','G','S'), mfp->reg,      sizeof(mfp->reg));
		WriteState(state, id, MAKESTATEID('R','E','L','D'), mfp->reload,   sizeof(mfp->reload));
		WriteState(state, id, MAKESTATEID('T','M','I','R'), mfp->timer,    sizeof(mfp->timer));
	}
}
