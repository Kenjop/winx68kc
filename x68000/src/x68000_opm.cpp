/* -----------------------------------------------------------------------------------
  YAMAHA YM2151(OPM) Emulator Interface for fmgen
                                                      (c) 2007-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	fmgen�ڑ��p
	�^�C�}��fmgen�̂��̂ł͂Ȃ����O�ŏ������Ă���i�̂�CSM�g���Ă�^�C�g���ŕs��o�邩���j
	�C�x���g�^�C�}��g���Ă邯��1�Ɍ��炵�����iTODO�j
*/

#include "osconfig.h"
#include "x68000_opm.h"
#include "opm.h"
#include <math.h>

typedef struct {
	FM::OPM* fmgen;

	UINT32   baseclk;
	UINT32   outfreq;     // �o�̓T���v�����g��
	float    volume;      // 1.0 �� 100%

	TIMERHDL timer;       // Timer manager handler
	TIMER_ID timerid[2];  // Timer ID

	X68OPMINTCB intfunc;     // Interrupt callback
	void*    intprm;      // Interrupt callback param

	UINT32   reg;         // Access register
	UINT32   regw[0x100];

	UINT32   st;
	UINT32   regtc, intst;
	UINT32   rega, regb;

	void*    portprm;     // I/O�|�[�g�A�N�Z�X�p�����[�^
	X68OPM_CT_W portw;       // I/O���C�g�֐�
} INFO_OPM;


// --------------------------------------------------------------------------
//   �R�[���o�b�N�Q
// --------------------------------------------------------------------------
static void UpdateIrq(INFO_OPM* opm)
{
	UINT32 irq = ( (opm->regtc>>2) & opm->st ) & 3;
	if ( irq && !opm->intst ) {
		opm->intst = 1;
		if ( opm->intfunc ) opm->intfunc(opm->intprm, TRUE);
	} else if ( !irq && opm->intst ) {
		opm->intst = 0;
		if ( opm->intfunc ) opm->intfunc(opm->intprm, FALSE);
	}
}

static void SetTimerA(INFO_OPM* opm)
{
	if ( opm->regtc & 0x01 ) {
		TUNIT us = (TUNIT)((  64.0*(double)(1024-opm->rega)*1000000.0) / (double)opm->baseclk);  // usec
		Timer_ChangePeriod(opm->timer, opm->timerid[0], TIMERPERIOD_US(us));				
//LOG(("OPM : TimerA = %fms", (float)us/1000.0f));
	}
}

static void SetTimerB(INFO_OPM* opm)
{
	if ( opm->regtc & 0x02 ) {
		TUNIT us = (TUNIT)((1024.0*(double)( 256-opm->regb)*1000000.0) / (double)opm->baseclk);  // usec
		Timer_ChangePeriod(opm->timer, opm->timerid[1], TIMERPERIOD_US(us));				
//LOG(("OPM : TimerB = %fms", (float)us/1000.0f));
	}
}

static void SetTimerControl(INFO_OPM* opm, UINT32 data)
{
	UINT32 chg = opm->regtc ^ data;

	opm->regtc = data;

//LOG(("OPM : SetTC = $%02X", data));
	if ( chg & 0x03 ) {  // �ǂ��炩���ω�
		if ( chg & 0x01 ) {  // TimerA ON/OFF �ω�
			if ( data & 0x01 ) {
				// ON�ɂȂ���
				SetTimerA(opm);
			} else {
				// OFF�ɂȂ���
				Timer_ChangePeriod(opm->timer, opm->timerid[0], TIMERPERIOD_NEVER);				
			}
		}
		if ( chg & 0x02 ) {  // TimerB ON/OFF �ω�
			if ( data & 0x02 ) {
				// ON�ɂȂ���
				SetTimerB(opm);
			} else {
				// OFF�ɂȂ���
				Timer_ChangePeriod(opm->timer, opm->timerid[1], TIMERPERIOD_NEVER);				
			}
		}
	}

	opm->st &= (~data>>4) & 3;
	UpdateIrq(opm);
}

// �^�C�}�[�I�[�o�[�t���[
static TIMER_HANDLER(X68OPM_TimerCB)
{
	INFO_OPM* opm = (INFO_OPM*)prm;
	opm->st |= 1 << opt;
	UpdateIrq(opm);
}

