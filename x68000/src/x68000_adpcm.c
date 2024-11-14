/* -----------------------------------------------------------------------------------
  OKI MSM6258 ADPCM Emulator
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	- ��Ԃ͉��d���ςŌŒ�
	- MSM5205�Ɠ�����3bit�T���v�����[�h��������ۂ����AX68000�ł͎g���ĂȂ��̂Ŗ�����
*/

#include "osconfig.h"
#include "x68000_adpcm.h"
#include <math.h>

// �ő� 15.625kHz / 60Hz �ŁA1�t�������� 260 �T���v�����炢����Α����
#define DECODED_BUFFER_SZ  512

typedef struct {
	TIMERHDL timer;
	X68ADPCM_SAMPLE_REQ_CB cbfunc;
	void*    cbprm;
	UINT32   outfreq;    // �o�̓T���v�����g��
	float    volume;
	TIMER_ID tm_request;
	// �����ȉ����X�e�[�g�ۑ�
	UINT32   baseclk;    // �x�[�X�N���b�N
	UINT32   prescaler;  // �v���X�P�[���i0:1/1024 1:1/768 2:1/512 3:����`�j
	float    strmcnt;    // �X�g���[�����A�b�v�f�[�g�p�J�E���^
	float    strmadd;    // �X�g���[�����A�b�v�f�[�g�p�J�E���^���Z�l
	float    req_hz;     // �f�[�^�X�V���N�G�X�g���g���i�T���v�����g��/2�j
	UINT32   nibble;     // ���̓|�[�g�f�[�^
	UINT32   next_nib;   // ���ɎQ�Ƃ���nibble�̃r�b�g�ʒu�i0:Lower 1:Upper 2:Invalid�^�v�����[�h�p�j
	SINT32   data;       // ���݂�ADPCM�f�R�[�_�o�̓f�[�^
	SINT32   step;       // ���݂�ADPCM�f�R�[�_�X�e�b�v
	float    vol_l;      // �{�����[��
	float    vol_r;      //
	UINT32   stat;       // �X�e�[�^�X
	SINT32   out;        // �Ō�ɃT�E���h�X�g���[���ɕԂ����f�[�^�i�o�͉��j
	// �ȉ��̓^�C�}���[�h�p�i����̓^�C�}���[�h�����Ȃ��Ȃ������j
	UINT32   wr_pos;     // �����O�o�b�t�@�������݃|�C���g
	UINT32   rd_pos;     // �����O�o�b�t�@�ǂݏo���|�C���g
	SINT16   decoded[DECODED_BUFFER_SZ];  // ADPCM�f�R�[�h�ς݃����O�o�b�t�@
} INFO_MSM6258;


static BOOL bTableInit = FALSE;
static SINT32 DIFF_TABLE[49*16];


// --------------------------------------------------------------------------
//   �����֐�
// --------------------------------------------------------------------------
static void AdpcmTableInit(void)
{
	// ADPCM�����e�[�u���̏�����
	static const SINT32 table[16][4] = {
		{  1, 0, 0, 0 }, {  1, 0, 0, 1 }, {  1, 0, 1, 0 }, {  1, 0, 1, 1 },
		{  1, 1, 0, 0 }, {  1, 1, 0, 1 }, {  1, 1, 1, 0 }, {  1, 1, 1, 1 },
		{ -1, 0, 0, 0 }, { -1, 0, 0, 1 }, { -1, 0, 1, 0 }, { -1, 0, 1, 1 },
		{ -1, 1, 0, 0 }, { -1, 1, 0, 1 }, { -1, 1, 1, 0 }, { -1, 1, 1, 1 }
	};
	SINT32 step, bit;

	if ( bTableInit ) return;

	for (step=0; step<=48; step++) {
		SINT32 diff = (SINT32)floor(16.0*pow(1.1, (double)step));
		for (bit=0; bit<16; bit++) {
			DIFF_TABLE[step*16+bit] = table[bit][0] *  // �^�����ŕω������̂Ŏ��M�Ȃ����A�T�E���h�L���v�`�����u�Ř^����������ł�X68000�̈ʑ��͐��ɐ�
				(diff   * table[bit][1] +
				 diff/2 * table[bit][2] +
				 diff/4 * table[bit][3] +
				 diff/8);
		}
	}
}

