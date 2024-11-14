/* -----------------------------------------------------------------------------------
  OKI MSM6258 ADPCM Emulator
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	- 補間は加重平均で固定
	- MSM5205と同じく3bitサンプルモードがあるっぽいが、X68000では使ってないので未実装
*/

#include "osconfig.h"
#include "x68000_adpcm.h"
#include <math.h>

// 最大 15.625kHz / 60Hz で、1フレあたり 260 サンプルくらいあれば足りる
#define DECODED_BUFFER_SZ  512

typedef struct {
	TIMERHDL timer;
	X68ADPCM_SAMPLE_REQ_CB cbfunc;
	void*    cbprm;
	UINT32   outfreq;    // 出力サンプル周波数
	float    volume;
	TIMER_ID tm_request;
	// ここ以下をステート保存
	UINT32   baseclk;    // ベースクロック
	UINT32   prescaler;  // プリスケーラ（0:1/1024 1:1/768 2:1/512 3:未定義）
	float    strmcnt;    // ストリーム時アップデート用カウンタ
	float    strmadd;    // ストリーム時アップデート用カウンタ加算値
	float    req_hz;     // データ更新リクエスト周波数（サンプル周波数/2）
	UINT32   nibble;     // 入力ポートデータ
	UINT32   next_nib;   // 次に参照するnibbleのビット位置（0:Lower 1:Upper 2:Invalid／プルモード用）
	SINT32   data;       // 現在のADPCMデコーダ出力データ
	SINT32   step;       // 現在のADPCMデコーダステップ
	float    vol_l;      // ボリューム
	float    vol_r;      //
	UINT32   stat;       // ステータス
	SINT32   out;        // 最後にサウンドストリームに返したデータ（出力音）
	// 以下はタイマモード用（現状はタイマモードしかなくなったが）
	UINT32   wr_pos;     // リングバッファ書き込みポイント
	UINT32   rd_pos;     // リングバッファ読み出しポイント
	SINT16   decoded[DECODED_BUFFER_SZ];  // ADPCMデコード済みリングバッファ
} INFO_MSM6258;


static BOOL bTableInit = FALSE;
static SINT32 DIFF_TABLE[49*16];


// --------------------------------------------------------------------------
//   内部関数
// --------------------------------------------------------------------------
static void AdpcmTableInit(void)
{
	// ADPCM差分テーブルの初期化
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
			DIFF_TABLE[step*16+bit] = table[bit][0] *  // 録音環境で変化したので自信ないが、サウンドキャプチャ装置で録音した限りではX68000の位相は正極性
				(diff   * table[bit][1] +
				 diff/2 * table[bit][2] +
				 diff/4 * table[bit][3] +
				 diff/8);
		}
	}
}

static void AdpcmCalcStep(INFO_MSM6258* snd)
{
	// strmadd は、出力1サンプルに対するデバイス内部サンプルの進行量
	// X68000で実機確認した限り、Prescaler=3 は 1/512（但し2よりも音圧下がる？）
	static const UINT32 DIVIDER[4] = { 1024, 768, 512, 512/*未定義設定*/ };
	snd->strmadd = (float)snd->baseclk / (float)( snd->outfreq * DIVIDER[snd->prescaler] );
	snd->req_hz = ( (float)snd->baseclk / (float)DIVIDER[snd->prescaler] ) / 2.0f;  // 1回で2サンプル生成するので1/2
//LOG(("ADPCM : clock=%d, prescaler=%d, atrmadd=%f, rate=%fHz", snd->baseclk, DIVIDER[snd->prescaler], snd->strmadd, snd->req_hz*2));
}

static void DecodeAdpcm(INFO_MSM6258* snd, UINT32 nibble)
{
	static const SINT32 SHIFT_TABLE[16] = {
		-1, -1, -1, -1, 2, 4, 6, 8,
		-1, -1, -1, -1, 2, 4, 6, 8
	};
	// ADPCM 更新
	SINT32 data, step;
#if 1
	// DCオフセットがドリフトしていくのを回避するための細工
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
	// タイマーモード
	// リングバッファに既に積んであるデコード済みデータから出力
	if ( snd->rd_pos != snd->wr_pos ) {
		ret = snd->decoded[snd->rd_pos];
		snd->rd_pos = (snd->rd_pos+1) & (DECODED_BUFFER_SZ-1);
	}
	return ret;
}