// �X�g���[���o�b�t�@����̃f�[�^�v��
STREAM_HANDLER(X68OPM_StreamCB)
{
	INFO_OPM* opm = (INFO_OPM*)prm;
	opm->fmgen->Mix((FM::Sample*)buf, len);
}


// --------------------------------------------------------------------------
//   ���J�֐�
// --------------------------------------------------------------------------
// ������
X68OPMHDL X68OPM_Init(TIMERHDL timer, STREAMHDL strm, UINT32 baseclk, float volume)
{
	INFO_OPM* opm;

	opm = (INFO_OPM*)_MALLOC(sizeof(INFO_OPM), "OPM struct");
	do {
		if ( !opm ) break;
		memset(opm, 0, sizeof(INFO_OPM));
		opm->outfreq = SndStream_GetFreq(strm);
		if ( !opm->outfreq ) break;
		opm->baseclk = baseclk;

		opm->fmgen = new FM::OPM();
		if ( !opm->fmgen ) break;
		if ( !opm->fmgen->Init(baseclk, opm->outfreq) ) break;

		opm->timer   = timer;
		opm->timerid[0] = Timer_CreateItem(opm->timer, TIMER_NORMAL, TIMERPERIOD_NEVER, &X68OPM_TimerCB, (void*)opm, 0, MAKESTATEID('O','T','A','0'));
		opm->timerid[1] = Timer_CreateItem(opm->timer, TIMER_NORMAL, TIMERPERIOD_NEVER, &X68OPM_TimerCB, (void*)opm, 1, MAKESTATEID('O','T','B','0'));
		if ( !opm->timerid[0] ) break;
		if ( !opm->timerid[1] ) break;

		X68OPM_SetVolume((X68OPMHDL)opm, volume);
		X68OPM_Reset((X68OPMHDL)opm);

		if ( !SndStream_AddChannel(strm, &X68OPM_StreamCB, (void*)opm) ) break;

		LOG(("OPM (%dHz, Vol=%.2f%%) initialized.", baseclk, volume*100.0f));
		return (X68OPMHDL)opm;
	} while ( 0 );

	X68OPM_Cleanup((X68OPMHDL)opm);
	return NULL;
}

// �j��
void X68OPM_Cleanup(X68OPMHDL handle)
{
	INFO_OPM* opm = (INFO_OPM*)handle;
	if ( opm ) {
		if ( opm->fmgen ) delete opm->fmgen;
		_MFREE(opm);
	}
}

// �`�b�v���Z�b�g
void X68OPM_Reset(X68OPMHDL handle)
{
	INFO_OPM* opm = (INFO_OPM*)handle;
	if ( opm ) {
		memset(opm->regw, 0, sizeof(opm->regw));
		opm->fmgen->Reset();
		opm->st = 0;
		opm->regtc = 0;
		opm->intst = 0;
		opm->rega = 0;
		opm->regb = 0;
		opm->reg = 0;
		Timer_ChangePeriod(opm->timer, opm->timerid[0], TIMERPERIOD_NEVER);
		Timer_ChangePeriod(opm->timer, opm->timerid[1], TIMERPERIOD_NEVER);
	}
}

// I/O Write
MEM16W_HANDLER(X68OPM_Write)
{
	INFO_OPM* opm = (INFO_OPM*)prm;
	if ( opm && ( adr & 1 ) ) {
		// X68000�ł� $E90001/$E90003�i4�o�C�g���[�v�j�̂݌q�����Ă���
		switch ( ( adr >> 1 ) & 1 )
		{
		default:
		case 0:
			opm->reg = data;
			break;

		case 1:
			Timer_UpdateStream(opm->timer);
			opm->fmgen->SetReg(opm->reg, data);
			opm->regw[opm->reg] = 0x100 | data;

			// �^�C�}�֘A�͎��O�ł��
			switch ( opm->reg )
			{
				case 0x10:  // TimerA High 8bit
					opm->rega = ( data << 2 ) | ( opm->rega & 0x03 );
					SetTimerA(opm);
					break;
				case 0x11:  // TimerA Low  2bit
					opm->rega = ( data & 0x03 ) | ( opm->rega & ~0x03 );
					SetTimerA(opm);
					break;
				case 0x12:  // TimerB
					opm->regb = data;
					SetTimerB(opm);
					break;
				case 0x14:  // Timer Control
					SetTimerControl(opm, data);
					break;
				case 0x1B:
					if ( opm->portw ) opm->portw(opm->portprm, (data>>6)&3);  // CT1/CT2
					break;
				default:
					break;
			}
			break;
		}
	}
}