static void AdpcmCalcStep(INFO_MSM6258* snd)
{
	// strmadd �́A�o��1�T���v���ɑ΂���f�o�C�X�����T���v���̐i�s��
	// X68000�Ŏ��@�m�F��������APrescaler=3 �� 1/512�i�A��2��������������H�j
	static const UINT32 DIVIDER[4] = { 1024, 768, 512, 512/*����`�ݒ�*/ };
	snd->strmadd = (float)snd->baseclk / (float)( snd->outfreq * DIVIDER[snd->prescaler] );
	snd->req_hz = ( (float)snd->baseclk / (float)DIVIDER[snd->prescaler] ) / 2.0f;  // 1���2�T���v����������̂�1/2
//LOG(("ADPCM : clock=%d, prescaler=%d, atrmadd=%f, rate=%fHz", snd->baseclk, DIVIDER[snd->prescaler], snd->strmadd, snd->req_hz*2));
}

static void DecodeAdpcm(INFO_MSM6258* snd, UINT32 nibble)
{
	static const SINT32 SHIFT_TABLE[16] = {
		-1, -1, -1, -1, 2, 4, 6, 8,
		-1, -1, -1, -1, 2, 4, 6, 8
	};
	// ADPCM �X�V
	SINT32 data, step;
#if 1
	// DC�I�t�Z�b�g���h���t�g���Ă����̂�������邽�߂̍׍H
	data = (DIFF_TABLE[(snd->step<<4)|nibble]<<8) + (snd->data*250);
	data >>= 8;
#else
	data = DIFF_TABLE[(snd->step<<4)|nibble] + snd->data;
#endif
	data = NUMLIMIT(data, -2048, 2047);
	step = snd->step + SHIFT_TABLE[nibble];
	snd->step = NUMLIMIT(step, 0, 48);
	snd->data = data;
}

static SINT32 AdpcmUpdate(INFO_MSM6258* snd)
{
	SINT32 ret = snd->out;
	// �^�C�}�[���[�h
	// �����O�o�b�t�@�Ɋ��ɐς�ł���f�R�[�h�ς݃f�[�^����o��
	if ( snd->rd_pos != snd->wr_pos ) {
		ret = snd->decoded[snd->rd_pos];
		snd->rd_pos = (snd->rd_pos+1) & (DECODED_BUFFER_SZ-1);
	}
	return ret;
}


// --------------------------------------------------------------------------
//   �R�[���o�b�N�Q
// --------------------------------------------------------------------------
static TIMER_HANDLER(AdpcmRequest)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)prm;
	if ( !snd->cbfunc(snd->cbprm) ) {
		// �f�[�^�s���̍ۂ͍Ō�̃o�C�g�f�[�^�����̂܂܍ė��p���ăo�b�t�@�𖄂߂�i�h���L�����j
		// ���@�m�F��������ADMA���~�܂��Ă���ꍇ�͍Ō�ɑ���ꂽ1�o�C�g���i2��nibble�����݂Ɂj�J��Ԃ��������Ă���
		if ( snd->rd_pos == snd->wr_pos ) {
			X68ADPCM_Write((X68ADPCM)snd, 1, snd->nibble);
		}
	}
}

static BOOL AdpcmDummyCb(void* prm)
{
	// �R�[���o�b�N�Ăяo������ null �`�F�b�N���Ȃ����߂̏����_�~�[
	(prm);
	return FALSE;
}

// �X�g���[���o�b�t�@����̃f�[�^�v��
static STREAM_HANDLER(X68ADPCM_StreamCB)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)prm;
	if ( ( snd->stat & 0x80 ) == 0 ) {  // �Đ���
		UINT32 n = len;
		SINT32* p = buf;
		// DPCM�o�͂̍ő�l�� +2047/-2048�A����� x16 �� 0x7FFF�`-0x8000 �ɂȂ�
		const float vl = snd->vol_l * snd->volume * 16.0f;
		const float vr = snd->vol_r * snd->volume * 16.0f;

		while ( n-- ) {
			// �O��̎c�蕪���d���l�����ĉ��Z
			float ow = 1.0f - snd->strmcnt;
			float out = (float)snd->out * ow;

			// �f�o�C�X�����T���v����1�ȏ�i��ł�����A�T���v�������X�V
			snd->strmcnt += snd->strmadd;
			while ( snd->strmcnt >= 1.0f ) {
				snd->strmcnt -= 1.0f;
				snd->out = AdpcmUpdate(snd);
				// ����1�T���v���ȏ゠��Ȃ�t���ŉ��Z�A�����Ȃ�[���������Z
				if ( snd->strmcnt>=1.0f ) {
					out += (float)snd->out;
					ow += 1.0f;
				} else {
					out += (float)snd->out * snd->strmcnt;
					ow += snd->strmcnt;
				}
			}
			// �d���Ŋ���
			out /= ow;

			// �o��
			*p += (SINT32)(out*vl); p++;
			*p += (SINT32)(out*vr); p++;
		}
	}
}