// --------------------------------------------------------------------------
//   コールバック群
// --------------------------------------------------------------------------
static TIMER_HANDLER(AdpcmRequest)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)prm;
	if ( !snd->cbfunc(snd->cbprm) ) {
		// データ不足の際は最後のバイトデータをそのまま再利用してバッファを埋める（ドラキュラ）
		// 実機確認した限り、DMAが止まっている場合は最後に送られた1バイトを（2つのnibbleを交互に）繰り返し処理している
		if ( snd->rd_pos == snd->wr_pos ) {
			X68ADPCM_Write((X68ADPCM)snd, 1, snd->nibble);
		}
	}
}

static BOOL AdpcmDummyCb(void* prm)
{
	// コールバック呼び出し時の null チェックを省くための初期ダミー
	(prm);
	return FALSE;
}

// ストリームバッファからのデータ要求
static STREAM_HANDLER(X68ADPCM_StreamCB)
{
	INFO_MSM6258* snd = (INFO_MSM6258*)prm;
	if ( ( snd->stat & 0x80 ) == 0 ) {  // 再生中
		UINT32 n = len;
		SINT32* p = buf;
		// DPCM出力の最大値は +2047/-2048、よって x16 で 0x7FFF～-0x8000 になる
		const float vl = snd->vol_l * snd->volume * 16.0f;
		const float vr = snd->vol_r * snd->volume * 16.0f;

		while ( n-- ) {
			// 前回の残り分を重さ考慮して加算
			float ow = 1.0f - snd->strmcnt;
			float out = (float)snd->out * ow;

			// デバイス内部サンプルが1以上進んでいたら、サンプル数分更新
			snd->strmcnt += snd->strmadd;
			while ( snd->strmcnt >= 1.0f ) {
				snd->strmcnt -= 1.0f;
				snd->out = AdpcmUpdate(snd);
				// 次が1サンプル以上あるならフルで加算、無いなら端数分を加算
				if ( snd->strmcnt>=1.0f ) {
					out += (float)snd->out;
					ow += 1.0f;
				} else {
					out += (float)snd->out * snd->strmcnt;
					ow += snd->strmcnt;
				}
			}
			// 重さで割る
			out /= ow;

			// 出力
			*p += (SINT32)(out*vl); p++;
			*p += (SINT32)(out*vr); p++;
		}
	}
}


// --------------------------------------------------------------------------
//   公開関数
// --------------------------------------------------------------------------
// 初期化
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

// 破棄
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
		// X68000では $E92001/$E92003（4バイトループ）のみ繋がっている
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
		// X68000では $E92001/$E92003（4バイトループ）のみ繋がっている
		Timer_UpdateStream(snd->timer);
		switch ( ( adr >> 1 ) & 1 )
		{
		default:
		case 0:  // Command
			if ( data & 0x01 ) {
				// 停止
				snd->stat |= 0x80;
				Timer_ChangePeriod(snd->timer, snd->tm_request, TIMERPERIOD_NEVER);
			} else if ( ( data & 0x02 ) && ( snd->stat & 0x80 ) ) {
				/*
					停止中に限定すると、ドラキュラOPの終盤のティンパニの音量がおかしい
					→ 限定しないとOVER TAKEのPCM（多重化PCM）が崩れる
					   ドラキュラの方はADPCMデータ不足時に最終データを再デコードすることで修正される
				*/
				// 再生
				snd->stat &= ~0x80;
				snd->data = 0;  // 出力は再生開始タイミングでリセットされる
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
		// 再生中、かつ次のデータに空きがある場合にReady
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
			if ( (snd->stat & 0x80)==0 ) {  // 再生中
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
			if ( (snd->stat & 0x80)==0 ) {  // 且つ再生中
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


// ステートロード/セーブ
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