// I/O Read
MEM16R_HANDLER(X68OPM_Read)
{
	INFO_OPM* opm = (INFO_OPM*)prm;
	if ( opm && ( adr & 1 ) ) {
		// X68000�ł� $E90001/$E90003�i4�o�C�g���[�v�j�̂݌q�����Ă���
		return opm->st;
	}
	return 0xFF;
}

// ���荞�݃R�[���o�b�N�̓o�^
void X68OPM_SetIntCallback(X68OPMHDL handle, X68OPMINTCB cb, void* cbprm)
{
	INFO_OPM* opm = (INFO_OPM*)handle;
	if ( opm ) {
		opm->intfunc = cb;
		opm->intprm  = cbprm;
	}
}

// �}�X�^�[�{�����[��
void X68OPM_SetVolume(X68OPMHDL handle, float volume)
{
	INFO_OPM* opm = (INFO_OPM*)handle;
	if ( opm ) {
		float db = 20.0f * log10f(volume);
		opm->volume = volume;
		opm->fmgen->SetVolume(db*2);  // fmgen�̃{�����[����1=0.5db
	}
}

// I/O�A�N�Z�X�֐��ݒ�
void X68OPM_SetPort(X68OPMHDL handle, X68OPM_CT_W wr, void* prm)
{
	INFO_OPM* opm = (INFO_OPM*)handle;
	if ( opm ) {
		opm->portprm = prm;
		opm->portw   = wr;
	}
}

void X68OPM_LoadState(X68OPMHDL hdl, STATE* state, UINT32 id)
{
	INFO_OPM* opm = (INFO_OPM*)hdl;
	if ( opm ) {
		UINT32 i;
		ReadState(state, id, MAKESTATEID('R','E','G','N'), &opm->reg,     sizeof(opm->reg));
		ReadState(state, id, MAKESTATEID('R','E','G','W'), opm->regw,     sizeof(opm->regw));
		ReadState(state, id, MAKESTATEID('S','T','A','T'), &opm->st,      sizeof(opm->st));
		ReadState(state, id, MAKESTATEID('R','G','T','C'), &opm->regtc,   sizeof(opm->regtc));
		ReadState(state, id, MAKESTATEID('I','N','T','S'), &opm->intst,   sizeof(opm->intst));
		ReadState(state, id, MAKESTATEID('R','E','G','A'), &opm->rega,    sizeof(opm->rega));
		ReadState(state, id, MAKESTATEID('R','E','G','B'), &opm->regb,    sizeof(opm->regb));
		opm->fmgen->Reset();
		// �Ԃ����Ⴏ���S�ɂ͕��A�ł��Ȃ����A�����ƕ��A������Ȃ�fmgen���Ɏ�����Ȃ���Ȃ�Ȃ��̂Łc
		for (i=0; i<0x100; i++) if ( opm->regw[i] & 0x100 ) { opm->fmgen->SetReg(i, opm->regw[i] & 0xFF); }
	}
}
void X68OPM_SaveState(X68OPMHDL hdl, STATE* state, UINT32 id)
{
	INFO_OPM* opm = (INFO_OPM*)hdl;
	if ( opm ) {
		WriteState(state, id, MAKESTATEID('R','E','G','N'), &opm->reg,     sizeof(opm->reg));
		WriteState(state, id, MAKESTATEID('R','E','G','W'), opm->regw,     sizeof(opm->regw));
		WriteState(state, id, MAKESTATEID('S','T','A','T'), &opm->st,      sizeof(opm->st));
		WriteState(state, id, MAKESTATEID('R','G','T','C'), &opm->regtc,   sizeof(opm->regtc));
		WriteState(state, id, MAKESTATEID('I','N','T','S'), &opm->intst,   sizeof(opm->intst));
		WriteState(state, id, MAKESTATEID('R','E','G','A'), &opm->rega,    sizeof(opm->rega));
		WriteState(state, id, MAKESTATEID('R','E','G','B'), &opm->regb,    sizeof(opm->regb));
	}
}