// --------------------------------------------------------------------------
//   ���J�֐�
// --------------------------------------------------------------------------
// ������
X68ADPCM X68ADPCM_Init(TIMERHDL timer, STREAMHDL strm, UINT32 baseclk, float volume)
{
	INFO_MSM6258* snd;

	AdpcmTableInit();
	snd = (INFO_MSM6258*)_MALLOC(sizeof(INFO_MSM6258), "X68ADPCM struct");
	do {
		if ( !snd ) break;
		memset(snd, 0, sizeof(INFO_MSM6258));
		snd->timer = timer;
		snd->baseclk = baseclk;
		snd->volume = volume;
		snd->outfreq = SndStream_GetFreq(strm);
		snd->cbfunc = &AdpcmDummyCb;
		snd->tm_request = Timer_CreateItem(snd->timer, TIMER_NORMAL, TIMERPERIOD_NEVER, &AdpcmRequest, (void*)snd, 0, MAKESTATEID('X','A','R','Q'));
		X68ADPCM_Reset((X68ADPCM)snd);
		if ( !SndStream_AddChannel(strm, &X68ADPCM_StreamCB, (void*)snd) ) break;
		LOG(("X68ADPCM : initialize OK (Vol=%.2f%%)", volume*100.0f));
		return (X68ADPCM)snd;
	} while ( 0 );

	X68ADPCM_Cleanup((X68ADPCM)snd);
	return NULL;
}

// �j��
void X68ADPCM_Cleanup(X68ADPCM handle)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)handle;
	if ( snd ) {
		_MFREE(snd);
	}
}

void X68ADPCM_Reset(X68ADPCM handle)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)handle;
	if ( snd ) {
		snd->prescaler = 0;
		snd->vol_l = 256;
		snd->vol_r = 256;
		snd->stat = 0xC0;
		snd->data = 0;
		snd->step = 0;
		snd->out = 0;
		snd->nibble = 0;
		snd->next_nib = 2;
		snd->wr_pos = 0;
		snd->rd_pos = 0;
		AdpcmCalcStep(snd);
		Timer_ChangePeriod(snd->timer, snd->tm_request, TIMERPERIOD_NEVER);
	}
}

MEM16R_HANDLER(X68ADPCM_Read)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)prm;
	UINT32 ret = 0;
	if ( snd && ( adr & 1 ) ) {
		// X68000�ł� $E92001/$E92003�i4�o�C�g���[�v�j�̂݌q�����Ă���
		switch ( ( adr >> 1 ) & 1 )
		{
		default:
		case 0:  // Command
			ret = snd->stat;
			break;
		case 1:  // Data
			ret = snd->nibble;
			break;
		}
	}
	return ret;
}

MEM16W_HANDLER(X68ADPCM_Write)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)prm;
	if ( snd && ( adr & 1 ) ) {
		// X68000�ł� $E92001/$E92003�i4�o�C�g���[�v�j�̂݌q�����Ă���
		Timer_UpdateStream(snd->timer);
		switch ( ( adr >> 1 ) & 1 )
		{
		default:
		case 0:  // Command
			if ( data & 0x01 ) {
				// ��~
				snd->stat |= 0x80;
				Timer_ChangePeriod(snd->timer, snd->tm_request, TIMERPERIOD_NEVER);
			} else if ( ( data & 0x02 ) && ( snd->stat & 0x80 ) ) {
				/*
					��~���Ɍ��肷��ƁA�h���L����OP�̏I�Ղ̃e�B���p�j�̉��ʂ���������
					�� ���肵�Ȃ���OVER TAKE��PCM�i���d��PCM�j�������
					   �h���L�����̕���ADPCM�f�[�^�s�����ɍŏI�f�[�^���ăf�R�[�h���邱�ƂŏC�������
				*/
				// �Đ�
				snd->stat &= ~0x80;
				snd->data = 0;  // �o�͍͂Đ��J�n�^�C�~���O�Ń��Z�b�g�����
				snd->step = 0;
				snd->out = 0;
				snd->wr_pos = 0;
				snd->rd_pos = 0;
				Timer_ChangePeriod(snd->timer, snd->tm_request, TIMERPERIOD_HZ(snd->req_hz));
			}
			break;

		case 1:  // Data
			snd->nibble = data;
			{
			UINT32 next_pos;
			DecodeAdpcm(snd, (data>>0)&15);
			next_pos = (snd->wr_pos+1) & (DECODED_BUFFER_SZ-1);
			if ( next_pos != snd->rd_pos ) {
				snd->decoded[snd->wr_pos] = (SINT16)snd->data;
				snd->wr_pos = next_pos;
			}
			DecodeAdpcm(snd, (data>>4)&15);
			next_pos = (snd->wr_pos+1) & (DECODED_BUFFER_SZ-1);
			if ( next_pos != snd->rd_pos ) {
				snd->decoded[snd->wr_pos] = (SINT16)snd->data;
				snd->wr_pos = next_pos;
			}
			}
			break;
		}
	}
}

