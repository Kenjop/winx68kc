/* -----------------------------------------------------------------------------------
  Sound stream manager
                                                      (c) 2004-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "osconfig.h"
#include "sound_stream.h"

#include <math.h>

//#define SAVE_STREAM

#define MAX_SNDCHANNEL  8
#define MAX_FILTER      2
#define SNDBUF_LEN      0x8000   // 48kHzで680ms分くらい（2のべき乗のこと）

#define FILTER_OVER_SAMPLE  // フィルタ処理をオーバーサンプルする

typedef struct {
	float freq;
	float a1, a2;
	float b0, b1, b2;
	float in_l[2];
	float in_r[2];
	float out_l[2];
	float out_r[2];
} INFO_SNDFILTER;

typedef struct {
	UINT32     freq;
	UINT32     volume;                     // ボリューム（0～256）
	UINT32     chnum;                      // 現在の総CH数
	STREAMCHCB cbfunc[MAX_SNDCHANNEL];     // CH毎のPCM取得コールバックルーチン
	void*      cbprm[MAX_SNDCHANNEL];      // CH毎のコールバックパラメータ
	SNDFILTER  filter[MAX_SNDCHANNEL][MAX_FILTER];     // チャンネルに割り当てられたフィルタ
	UINT32     num_filter[MAX_SNDCHANNEL]; // チャンネルに割り当てられたフィルタ数
	SINT16     buffer[SNDBUF_LEN*2];       // 出力用リングバッファ
	SINT32     tmpbuf[SNDBUF_LEN*2];       // 合成用一時バッファ
	SINT32     fltbuf[SNDBUF_LEN*2];       // 合成用一時バッファ（フィルタ用）
	UINT32     wpos;                       // リングバッファ書き込み点
	UINT32     rpos;                       // リングバッファ読み込み点
	UINT32     bufcnt;                     // バッファ内のサンプル数
	MUTEX_DEFINE(mutex);
} INFO_SNDSTREAM;

#ifdef SAVE_STREAM
static FILE* fp = NULL;
#endif



// --------------------------------------------------------------------------
//   フィルタ関連（共通部）
// --------------------------------------------------------------------------
void SndStream_AddFilter(STREAMHDL hstrm, SNDFILTER filter, void* dev)
{
	INFO_SNDSTREAM* snd = (INFO_SNDSTREAM*)hstrm;
	if ( snd && filter && dev ) {
		UINT32 i;
		// 同一フィルタ多重登録チェック（同一フィルタを複数重ねたり、複数デバイスで使い回すことはできない）
		for (i=0; i<MAX_SNDCHANNEL; i++) {
			if ( snd->cbprm[i]!=NULL ) {
				UINT32 j;
				for (j=0; j<MAX_FILTER; j++) {
					if ( snd->filter[i][j]==filter ) {
						break;
					}
				}
				if ( j!=MAX_FILTER ) {
					LOG(("SndStream_AddFilter : ##### Filter $%p is already applied for dev=$%p (@%d)", filter, snd->cbprm[i], j+1));
					return;
				}
			}
		}
		for (i=0; i<MAX_SNDCHANNEL; i++) {
			if ( snd->cbprm[i]==dev ) {
				if ( snd->num_filter[i]<MAX_FILTER ) {
					snd->filter[i][snd->num_filter[i]] = filter;
					snd->num_filter[i]++;
//					LOG(("SndStream_AddFilter : Filter(%d) $%p added to dev=$%p", snd->num_filter[i], filter, dev));
				} else {
					LOG(("SndStream_AddFilter : ##### Filter count for dev=$%p overflow", dev));
				}
				return;
			}
		}
	}
	LOG(("### SndStream_AddFilter : invalid handle error. dev=$%p, filter=$%p", dev, filter));
}

void SndStream_RemoveFilter(STREAMHDL hstrm, SNDFILTER filter, void* dev)
{
	INFO_SNDSTREAM* snd = (INFO_SNDSTREAM*)hstrm;
	if ( snd && filter && dev ) {
		UINT32 i;
		for (i=0; i<MAX_SNDCHANNEL; i++) {
			if ( snd->cbprm[i]==dev ) {
				UINT32 j;
				for (j=0; j<MAX_FILTER; j++) {
					if ( snd->filter[i][j]==filter ) {
						UINT32 k;
						for (k=j; k<(MAX_FILTER-1); k++) {
							snd->filter[i][k] = snd->filter[i][k+1];
						}
//						LOG(("SndStream_RemoveFilter : Filter(%d) $%p removed from dev=$%p", snd->num_filter[i], filter, dev));
						snd->filter[i][MAX_FILTER-1] = NULL;
						snd->num_filter[i]--;
						return;  // 多重登録は禁止にしたので、同じものが複数登録されていることはありえないはず
					}
				}
			}
		}
	}
	LOG(("### SndStream_RemoveFilter : invalid handle error. dev=$%p, filter=$%p", dev, filter));
}

static INFO_SNDFILTER* CreateSndFilter(STREAMHDL _strm)
{
	INFO_SNDSTREAM* strm = (INFO_SNDSTREAM*)_strm;
	INFO_SNDFILTER* f = (INFO_SNDFILTER*)_MALLOC(sizeof(INFO_SNDFILTER), "Sound filter");
	do {
		if ( !f ) break;
		f->freq = (float)strm->freq;
		f->b0 = 1.0f;
		f->a1 = f->a2 = 0.0f;
		f->b1 = f->b2 = 0.0f;
		f->in_l[0] = f->in_l[1] = 0.0f;
		f->in_r[0] = f->in_r[1] = 0.0f;
		f->out_l[0] = f->out_l[1] = 0.0f;
		f->out_r[0] = f->out_r[1] = 0.0f;
		return f;
	} while ( 0 );
	return NULL;
}

void SndFilter_Destroy(SNDFILTER _filter)
{
	INFO_SNDFILTER* filter = (INFO_SNDFILTER*)_filter;
	if ( filter ) {
		_MFREE(filter);
	}
}

void SndFilter_LoadState(SNDFILTER _f, STATE* state, UINT32 id)
{
	INFO_SNDFILTER* f = (INFO_SNDFILTER*)_f;
	if ( f && state ) {
		ReadState(state, id, MAKESTATEID('P','R','M','S'), f, sizeof(INFO_SNDFILTER));
	}
}

void SndFilter_SaveState(SNDFILTER _f, STATE* state, UINT32 id)
{
	INFO_SNDFILTER* f = (INFO_SNDFILTER*)_f;
	if ( f && state ) {
		WriteState(state, id, MAKESTATEID('P','R','M','S'), f, sizeof(INFO_SNDFILTER));
	}
}

static void DoSndFilter(SNDFILTER _filter, SINT32* in, SINT32* out, UINT32 len, BOOL overwrite)
{
	INFO_SNDFILTER* f = (INFO_SNDFILTER*)_filter;
#ifdef FILTER_OVER_SAMPLE
	UINT32 _ODD = 0;   // 1サンプルで 0->1(->0) と変化するので、ローカルで（構造体で保持しなくて）よい
#endif
	// 双二次フィルタ計算
	if ( overwrite ) {
		// 入力バッファ上書き（複数フィルタの最終段以外）
		SINT32* o = in;
		while ( len ) {
			// 入力取得
			float in_l = (float)(*in++);
			float in_r = (float)(*in++);

			// 出力計算
			float out_l = f->b0*in_l + f->b1*f->in_l[0]  + f->b2*f->in_l[1]
									 - f->a1*f->out_l[0] - f->a2*f->out_l[1];
			float out_r = f->b0*in_r + f->b1*f->in_r[0]  + f->b2*f->in_r[1]
									 - f->a1*f->out_r[0] - f->a2*f->out_r[1];

			// 次の計算のためにバッファリング
			f->in_l[1] = f->in_l[0];
			f->in_l[0] = in_l;
			f->in_r[1] = f->in_r[0];
			f->in_r[0] = in_r;
			f->out_l[1] = f->out_l[0];
			f->out_l[0] = out_l;
			f->out_r[1] = f->out_r[0];
			f->out_r[0] = out_r;

			// 出力
#ifdef FILTER_OVER_SAMPLE
			if ( !_ODD ) {
				in -= 2;
			} else {
#endif
				*o++ = (SINT32)out_l;  // 入力を上書き
				*o++ = (SINT32)out_r;
				len--;
#ifdef FILTER_OVER_SAMPLE
			}
			_ODD ^= 1;
#endif
		}
	} else {
		// 出力バッファに加算（複数フィルタの最終段）
		SINT32* o = out;
		while ( len ) {
			// 入力取得
			float in_l = (float)(*in++);
			float in_r = (float)(*in++);

			// 出力計算
			float out_l = f->b0*in_l + f->b1*f->in_l[0]  + f->b2*f->in_l[1]
									 - f->a1*f->out_l[0] - f->a2*f->out_l[1];
			float out_r = f->b0*in_r + f->b1*f->in_r[0]  + f->b2*f->in_r[1]
									 - f->a1*f->out_r[0] - f->a2*f->out_r[1];

			// 次の計算のためにバッファリング
			f->in_l[1] = f->in_l[0];
			f->in_l[0] = in_l;
			f->in_r[1] = f->in_r[0];
			f->in_r[0] = in_r;
			f->out_l[1] = f->out_l[0];
			f->out_l[0] = out_l;
			f->out_r[1] = f->out_r[0];
			f->out_r[0] = out_r;

			// 出力
#ifdef FILTER_OVER_SAMPLE
			if ( !_ODD ) {
				in -= 2;
			} else {
#endif
				*o++ += (SINT32)out_l;  // SndStream_TimerCB() 内で in に出力するため、加算出力のこと
				*o++ += (SINT32)out_r;
				len--;
#ifdef FILTER_OVER_SAMPLE
			}
			_ODD ^= 1;
#endif
		}
	}
}


// --------------------------------------------------------------------------
//   フィルタ関連（フィルタ個別）
// --------------------------------------------------------------------------
SNDFILTER SndFilter_Create(STREAMHDL _strm)
{
	INFO_SNDSTREAM* strm = (INFO_SNDSTREAM*)_strm;
	INFO_SNDFILTER* f = CreateSndFilter(_strm);
	do {
		if ( !f ) break;
		return (SNDFILTER)f;
	} while ( 0 );

	SndFilter_Destroy((SNDFILTER)f);

	return NULL;
}

// -------------------------------
// 各フィルタの係数計算
// -------------------------------
#ifdef FILTER_OVER_SAMPLE
#define FILTER_FREQ_MULTI  2
#else
#define FILTER_FREQ_MULTI  1
#endif

#define PREPARE_FILTER_PARAM \
  float omega = (2.0f * 3.14159265358979f * cutoff) / (f->freq*FILTER_FREQ_MULTI); \
  float alpha = sinf(omega) / (2.0f*q);                        \
  float a0, a1, a2, b0, b1, b2;

#define PREPARE_FILTER_PARAM_BAND \
  float omega = (2.0f * 3.14159265358979f * cutoff) / (f->freq*FILTER_FREQ_MULTI); \
  float alpha = sinf(omega) * sinhf(logf(2.0f) / 2.0f * bw * omega / sinf(omega)); \
  float a0, a1, a2, b0, b1, b2;

#define SET_FILTER_PARAM(f) \
  (f)->a1 = a1 / a0; \
  (f)->a2 = a2 / a0; \
  (f)->b0 = b0 / a0; \
  (f)->b1 = b1 / a0; \
  (f)->b2 = b2 / a0; \


void SndFilter_ClearPrm(SNDFILTER _f)
{
	INFO_SNDFILTER* f = (INFO_SNDFILTER*)_f;
	if ( f ) {
		f->b0 = 1.0f;
		f->a1 = f->a2 = 0.0f;
		f->b1 = f->b2 = 0.0f;
	}
}

void SndFilter_SetPrmLowPass(SNDFILTER _f, float cutoff, float q)
{
	INFO_SNDFILTER* f = (INFO_SNDFILTER*)_f;
	if ( f ) {
		PREPARE_FILTER_PARAM

		// 双二次フィルタ用のLPF係数を計算する
		a0 =  1.0f + alpha;
		a1 = -2.0f * cosf(omega);
		a2 =  1.0f - alpha;
		b0 = (1.0f - cosf(omega)) / 2.0f;
		b1 =  1.0f - cosf(omega);
		b2 = (1.0f - cosf(omega)) / 2.0f;

		SET_FILTER_PARAM(f)
	}
}

void SndFilter_SetPrmHighPass(SNDFILTER _f, float cutoff, float q)
{
	INFO_SNDFILTER* f = (INFO_SNDFILTER*)_f;
	if ( f ) {
		PREPARE_FILTER_PARAM

		// 双二次フィルタ用のHPF係数を計算する
		a0 =   1.0f + alpha;
		a1 =  -2.0f * cosf(omega);
		a2 =   1.0f - alpha;
		b0 =  (1.0f + cosf(omega)) / 2.0f;
		b1 = -(1.0f + cosf(omega));
		b2 =  (1.0f + cosf(omega)) / 2.0f;

		SET_FILTER_PARAM(f)
	}
}

void SndFilter_SetPrmBandPass(SNDFILTER _f, float cutoff, float bw)
{
	INFO_SNDFILTER* f = (INFO_SNDFILTER*)_f;
	if ( f ) {
		PREPARE_FILTER_PARAM_BAND

		// 双二次フィルタ用のBPF係数を計算する
		a0 =  1.0f + alpha;
		a1 = -2.0f * cosf(omega);
		a2 =  1.0f - alpha;
		b0 =  alpha;
		b1 =  0.0f;
		b2 = -alpha;

		SET_FILTER_PARAM(f)
	}
}

void SndFilter_SetPrmLowShelf(SNDFILTER _f, float cutoff, float q, float gain)
{
	INFO_SNDFILTER* f = (INFO_SNDFILTER*)_f;
	if ( f ) {
		PREPARE_FILTER_PARAM
		float A =  powf(10.0f, (gain/40.0f));
		float beta = sqrtf(A) / q;

		// 双二次フィルタ用のLowShelf係数を計算する
		a0 = (A + 1.0f)+(A - 1.0f) * cosf(omega) + beta * sinf(omega);
		a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosf(omega));
		a2 = (A + 1.0f) + (A - 1.0f) * cosf(omega) - beta * sinf(omega);
		b0 =  A * ((A + 1.0f) - (A - 1.0f) * cosf(omega) + beta * sinf(omega));
		b1 =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosf(omega));
		b2 =  A * ((A + 1.0f) - (A-  1.0f) * cosf(omega) - beta * sinf(omega));

		SET_FILTER_PARAM(f)
	}
}

void SndFilter_SetPrmHighShelf(SNDFILTER _f, float cutoff, float q, float gain)
{
	INFO_SNDFILTER* f = (INFO_SNDFILTER*)_f;
	if ( f ) {
		PREPARE_FILTER_PARAM
		float A =  powf(10.0f, (gain/40.0f));
		float beta = sqrtf(A) / q;

		// 双二次フィルタ用のHighShelf係数を計算する
		a0 = (A + 1.0f) - (A - 1.0f) * cosf(omega) + beta * sinf(omega);
		a1 =  2.0f * ((A - 1.0f) - (A + 1.0f) * cosf(omega));
		a2 = (A + 1.0f) - (A - 1.0f) * cosf(omega) - beta * sinf(omega);
		b0 =  A * ((A + 1.0f) + (A - 1.0f) * cosf(omega) + beta * sinf(omega));
		b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosf(omega));
		b2 =  A * ((A + 1.0f) + (A - 1.0f) * cosf(omega) - beta * sinf(omega));

		SET_FILTER_PARAM(f)
	}
}

// その他のフィルタが必要な場合は随時追加


// --------------------------------------------------------------------------
//   コールバック群
// --------------------------------------------------------------------------
// XXX この処理はエミュドライバとサウンドデバイスのコールバックで排他である必要があるので注意
STRMTIMER_HANDLER(SndStream_TimerCB)
{
	INFO_SNDSTREAM* snd = (INFO_SNDSTREAM*)prm;

	MUTEX_LOCK(snd->mutex);
	{
		const SINT32 v = snd->volume;
		UINT32 cnt = count;
		SINT32* in = snd->tmpbuf;

		// PULL側（GetPCMからの呼び出し）は SNDBUF_LEN サイズにリミットしてるので、
		// エミュコアが1秒分まとめて呼ばれるとかでない限りはこのチェックには引っかからないはず
		if ( cnt>SNDBUF_LEN ) {
			LOG(("SndStream_TimerCB : srequest size is too large (%d > %d)", (int)cnt, SNDBUF_LEN));
			cnt = SNDBUF_LEN;
		}

		// 全チャンネルの出力を合成
		memset(in, 0, cnt*2*sizeof(SINT32));
		{
			UINT32 i;
			const UINT32 ch = snd->chnum;
			for (i=0; i<ch; i++) {
				UINT32 num = snd->num_filter[i];
				if ( num ) {
					// フィルタがある場合
					UINT32 j;
					memset(snd->fltbuf, 0, cnt*2*sizeof(SINT32));      // フィルタ用の一時バッファをクリア
					snd->cbfunc[i](snd->cbprm[i], snd->fltbuf, cnt);   // フィルタ用の一時バッファにデバイス出力
					// 先に登録したものから順に適用
					for (j=0; j<num; j++) {
						// 最終段以外は一時バッファ→一時バッファ、最終段は一時バッファ→inに書き出し
						DoSndFilter(snd->filter[i][j], snd->fltbuf, in, cnt, ((j+1)<num)?TRUE:FALSE);
					}
				} else {
					snd->cbfunc[i](snd->cbprm[i], in, cnt);
				}
			}
		}

		// ボリュームとリミット処理をして最終出力
		do {
			SINT16* out = snd->buffer+snd->wpos*2;
			UINT32 n = cnt;
#ifdef SAVE_STREAM
			SINT16* _out;
			UINT32 _n;
#endif
			// 終端がバッファ全体長を越えるなら、終端までを埋める
			if ( (snd->wpos+n)>SNDBUF_LEN ) n = SNDBUF_LEN-snd->wpos;
			// 全体長から今回のスライス長を引き、書き込みポイントを更新
			cnt -= n;
			snd->wpos = (snd->wpos+n)&(SNDBUF_LEN-1);
			// 今回のスライス分出力
#ifdef SAVE_STREAM
			_out = out;
			_n = n;
#endif
			do {
				SINT32 d;
				d = ((*in++)*v)>>8;
				*out++ = (SINT16)NUMLIMIT(d,-0x8000,0x7fff);
				d = ((*in++)*v)>>8;
				*out++ = (SINT16)NUMLIMIT(d,-0x8000,0x7fff);
			} while ( --n );
#ifdef SAVE_STREAM
			fwrite(_out, sizeof(SINT16)*2, _n, fp);
#endif
			// 全体長が0になるまでスライスを続ける
		} while ( cnt );

		snd->bufcnt += count;
		if ( snd->bufcnt>SNDBUF_LEN ) {  // バッファがあふれている（GetPCMでの吸い上げが追いついていない＝PUSH側の過供給）
			UINT32 dif = snd->bufcnt%SNDBUF_LEN;
//			LOG(("SndStream_TimerCB : sound buffer overflow(%d > %d)", (int)snd->bufcnt, SNDBUF_LEN));
			snd->rpos = (snd->rpos+dif)&(SNDBUF_LEN-1);
			snd->bufcnt = SNDBUF_LEN;
		}
	}
	MUTEX_UNLOCK(snd->mutex);
}


// --------------------------------------------------------------------------
//   公開関数
// --------------------------------------------------------------------------
// 初期化
STREAMHDL SndStream_Init(TIMERHDL timer, UINT32 freq)
{
	INFO_SNDSTREAM* snd = (INFO_SNDSTREAM*)_MALLOC(sizeof(INFO_SNDSTREAM), "Sound stream");
	do {
		if ( !snd ) break;  // これがないと libansi.a をリンクしようとする。エラーチェック必要と判断されるせい？
		memset(snd, 0, sizeof(INFO_SNDSTREAM));
		MUTEX_INIT(snd->mutex);
		snd->volume = 256;
		snd->freq   = freq;
		memset(snd->buffer, 0, SNDBUF_LEN*2*sizeof(SINT16));
		snd->wpos = 0;
		snd->rpos = 0;
		if ( !Timer_AddStream(timer, SndStream_TimerCB, (void*)snd, freq) ) break;
		LOG(("SndStream (%dHz) Initialized", freq));
#ifdef SAVE_STREAM
		fp = fopen("pcm.snd", "wb");
#endif
		return (STREAMHDL)snd;
	} while ( 0 );

	SndStream_Cleanup((STREAMHDL)snd);

	return NULL;
}


// 破棄
void SndStream_Cleanup(STREAMHDL hstrm)
{
	INFO_SNDSTREAM* snd = (INFO_SNDSTREAM*)hstrm;

#ifdef SAVE_STREAM
	fclose(fp);
#endif
	if ( snd ) {
		MUTEX_RELEASE(snd->mutex);
		_MFREE(snd);
	}
}


// サウンドバッファクリア
// スレッド排他処理は必要に応じて呼び出し元が行うこと
void SndStream_Clear(STREAMHDL hstrm)
{
	INFO_SNDSTREAM* snd = (INFO_SNDSTREAM*)hstrm;
	if ( snd ) {
		snd->buffer[0] = snd->buffer[1] = 0;
		snd->wpos = 0;
		snd->rpos = 0;
		snd->bufcnt = 0;
	}
}


// 出力レートを取得
UINT32 SndStream_GetFreq(STREAMHDL hstrm)
{
	INFO_SNDSTREAM* snd = (INFO_SNDSTREAM*)hstrm;
	if ( snd ) return snd->freq;
	return 44100;
}


// サウンドチャネルを追加
BOOL SndStream_AddChannel(STREAMHDL hstrm, STREAMCHCB cb, void* prm)
{
	INFO_SNDSTREAM* snd = (INFO_SNDSTREAM*)hstrm;
	if ( !snd ) return FALSE;
	if ( snd->chnum>=MAX_SNDCHANNEL ) {
		LOG(("SndStream_AddChannel : サウンドチャネルの数が多すぎます"));
		return FALSE;
	}
	snd->cbfunc[snd->chnum] = cb;
	snd->cbprm[snd->chnum]  = prm;
	snd->chnum++;
	return TRUE;
}


// サウンドチャネルを削除
BOOL SndStream_RemoveChannel(STREAMHDL hstrm, void* prm)
{
	INFO_SNDSTREAM* snd = (INFO_SNDSTREAM*)hstrm;
	UINT32 i;

	if ( !snd ) return FALSE;

	for (i=0; i<snd->chnum; i++) {
		if ( snd->cbprm[i]==prm ) {
			// 指定デバイスが存在している
			break;
		}
	}

	// 指定デバイスが存在しているなら取り除く
	if ( i<snd->chnum ) {
		// 一つずつ前へ
		for (i++; i<MAX_SNDCHANNEL; i++) {
			snd->cbfunc[i-1] = snd->cbfunc[i];
			snd->cbprm[i-1]  = snd->cbprm[i];
		}
		// 最後尾を消去
		snd->cbfunc[MAX_SNDCHANNEL-1] = NULL;
		snd->cbprm[MAX_SNDCHANNEL-1]  = NULL;
		// 総チャンネル数を減らす
		snd->chnum--;
	} else {
		LOG(("SndStream_RemoveChannel : 削除対象が見つかりませんでした"));
		return FALSE;
	}

	return TRUE;
}


// 出力ボリュームをセット（0～256、振幅の単純リニア変化）
void SndStream_SetVolume(STREAMHDL hstrm, UINT32 vol)
{
	INFO_SNDSTREAM* snd = (INFO_SNDSTREAM*)hstrm;
	if ( !snd ) return;
//	if ( vol>=256 ) vol = 256;
	snd->volume = vol;
}


// PCMを取得
// システム（OS／サウンドカード割り込みなど）から呼ばれる
// pcmlenはサンプル数（pcmlen x 2(sizeof(short)) x 2(stereo) が実バイト数になる）
// pcmlenに0を指定すると、現在バッファにあるサンプル数を返す。
//
// ※注）
//  - スレッド排他処理は必要に応じて呼び出し元が行うこと
//  - SNDBUF_LENでの定義時間以内のピリオドで呼ぶこと
UINT32 CALLBACK SndStream_GetPCM(STREAMHDL hstrm, SINT16* buf, UINT32 pcmlen)
{
	INFO_SNDSTREAM* snd = (INFO_SNDSTREAM*)hstrm;
	UINT32 len = pcmlen;
	SINT16* out = buf;
	SINT16* inbase = snd->buffer;
	UINT32 ret = 0;

	if ( !snd ) goto quit;
	if ( !snd->buffer ) goto quit;

	if ( pcmlen==0 ) {
		ret = snd->bufcnt;
		goto quit;
	}

	do {
		UINT32 n, rpos;
		SINT32 size;

		// バッファに空きがあるなら、不足分を追加生成
		size = len-snd->bufcnt;
		if ( (size>0)&&(snd->bufcnt<SNDBUF_LEN) ) {
			// 生成結果がバッファ長を越えるなら、取敢えず空いてるバッファ分だけ処理する
			if ( (snd->bufcnt+size)>SNDBUF_LEN ) size = SNDBUF_LEN-snd->bufcnt;  // snd->bufcnt<SNDBUF_LEN なので sizeが0以下になることはない
			SndStream_TimerCB(hstrm, (UINT32)size);
		}

		MUTEX_LOCK(snd->mutex);
		// 要求長がバッファ済みのデータより多い場合は、取敢えずバッファしてある分だけ処理
		n = (len<snd->bufcnt)?len:snd->bufcnt;
		rpos = snd->rpos*2;
		// nはカウントダウンされるので先に引いておく
		len -= n;
		snd->bufcnt -= n;
		do {
			SINT16* in = inbase+rpos;
			*out++ = *in++;
			*out++ = *in++;
			rpos = (rpos+2)&((SNDBUF_LEN-1)<<1);
		} while ( --n );
		snd->rpos = rpos>>1;
		MUTEX_UNLOCK(snd->mutex);
	} while ( len );

	ret = pcmlen;

  quit:;
	return ret;
}


// ステートロード/セーブ
// snd->buffer はサイズ大きいので、音の乱れが気にならないなら、保存対象から外してもタイミング系への影響はないはず
void SndStream_LoadState(STREAMHDL hstrm, STATE* state, UINT32 id)
{
	INFO_SNDSTREAM* snd = (INFO_SNDSTREAM*)hstrm;
	MUTEX_LOCK(snd->mutex);
	ReadState(state, id, MAKESTATEID('B','U','F','F'), snd->buffer,  sizeof(snd->buffer));
	ReadState(state, id, MAKESTATEID('W','P','O','S'), &snd->wpos,   sizeof(snd->wpos));
	ReadState(state, id, MAKESTATEID('R','P','O','S'), &snd->rpos,   sizeof(snd->rpos));
	ReadState(state, id, MAKESTATEID('B','C','N','T'), &snd->bufcnt, sizeof(snd->bufcnt));
	MUTEX_UNLOCK(snd->mutex);
}
void SndStream_SaveState(STREAMHDL hstrm, STATE* state, UINT32 id)
{
	INFO_SNDSTREAM* snd = (INFO_SNDSTREAM*)hstrm;
	MUTEX_LOCK(snd->mutex);
	WriteState(state, id, MAKESTATEID('B','U','F','F'), snd->buffer,  sizeof(snd->buffer));
	WriteState(state, id, MAKESTATEID('W','P','O','S'), &snd->wpos,   sizeof(snd->wpos));
	WriteState(state, id, MAKESTATEID('R','P','O','S'), &snd->rpos,   sizeof(snd->rpos));
	WriteState(state, id, MAKESTATEID('B','C','N','T'), &snd->bufcnt, sizeof(snd->bufcnt));
	MUTEX_UNLOCK(snd->mutex);
}