BOOL X68ADPCM_IsDataReady(X68ADPCM handle)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)handle;
	if ( snd ) {
		// �Đ����A�����̃f�[�^�ɋ󂫂�����ꍇ��Ready
		UINT32 next_pos = (snd->wr_pos+1) & (DECODED_BUFFER_SZ-1);
		return ( !(snd->stat & 0x80) && (next_pos!=snd->rd_pos) ) ? TRUE : FALSE;
	}
	return FALSE;
}

void X68ADPCM_SetCallback(X68ADPCM handle, X68ADPCM_SAMPLE_REQ_CB cb, void* cbprm)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)handle;
	if ( snd ) {
		snd->cbfunc = ( cb ) ? cb : &AdpcmDummyCb;
		snd->cbprm  = cbprm;
	}
}

void X68ADPCM_SetBaseClock(X68ADPCM handle, UINT32 baseclk)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)handle;
	if ( snd ) {
		if ( snd->baseclk != baseclk ) {
			Timer_UpdateStream(snd->timer);
			snd->baseclk = baseclk;
			AdpcmCalcStep(snd);
			if ( (snd->stat & 0x80)==0 ) {  // �Đ���
				Timer_ChangePeriod(snd->timer, snd->tm_request, TIMERPERIOD_HZ(snd->req_hz));
			}
		}
	}
}

void X68ADPCM_SetPrescaler(X68ADPCM handle, UINT32 pres)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)handle;
	if ( snd && pres<4 ) {
		if ( ( pres < 4 ) && ( snd->prescaler != pres ) ) {
			Timer_UpdateStream(snd->timer);
			snd->prescaler = pres;
			AdpcmCalcStep(snd);
			if ( (snd->stat & 0x80)==0 ) {  // ���Đ���
				Timer_ChangePeriod(snd->timer, snd->tm_request, TIMERPERIOD_HZ(snd->req_hz));
			}
		}
	}
}

void X68ADPCM_SetChannelVolume(X68ADPCM handle, float vol_l, float vol_r)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)handle;
	if ( snd ) {
		vol_l = NUMLIMIT(vol_l, 0.0f, 1.0f);
		vol_r = NUMLIMIT(vol_r, 0.0f, 1.0f);
		if ( ( snd->vol_l != vol_l ) || ( snd->vol_r != vol_r ) ) {
			Timer_UpdateStream(snd->timer);
			snd->vol_l = vol_l;
			snd->vol_r = vol_r;
		}
	}
}

void X68ADPCM_SetMasterVolume(X68ADPCM handle, float volume)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)handle;
	if ( snd ) {
		if ( snd->volume != volume ) {
			Timer_UpdateStream(snd->timer);
			snd->volume = volume;
		}
	}
}


// �X�e�[�g���[�h/�Z�[�u
void X68ADPCM_LoadState(X68ADPCM handle, STATE* state, UINT32 id)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)handle;
	if ( snd && state ) {
		size_t sz = ((size_t)&(((INFO_MSM6258*)0)->baseclk));  // offsetof(INFO_MSM6258,baseclk)
		ReadState(state, id, MAKESTATEID('I','N','F','O'), &snd->baseclk, sizeof(INFO_MSM6258)-(UINT32)sz);
	}
}
void X68ADPCM_SaveState(X68ADPCM handle, STATE* state, UINT32 id)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)handle;
	if ( snd && state ) {
		size_t sz = ((size_t)&(((INFO_MSM6258*)0)->baseclk));  // offsetof(INFO_MSM6258,baseclk)
		WriteState(state, id, MAKESTATEID('I','N','F','O'), &snd->baseclk, sizeof(INFO_MSM6258)-(UINT32)sz);
	}
}
