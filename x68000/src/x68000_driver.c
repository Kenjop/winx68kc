/* -----------------------------------------------------------------------------------
  "SHARP X68000" System Driver
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	けろぴー（WinX68k）ベースに新規に起こし直したもの。
	基本的に原形はほぼとどめていない、というかけろぴーのコードが酷すぎる。自分でも読めない。

	TODO:
	〇 優先度高
	- ソース用ドキュメント
	- MUSASHI以外のCPUコアを試す
	- 起動時ウィンドウ表示位置記憶
	- SI.X のリザルトがもう少し実機に近くなるようウェイト
	- windrvの組み込みはどうしよう。Win側のコードとX68000のコードが混然としてるのよねあれ…

	〇 優先度中
	- DMA周りは割とけろぴーそのまま。怪しいので書き直したい
	- ディスク周り（FDC/FDD）のコードもけろぴーから引っ張ってきたので全体的に汚い。書き直したい
	- ATARI仕様以外のジョイパッド対応
	- VDPデバッグ用の表示レイヤごとのON/OFF機能
	- デバッグ用の各種レジスタ等表示機能

	〇 優先度低
	- D88 ディスクイメージ対応（めんどいのでやりたくない。必要なタイトルがあった場合）
	- GVRAM 高速クリアの処理が適当なのを直す（相当めんどいので、不具合の出るタイトルが見つかれば）
	- FDDのアクセスランプのON/OFFをFDC側から（SRT/HUT/HLT考慮して）通知した方が正確になる？
	- FMコア変更（ymfm）→ 試したが処理くっそ重いのでペンディング
	- Win32部分以外のC++化（構造的にはそんなに手間はかからない気がする、が、タイマ管理とビデオ周
	  り、あとFDD辺りはC++に合わせて大幅書き直しかなあ）
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
// ボリュームバランスはフィルタ適用後で取ってるので、フィルタ変更する場合は注意
// エトプリSE、ドラキュラBGM辺りで調整
// OPMを0.9くらいより上にすると、HEAVY NOVAのOP（縦スクロールで要塞っぽいのが出るとこ）で音が割れる
#define VOLUME_OPM          0.80f  // 1.00f
#define VOLUME_ADPCM        1.45f  // 1.80f

// 68000の動作クロック（起動時設定可能）
#define X68000_CPU_CLK      (drv->cpu_clk)

// 最大メインRAM容量 12Mb（実サイズは起動時設定）
#define MAIN_RAM_SIZE_MAX   (12*1024*1024)
#define DEFAULT_RAM_SIZE    (2*1024*1024)


// --------------------------------------------------------------------------
//   enums
// --------------------------------------------------------------------------
typedef enum {
	H_EV_SYNCE = 0,  // 水平同期パルス終了位置
	H_EV_START,      // 水平表示期間開始位置
	H_EV_END,        // 水平表示期間終了位置
	H_EV_TOTAL,      // 水平トータル
	H_EV_MAX
} CRTC_H_EV;

typedef enum {
	V_EV_SYNCE = 0,  // 上記の垂直同期版
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
//	float          h_step;   // イベント駆動になったので現在は使ってない
//	float          h_count;  // 同上
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
	// 各ドライバ共通データ（C++におけるベースクラス相当）
	EMUDRIVER      drv;
	// デバイス類
	MEM16HDL       mem;
	CPUDEV*        cpu;
	TIMER_ID       tm_hsync[H_EV_MAX]; // 同期用
	TIMER_ID       tm_contrast;        // コントラスト変化用
	TIMER_ID       tm_keybuf;          // キーバッファ補充用
	SNDFILTER      hpf_adpcm;
	SNDFILTER      lpf_adpcm;
	SNDFILTER      lpf_opm;
	// X68kデバイス群
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
	// メモリ
	UINT8          ram[MAIN_RAM_SIZE_MAX];  // 確保だけは最大サイズで行う
	UINT8          sram[0x4000];
	// 構造体型情報
	INFO_TIMING    t;
	INFO_PPI       ppi;
	INFO_CRTC_OP   crtc_op;
	INFO_MOUSE     mouse;
	INFO_KEYBUF    keybuf;
	// その他の情報
	UINT8          sysport[8];
	UINT8          joyport[2];
	// 起動時オプション
	UINT32         cpu_clk;
	UINT32         ram_size;
	BOOL           fast_fdd;
} X68000_DRIVER;

// 実行プラットフォーム側のエンディアンに再配置されたROMコピー
static UINT8 ipl_endian [0x20000];
static UINT8 font_endian[0xC0000];


// --------------------------------------------------------------------------
//   Internal
// --------------------------------------------------------------------------
#define SET_GPIP(a,b)  X68MFP_SetGPIP(drv->mfp,a,b)
#define CRTC(r)        (READBEWORD(&drv->vid->crtc[(r)*2]))

static void UpdateSpOffset(X68000_DRIVER* drv)
{
	const UINT32 bgsp_reso = drv->vid->bgram[0x810/2];  // bg_reso & 0x01 で 512 ドットモード
	const UINT32 crtc_reso = CRTC(20);

	// BG/SPのズレ計算（アトミックロボキッドなどが使用）
	drv->vid->sp_ofs_x = ( (SINT32)(drv->vid->bgram[0x80C/2]&0x03F) - (SINT32)(CRTC(2)+4) ) << 3;
	drv->vid->sp_ofs_y = (SINT32)(drv->vid->bgram[0x80E/2]&0x0FF) - (SINT32)CRTC(6);

	// 基本的にカプコン系はCRTCとBGSP間の設定に1ドット差があるものが多い（ファイナルファイト、大魔界村、ストライダー飛竜など）
	// 1ラスタ分の時間が稼げるから？
	drv->vid->sp_ofs_y &= ~1;
		
	// ズレをCRTCの高解像度/標準解像度ベースで補正（必要なタイトルあるのか不明）
	if ( ( crtc_reso ^ (bgsp_reso<<2) ) & 0x10 ) {  // CRTCの解像度設定の基本走査線数と、BGSPの垂直解像度設定の間に違いがある場合
		if ( crtc_reso & 0x10 ) {
			// 高解像度時はCRTC値は512ラインベースなので、BGSP=256ラインなら半分で使用
			drv->vid->sp_ofs_y >>= 1;
		} else {
			// 標準解像度時はCRTC値は256ラインベースなので、BGSP=512ラインなら倍で使用
			drv->vid->sp_ofs_y <<= 1;
		}
	}
}

static void UpdateCrtTiming(X68000_DRIVER* drv)
{
	#define HF0_OSC_CLK  38864000  // 標準解像度 OSC 周波数
	#define HF1_OSC_CLK  69552000  // 高解像度 OSC 周波数

	// 以下のテーブル内の数値は全て割り切れて整数になるはず
	static const UINT32 DOT_CLOCK_TABLE[2][8] = {
		{  // HRL=0
			HF0_OSC_CLK/8, HF0_OSC_CLK/4, HF0_OSC_CLK/8, HF0_OSC_CLK/4,  // 標準解像度 256 / 512 /  ?  /  ?   XXX ？部分が正しく動くか不明
			HF1_OSC_CLK/6, HF1_OSC_CLK/3, HF1_OSC_CLK/2, HF1_OSC_CLK/3   // 高解像度   256 / 512 / 768 / 512  実機で試した限りこう？
		},
		{  // HRL=1
			HF0_OSC_CLK/8, HF0_OSC_CLK/4, HF0_OSC_CLK/8, HF0_OSC_CLK/4,  // 標準解像度 256 / 512 /  ?  /  ?
			HF1_OSC_CLK/8, HF1_OSC_CLK/4, HF1_OSC_CLK/2, HF1_OSC_CLK/4   // 高解像度   192 / 384 / 768 / 384  実機で試した限りこう？
		}
	};
	const UINT32 r20 = CRTC(20);
	const UINT32 idxh = ( ( r20 & 0x10/*HF*/ ) >> 2 ) | ( r20 & 0x03/*HD*/ );
	const UINT32 idxv = ( r20 & 0x10/*HF*/ ) >> 4;
	const UINT32 hrl = ( drv->sysport[4] & 0x02 ) >> 1;
	const UINT32 old_h_st = drv->t.h_ev_pos[H_EV_START];
	const UINT32 old_h_ed = drv->t.h_ev_pos[H_EV_END];

	// タイミングデータ取得
	drv->t.h_ev_pos[H_EV_SYNCE] = (CRTC(1)+1) << 3;  // H-SYNC END
	drv->t.h_ev_pos[H_EV_START] = (CRTC(2)+5) << 3;  // H-START
	drv->t.h_ev_pos[H_EV_END  ] = (CRTC(3)+5) << 3;  // H-END
	drv->t.h_ev_pos[H_EV_TOTAL] = (CRTC(0)+1) << 3;  // H-TOTAL
	drv->t.v_ev_pos[V_EV_SYNCE] = (CRTC(5)+1);       // V-SYNC END
	drv->t.v_ev_pos[V_EV_START] = (CRTC(6)+1);       // V-START
	drv->t.v_ev_pos[V_EV_END  ] = (CRTC(7)+1);       // V-END
	drv->t.v_ev_pos[V_EV_TOTAL] = (CRTC(4)+1);       // V-TOTAL

	// HEAVY NOVAなど、V-ENDがV-TOTALよりも大きいタイトル用の処理（QuickHack的でアレ）
	if ( drv->t.h_ev_pos[H_EV_END] > drv->t.h_ev_pos[H_EV_TOTAL] ) drv->t.h_ev_pos[H_EV_END] = drv->t.h_ev_pos[H_EV_TOTAL];
	if ( drv->t.v_ev_pos[V_EV_END] > drv->t.v_ev_pos[V_EV_TOTAL] ) drv->t.v_ev_pos[V_EV_END] = drv->t.v_ev_pos[V_EV_TOTAL];

	// HDISP/VDISP のサイズ（＝表示画面サイズ）を取得
	drv->vid->h_sz = drv->t.h_ev_pos[H_EV_END] - drv->t.h_ev_pos[H_EV_START];
	drv->vid->v_sz = drv->t.v_ev_pos[V_EV_END] - drv->t.v_ev_pos[V_EV_START];

	// hstep は M68000 の 1clk で進むドットクロック数
	// イベント駆動式になったので使わなくなった
//	drv->t.h_step = (float)DOT_CLOCK_TABLE[hrl][idx] / (float)X68000_CPU_CLK;

	// VSYNC 周波数、計算はしてるが使ってない（イベント周期と総ドット数で自動的にこの値になるはず）
	// 上位層で表示したい場合のために計算は残す
	drv->t.hsync_hz = (float)DOT_CLOCK_TABLE[hrl][idxh] / (float)(drv->t.h_ev_pos[H_EV_TOTAL]);
	drv->t.vsync_hz = (float)DOT_CLOCK_TABLE[hrl][idxh] / (float)(drv->t.h_ev_pos[H_EV_TOTAL]*drv->t.v_ev_pos[V_EV_TOTAL]);

	// 2重描画/半数描画調整用
	switch ( r20 & 0x14 ) {
		case 0x04:  // 標準解像度・垂直512ドット
			drv->t.line_shift = DRAW_LINE_DOUBLE;
			break;
		case 0x10:  // 高解像度・垂直256ドット
			drv->t.line_shift = DRAW_LINE_HALF;
			break;
		case 0x00:  // 標準解像度・垂直256ドット
		case 0x14:  // 高解像度・垂直512ドット
		default:
			drv->t.line_shift = DRAW_LINE_NORMAL;
			break;
	}
	drv->vid->v_sz <<= 1;
	drv->vid->v_sz >>= drv->t.line_shift;

	// ドルアーガが560ライン設定（の半ラスタ280ラインモード）に設定するので、最終解像度の段階でチェックする
	// （でないと現行の最大512ラインチェックに引っかかってフルサイズ表示されない）
	if ( drv->vid->h_sz > X68_MAX_VSCR_WIDTH ) {
		LOG(("### UpdateCrtTiming : CRTC setting error, H-SIZE=%d is too large.", drv->vid->h_sz));
		drv->vid->h_sz = X68_MAX_VSCR_WIDTH;
	}
	if ( drv->vid->v_sz > X68_MAX_VSCR_HEIGHT ) {
		LOG(("### UpdateCrtTiming : CRTC setting error, V-SIZE=%d is too large.", drv->vid->v_sz));
		drv->vid->v_sz = X68_MAX_VSCR_HEIGHT;
	}

	// 描画領域取得
	{
		typedef struct {
			UINT32  total;
			UINT32  pulse;
			UINT32  start;
			UINT32  end;
		} CRTC_DEFAULT_TIMING;

		// 取敢えずわかってる分だけ埋めとく／？付部分は現状不明 or 未テスト
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
			// TOTALと同期パルス幅が標準設定の場合は、デフォルト領域に固定
			drv->t.scrn_area.x1 = (dt_h->start+5) << 3;
			drv->t.scrn_area.x2 = (dt_h->end  +5) << 3;
			drv->t.scrn_area.y1 = (dt_v->start+1);
			drv->t.scrn_area.y2 = (dt_v->end  +1);
		} else {
			// それ以外の設定の場合、怪しげな計算
			// 基本的には「標準設定のSTART/ENDが、現在のCRTC設定におけるどの辺りに来るか」を、同期パルス幅を除いた部分の比で
			// 計算して求めている。計算自体の妥当性は微妙
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
			// 補正後サイズが元の表示エリアより小さいなら、縦横比を維持したまま元サイズが入るように広げる
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

	// スクリーンバッファのクリップ領域変更（Win側描画などの上位層用）
	do {
		UINT32 w = _MIN(drv->vid->h_sz, drv->vid->vscr_w);
		UINT32 h = _MIN(drv->vid->v_sz, drv->vid->vscr_h);
		DRIVER_SETSCREENCLIP(0, 0, w, h);
	} while ( 0 );

	// H方向の描画エリアが広がったフレームの描画ゴミ回避（AQUALESなど）
	if ( ( old_h_st > drv->t.h_ev_pos[H_EV_START] ) || ( old_h_ed < drv->t.h_ev_pos[H_EV_END] ) ) {
		drv->t.drawskip = TRUE;
	}

	// HSYNCタイマ更新
	{
	TUNIT hsync = TIMERPERIOD_HZ(drv->t.hsync_hz);
	double total = (double)drv->t.h_ev_pos[H_EV_TOTAL];
//	Timer_ChangeStartAndPeriod(_MAINTIMER_, drv->tm_hsync[0], DBL2TUNIT((double)(drv->t.h_ev_pos[H_EV_SYNCE]*hsync)/total), hsync);  // 現状同期パルス終了タイミングは使ってない
	Timer_ChangeStartAndPeriod(_MAINTIMER_, drv->tm_hsync[1], DBL2TUNIT((double)(drv->t.h_ev_pos[H_EV_START]*hsync)/total), hsync);
	Timer_ChangeStartAndPeriod(_MAINTIMER_, drv->tm_hsync[2], DBL2TUNIT((double)(drv->t.h_ev_pos[H_EV_END  ]*hsync)/total), hsync);
	Timer_ChangeStartAndPeriod(_MAINTIMER_, drv->tm_hsync[3], hsync, hsync);
	}

	// SP補正更新
	UpdateSpOffset(drv);

	// 更新フラグを落とす
	drv->t.update = FALSE;

//LOG(("UpdateCrtTiming : HRL=%d, H=%d/%d/%d/%d(%d), V=%d/%d/%d/%d, %dx%d %.3fkHz/%.3fHz", hrl, drv->t.h_ev_pos[0], drv->t.h_ev_pos[1], drv->t.h_ev_pos[2], drv->t.h_ev_pos[3], CRTC(8), drv->t.v_ev_pos[0], drv->t.v_ev_pos[1], drv->t.v_ev_pos[2], drv->t.v_ev_pos[3], drv->vid->h_sz, drv->vid->v_sz, drv->t.hsync_hz/1000, drv->t.vsync_hz));
}

static void DoFastClear(X68000_DRIVER* drv)
{
	/*
		FastClr 動作の注意点（PITAPAT/DYNAMITE DUKE 辺りの挙動より）

		1. CRTC動作ポート で 0x02 を立てると起動待機状態に入る（この時点ではリードバックしても 0x02 が返らない）
		2. VDISP 開始時点で動作中になる（CRTC動作ポートのリードバックで 0x02 が立つ）
		3. VDISP 期間内に消去を実行し、次の VBLANK 突入時点で終了する（0x02 が落ちる）

		正確には1フレームかけて毎ラスタクリアしていくのが正しい。
		また、VDP の GrDrawLine のように、各 GP ごとのスクロールレジスタを反映させるのが正しいと思われる。
		が、めんどいので適当に簡易化 ＆ ここで一括処理する。
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
		Level7 : NMIスイッチ（オートベクタ）
		Level6 : MFP
		Level5 : SCC
		Level4 : MIDI (ボード上のスイッチで選択)
		Level3 : DMAC
		Level2 : MIDI (ボード上のスイッチで選択)
		Level1 : I/Oコントローラ
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
		-------2   CT2：FDCの強制READY
		------1-   CT1：ADPCM基本クロック（0:8MHz、1:4MHz）
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
	// XXX 変化速度不明、現状は 1/40 秒で1段階変化としている
	if ( drv->vid->contrast < drv->sysport[1] ) {
		drv->vid->contrast++;
	} else if ( drv->vid->contrast > drv->sysport[1] ) {
		drv->vid->contrast--;
	} else {
		// 変化終了したらタイマを止めておく
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
		「アルゴスの戦士」や「クレイジークライマー」などが停止する問題：

		GPIPを見張ってVBLANKを待っているが、VBLANK Intも有効でそちらが先に走る＆割り込み処理が長く
		VBLANK期間に処理が終わらないため、永久にVBLANKフラグを見つけられなくなる、という問題。
		実機では丁度VBLANKに入った瞬間にGPIPを読み、直後に割り込みが入る、というタイミングが存在す
		ると思われるが、エミュだとこれが発生しなかった（↓のイベント処理でVBLANKに入ると同時に割り
		込み発生→次のOPコード前に割り込み処理に入ってしまう、という挙動のため、VBLANKに入った瞬間
		を取り込むタイミングが存在しない）。

		よってこのエミュでは、水平・垂直カウンタイベントで発生するMFPの割り込みを微小時間ペンディン
		グしておいてから割り込みを上げることで、上記を回避するようにしている（IRQタイマの内、MFP用
		の6番だけ0.5usほどの遅延を入れている）。

		KLAX、メタルオレンジEX辺りの停止も同問題。
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

		// ラスタ割り込みは同ラインの描画と同じか、それより遅いタイミングでないと、メルヘンメイズで表示ゴミが出る
		// ゼビウスがMFPのCIRQを見張るので、ちゃんとある程度の期間Lowが出ないとダメ
		if ( drv->t.v_count == drv->t.vint_line ) {
			SET_GPIP(GPIP_BIT_CIRQ, 0);  // ActiveLow
		}

		// このラインを描画（上記に合わせてHDISP終了タイミングに移動）
		// 10MHz動作時のドラキュラのステータス下部にまれにちらつきが出るが、タイミング的にはここが望ましい
		if ( drv->t.next_v_ev == V_EV_END )  // 次 EV が V-END、つまり現在 V-DISP 期間
		{
			// VDISP期間なので1ライン描画
			UINT32 line = drv->t.v_count - drv->t.v_ev_pos[V_EV_START];
			// 表示モード別に必要なラスタを描画
			switch ( drv->t.line_shift )
			{
			case DRAW_LINE_DOUBLE:  // 標準解像度・512ライン（インタレース）
				X68Video_LineUpdate(drv->vid, (line<<1)+0);
				X68Video_LineUpdate(drv->vid, (line<<1)+1);
				break;
			case DRAW_LINE_HALF:    // 高解像度・256ライン（奇数ラインの時のみ描く）
				if ( line & 1 ) {
					X68Video_LineUpdate(drv->vid, line>>1);
				}
				break;
			case DRAW_LINE_NORMAL:  // 標準（毎ライン描く）
			default:
				X68Video_LineUpdate(drv->vid, line);
				break;
			}
		}
		break;

	case H_EV_TOTAL:  // H-END -> H-TOTAL
		// ラスタカウントの進行
		drv->t.v_count++;
//		drv->t.h_count -= drv->t.h_ev_pos[H_EV_TOTAL];  // イベント駆動になったので現在は使ってない

		// ラスタ割り込みOFF（現在の状態に関係なく）
		SET_GPIP(GPIP_BIT_CIRQ, 1);

		// ラスタイベント進行
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
				// 高速クリア実行待ち状態なら開始状態に移行する（実行もここで行う）
				if  ( drv->crtc_op.fastclr_state == FASTCLR_WAIT ) {
					drv->crtc_op.fastclr_state = FASTCLR_EXEC;
					DoFastClear(drv);
				}
				break;

			case V_EV_END:    // V-START -> V-END
				drv->t.next_v_ev++;
				SET_GPIP(GPIP_BIT_VDISP, 0);
				// 高速クリア開始状態なら終了状態に移行（要求待機状態に戻る）
				if  ( drv->crtc_op.fastclr_state == FASTCLR_EXEC ) {
					drv->crtc_op.fastclr_state = FASTCLR_IDLE;
				}
				// ここで画面確定（VDP仮想スクリーンから表示テクスチャへ転送）
				X68Video_Update(drv->vid, drv->drv.scr);
				drv->drv.scr->frame++;
				drv->t.drawskip = FALSE;
				if ( drv->t.update ) UpdateCrtTiming(drv);  // 走査線が左上に戻る瞬間にタイミングパラメータを更新する
				break;

			case V_EV_TOTAL:  // V-END -> V-TOTAL
				drv->t.next_v_ev = 0;
				drv->t.v_count = 0;
				break;
			}
		}
		break;
	}

	// DMA#0～#2実行（#3はADPCMからサンプル補充が必要な時に呼び出し）
	// DMA#0（FDC）
	if ( drv->fast_fdd ) {
		while ( X68DMA_Exec(drv->dma, 0) ) {
			// 可能な限り連続呼び出ししてみる
		}
	} else {
		// 「NAIOUS」や「斬」OP、「ファランクス」OPなど、FDDアクセスが速すぎるとシーケンスが崩れるタイトル用に、
		// デフォルトでは遅め（1ラスタに2回）
		if ( opt & 1 ) X68DMA_Exec(drv->dma, 0);
	}
	// その他は水平イベントごとに（1ラスタに3回）呼んでみる
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
	X68DMA_BusErr(drv->dma, adr, TRUE);   // DMA側が拾わなかった時は、X68DMA_BusErr() 内でCPU側へ通知される
	return 0xFF;
}

static MEM16W_HANDLER(X68000_WriteOpenBus)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
//LOG(("### BusErr(W) : $%06X", adr));
	X68DMA_BusErr(drv->dma, adr, FALSE);  // DMA側が拾わなかった時は、X68DMA_BusErr() 内でCPU側へ通知される
}

static MEM16R_HANDLER(X68000_DUMMY_R)
{
	// バスエラーの起きない空きアドレス空間用
	return 0xFF;
}

static MEM16W_HANDLER(X68000_DUMMY_W)
{
	// バスエラーの起きない空きアドレス空間用
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

	// GVRAM のアドレス換算の考え方については X68000_GVRAM_W のコメント参照
	switch ( reg28 & 0x0B )
	{
		case 0x00:  // 16色
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

		case 0x01:  // 256色
		case 0x02:  // 未定義（256色と同じ動作？ → 前半0x80000は256色と同じ、後半は0が返る？）
			if ( ( adr < 0x100000 ) && ( adr & 1 ) ) {
				adr ^= (adr>>19) & 1;
				adr = (adr^LE_ADDR_SWAP) & 0x7FFFF;
				ret = gvram[adr];
			}
			break;

		default:   // 65536色  0x08 が立っている場合もこのメモリ配置っぽい（NEMESIS/苦胃頭捕物帳）
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
			// リードバックできるのは R20/R21 のみ？
			return drv->vid->crtc[adr&0x3F];
		} else if ( adr<0x30 ) {
			// その他のCRTCレジスタは0が返る
			return 0x00;
		}
	} else if ( adr>=0x480 && adr<=0x4FF ) {
		UINT32 ret = 0x00;
		if ( adr & 1 ) {
			// ラスタコピービット、及び高速クリア動作中フラグだけが返る
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
	UINT32 ret = 0;  // 非デコード領域の返値は0

	adr &= 0x0FFF;  // $E82000-$E83FFF / 後半はミラー

	if ( adr < 0x400 ) {
		// パレットエリア
		const UINT8* p = (const UINT8*)drv->vid->pal;
		ret = p[adr^LE_ADDR_SWAP];
	} else {
		// 0x0400～0x0FFF はワード単位でミラー
		// 0x0800以降へのアクセスは、デコードはされていないがバスエラーは出ない（イメージファイト デモ開始直前）
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
	// 未使用ビットは1を返すよう変更
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	UINT8 ret = 0;
	switch ( adr & 0x0F )
	{
		case 0x01:  // SysPort #1
			ret = drv->sysport[1] | 0xF0;
			break;
		case 0x03:  // SysPort #2
			ret = drv->sysport[2] | 0xF4;
			ret &= ~0x08;  // ディスプレイONに固定
			break;
		case 0x05:  // SysPort #3
			ret = drv->sysport[3] | 0xE0;
			break;
		case 0x07:  // SysPort #4
			ret = drv->sysport[4] | 0xF1;
			ret |= 0x08;   // キーボード接続状態に固定
			break;
		case 0x0B:  // 10MHz:0xFF、16MHz:0xFE、030(25MHz):0xDCをそれぞれ返すらしい
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
		case 0x00:  // 16色
			if ( adr & 1 ) {
				if ( reg28 & 0x04 ) { 
					// 1024 dot
					/*
						Inside X68000 p.192 より

						1024 x 1024 モードは実際には以下のような配置になっており、
						GD0 が GVRAM 16bit の bit0～3、GD1 が bit4～7、GD2 が bit8～11、
						GD2 が bit12～15 という割り当てになる。

						       1024dot
						    <----------->
						    -------------  -
						    | GD0 | GD1 |  |
						    -------------  | 1024dot
						    | GD2 | GD3 |  |
						    -------------  -

						よって、GD0～GD3 を 65536 色モードのアドレス配置に換算した上で、
						16bit の内上記に対応した 4bit を書き換えればよい。
					*/
					UINT16* wp = (UINT16*)(&gvram[((adr&0xFF800)>>1)+(adr&0x3FE)]);
					UINT32 sft = ( (adr>>17) & 0x08 ) + ( (adr>>8)&0x04 );
					*wp = ( *wp & ~(0x0F<<sft) ) | ( (data&0x0F)<<sft );
				} else { 
					// 512 dot
					/*
						こちらは単純に、ページ0～ページ3の順で 0/4/8/12 bit 目～の 4bit
						に割り当て。
					*/
					UINT16* wp = (UINT16*)(&gvram[adr&0x7FFFE]);
					UINT32 sft = ( adr>>17 ) & 0x0C;
					*wp = ( *wp & ~(0x0F<<sft) ) | ( (data&0x0F)<<sft );
				}
			}
			break;

		case 0x01:  // 256色
		case 0x02:  // 未定義（256色と同じ動作？ → 前半0x80000は256色と同じ、後半は無視される？）
			/*
				16色よりは単純。
				16bit 単位の GVRAM において、ページ0が下位バイト、ページ1が上位バイトになる。
			*/
			if ( ( adr < 0x100000 ) && ( adr & 1 ) ) {
				adr ^= (adr>>19) & 1;
				adr = (adr^LE_ADDR_SWAP) & 0x7FFFF;
				gvram[adr] = (UINT8)data;
			}
			break;

		default:   // 65536色  0x08 が立っている場合もこのメモリ配置っぽい（NEMESIS/苦胃頭捕物帳）
			if ( adr < 0x080000 ) {
				adr ^= LE_ADDR_SWAP;
				gvram[adr] = (UINT8)data;
			}
			break;
	}
}

static MEM16W_HANDLER(X68000_TVRAM_W)
{
	// 同時アクセス/マスクによって関数で処理分ける方が速い？
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;
	UINT8* tvram = (UINT8*)drv->vid->tvram;
	UINT32 reg2A = drv->vid->crtc[0x2A];
	adr ^= LE_ADDR_SWAP;

	// テキストVRAMは1ライン 0x80 バイトなので、adr>>7 がライン番号になる
	#define SET_DIRTY                     { UINT32 l = (adr>>7) & 1023; drv->vid->tx_dirty[l>>5] |= (1<<(l&31)); }
	#define MASK_DATA(_ofs_)              ( tvram[adr+_ofs_] & m ) | ( data & ~m );
	#define WRITE_TX(_ofs_,_dt_,_dirty_)  if ( tvram[adr+_ofs_] != (UINT8)_dt_ ) { tvram[adr+_ofs_] = (UINT8)_dt_; _dirty_; }

	if ( reg2A & 0x01 ) {
		// 同時アクセスON
		UINT32 reg2B = drv->vid->crtc[0x2B];
		UINT32 dirty = 0;
		adr &= 0x1FFFF;
		if ( reg2A & 0x02 ) {
			// マスクあり
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
		// 同時アクセスOFF
		adr &= 0x7FFFF;
		if ( reg2A & 0x02 ) {
			// マスクあり
			UINT32 m = drv->vid->crtc[0x2E+((adr&1)^LE_ADDR_SWAP)];
			data = MASK_DATA(0);
		}
		WRITE_TX(0, data, SET_DIRTY);
	}
}

static MEM16W_HANDLER(X68000_BGRAM_W)
{
	// XXX 全てのBGRAMアクセスをフックするのと、毎ライン以下の計算が入るのとではどっちが軽いかは微妙？
	//     取敢えず現状は前者の方がマシ、という判断
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;

	// 画面モードレジスタ変更時に、BG/SP描画用パラメータを更新する
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
		// CRTCエリア 0x40ブロックループ
		adr &= 0x3F;
		switch ( adr )
		{
			// 有効ビットだけ取り込むように変更（STAR MOBILE）
			// 上位無効
			case 0x00:  // H-TOTAL   (H)
			case 0x02:  // H-SYNCEND (H)
			case 0x04:  // H-DISP    (H)
			case 0x06:  // H-DISPEND (H)
			case 0x10:  // H-ADJUST  (H)
				break;

			// タイミング更新必要 上位2bitのみ有効
			case 0x08:  // V-TOTAL   (H)
			case 0x0A:  // V-SYNCEND (H)
			case 0x0C:  // V-DISP    (H)
			case 0x0E:  // V-DISPEND (H)
				data &= 0x03;
				if ( drv->vid->crtc[adr] != data ) drv->t.update = TRUE;
				drv->vid->crtc[adr] = (UINT8)data;
				break;

			// タイミング更新必要 全bit有効
			case 0x01:  // H-TOTAL   (L)
			case 0x03:  // H-SYNCEND (L)
			case 0x05:  // H-DISP    (L)
			case 0x07:  // H-DISPEND (L)
			case 0x09:  // V-TOTAL   (L)
			case 0x0B:  // V-SYNCEND (L)
			case 0x0D:  // V-DISP    (L)
			case 0x0F:  // V-DISPEND (L)
			case 0x11:  // H-ADJUST  (L)
			case 0x29:  // メモリモード/表示モード制御(L)
				if ( drv->vid->crtc[adr] != data ) drv->t.update = TRUE;
				drv->vid->crtc[adr] = (UINT8)data;
				break;

			// ラスタ割り込み
			// 同一ラインで複数回入ってはいけないっぽい
			// なのでここでの割り込みチェックは行わない（メルヘンメイズ、KnightArmsなど）
			case 0x12:  // 上位は2bitのみ有効
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

			// スクロールレジスタ類 上位2bitのみ有効
			case 0x14:  // TX scroll X  (H)
			case 0x16:  // TX scroll Y  (H)
			case 0x18:  // GR0 scroll X (H)
			case 0x1A:  // GR0 scroll Y (H)
				data &= 0x03;
				drv->vid->crtc[adr] = (UINT8)data;
				break;

			// スクロールレジスタ類 上位1bitのみ有効
			case 0x1C:  // GR1 scroll X (H)
			case 0x1E:  // GR1 scroll Y (H)
			case 0x20:  // GR2 scroll X (H)
			case 0x22:  // GR2 scroll Y (H)
			case 0x24:  // GR3 scroll X (H)
			case 0x26:  // GR3 scroll Y (H)
				data &= 0x01;
				drv->vid->crtc[adr] = (UINT8)data;
				break;

			// ラスタコピー 全bit有効
			case 0x2C: // ラスタコピーsrc
			case 0x2D: // ラスタコピーdst
				if ( ( drv->vid->crtc[adr] != data ) && ( drv->crtc_op.reg & 0x08 ) ) {
					// $E80481 のラスタコピーフラグをONにしておいて src/dst だけを変更して連続実行することも可能らしい（ドラキュラなど）
					// 正確な動作としては、HBLANK 突入時点でラスタコピーONなら1回分を実行？
					drv->crtc_op.do_raster_copy = TRUE;
				}
				drv->vid->crtc[adr] = (UINT8)data;
				break;

			// その他 基本的に全bit取り込み
			default:
				drv->vid->crtc[adr] = (UINT8)data;
				break;
		}
	} else if ( adr>=0x480 && adr<=0x4FF ) {
		if ( adr & 1 ) {
			drv->crtc_op.reg = data & 0x08;  // ラスタコピーフラグだけ保存
			if ( drv->crtc_op.reg ) {
				// TXRAMラスタコピーON
				// XXX AQUALESがOPで1ラスタに2回src/dstレジスタを書き換えるので、フラグON時には即時実行しておく
				DoRasterCopy(drv);
//LOG(("%03d : RasterCopy enable", drv->t.v_count));
			} else if ( ( data & 0x02 ) && ( drv->crtc_op.fastclr_state == FASTCLR_IDLE ) ) {
				// GVRAM高速クリア
				// ラスタコピーとの同時ONではラスタコピー優先、高速クリアは実行されない。
				drv->crtc_op.fastclr_state = FASTCLR_WAIT;
			}
		}
	}
}

static MEM16W_HANDLER(X68000_VCTRL_W)
{
	X68000_DRIVER* drv = (X68000_DRIVER*)prm;

	adr &= 0x0FFF;  // $E82000-$E83FFF / 後半はミラー

	if ( adr < 0x400 ) {
		// パレットエリア
		UINT8* p = (UINT8*)drv->vid->pal;
		adr ^= LE_ADDR_SWAP;
		p[adr] = (UINT8)data;
		// テキストパレット範囲の場合はdirtyフラグを立てておく
		if ( (UINT32)(adr-0x200) < 0x20 ) {
			drv->vid->txpal_dirty = TRUE;
		}
	} else {
		// 0x0400～0x0FFF はワード単位でミラー（ドラスピが下位ワードにデータを入れてLONGアクセスしてる）
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
					X68Video_UpdateMixFunc(drv->vid);  // MIX関数更新
				}
				break;
			case 0x501:  // $E82501
				drv->vid->vctrl1 = (drv->vid->vctrl1 & 0xFF00) | ((data&0xFF)<<0);
				break;
			case 0x600:  // $E82600
				old = drv->vid->vctrl2;
				drv->vid->vctrl2 = (drv->vid->vctrl2 & 0x00FF) | ((data&0xFF)<<8);
				if ( old != drv->vid->vctrl2 ) {
					X68Video_UpdateMixFunc(drv->vid);  // MIX関数更新
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
		case 0x01:  // SysPort #1  画面コントラスト
			drv->sysport[1] = data & 0x0F;
			// コントラストは、例えば 0 → 15 と変化させた場合、設定後すぐにその明るさになるのではなく、徐々に
			// 変化するっぽい（KnightArms）
			if ( drv->sysport[1] != drv->vid->contrast ) {
				// コントラスト変化用タイマ起動
				// （現在は1/40秒ごとに1段階変化、遅いとファンタジーゾーンのローディング前のごみが目立つ）
				Timer_ChangePeriod(_MAINTIMER_, drv->tm_contrast, TIMERPERIOD_HZ(40));
			}
			break;
		case 0x03:  // SysPort #2  テレビ／3Dスコープ制御
			drv->sysport[2] = data & 0x0B;
			break;
		case 0x05:  // SysPort #3  カラーイメージユニット用
			drv->sysport[3] = data & 0x1F;
			break;
		case 0x07:  // SysPort #4  キーボードコントロール／NMI ack
			if ( ( drv->sysport[4] ^ data ) & 0x02 ) {
				// HRL変更時はタイミングパラメータ更新
				drv->t.update = TRUE;
			}
			drv->sysport[4] = data & 0x0E;
			break;
		case 0x0D:  // SysPort #5  SRAM書き込み制御
			drv->sysport[5] = data & 0xFF;
			break;
		case 0x0F:  // SysPort #6  電源OFF制御
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

				Inside X68000 p.295-296 のパンに関する記述はL/R逆だと思う
			*/
			drv->ppi.portc = (UINT8)data;
			X68ADPCM_SetChannelVolume(drv->adpcm, (drv->ppi.portc&2)?0.0f:1.0f/*L*/, (drv->ppi.portc&1)?0.0f:1.0f/*R*/);
			X68ADPCM_SetPrescaler(drv->adpcm, (drv->ppi.portc>>2)&3);
			break;
		case 0x07:  // Control
			drv->ppi.ctrl  = (UINT8)data;
			if ( !(drv->ppi.ctrl & 0x80) ) {  // 最上位が0ならPortCビットコントロール
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
	// SRAM書き込み禁止フラグのチェックが必要
	if ( drv->sysport[5] == 0x31 ) {
		drv->sram[adr&0x3FFF] = (UINT8)data;
	}
}



// --------------------------------------------------------------------------
//   各種デバイス初期化処理
// --------------------------------------------------------------------------
static void InitDevices(X68000_DRIVER* drv)
{
	UINT32 i;

	// CRTC初期値
	static const UINT8 _CRTC_DEFAULT_[] = {
		0x00, 0x89, 0x00, 0x0E, 0x00, 0x1C, 0x00, 0x7C, 0x02, 0x37, 0x00, 0x05, 0x00, 0x28, 0x02, 0x28,
		0x00, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	for (i=0; i<sizeof(_CRTC_DEFAULT_); i++) {
		drv->vid->crtc[i] = _CRTC_DEFAULT_[i];
	}

	// BGRAMの未使用領域 0xFF 埋めしとかないと、リセット後になぜかファランクスOPのズームロゴがずれる
	memset((UINT8*)drv->vid->bgram+0x400, 0xFF, 0x400);
	memset((UINT8*)drv->vid->bgram+0x800, 0x00, 0x12);  // これも必要？
	memset((UINT8*)drv->vid->bgram+0x812, 0xFF, 0x800-0x012);

	// BGSPズレをクリア
	drv->vid->sp_ofs_x = 0;
	drv->vid->sp_ofs_y = 0;
	
	// システムポート#4（HRL）
	drv->sysport[4] = 0x00;

	// PPI
	X68000_PPI_W((void*)drv, 0xE9A005, 0x0B);  // ADPCM rate & pan

	// IRQハンドラ
	IRQ_Reset(drv->cpu);

	// タイミングパラメータ再計算
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
	// MUSASHIからのリセット命令ハンドラ
	// CPU以外をリセットする（CPUまでリセットすると、リセットコールバックが無限に飛んでくる）
	InitDevices(drv);
	X68OPM_Reset(drv->opm);
	X68ADPCM_Reset(drv->adpcm);
	X68MFP_Reset(drv->mfp);
	X68DMA_Reset(drv->dma);
	X68IOC_Reset(drv->ioc);
//	X68FDD_Reset(drv->fdd);  // ドライブはそのまま
	X68FDC_Reset(drv->fdc);
	X68RTC_Reset(drv->rtc);
	X68SCC_Reset(drv->scc);
	X68MIDI_Reset(drv->midi);
	X68SASI_Reset(drv->sasi);
}


// --------------------------------------------------------------------------
//   メモリマップ定義
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
	/*       アドレス範囲        RDポインタ       WRポインタ       RDハンドラ        WRハンドラ        ハンドラprm */
	 MEMMAP( 0xC00000, 0xDFFFFF, NULL,            NULL,            X68000_GVRAM_R,   X68000_GVRAM_W,   drv        );
	 MEMMAP( 0xE00000, 0xE7FFFF, drv->vid->tvram, NULL,            NULL,             X68000_TVRAM_W,   drv        );
	 MEMMAP( 0xE80000, 0xE81FFF, NULL,            NULL,            X68000_CRTC_R,    X68000_CRTC_W,    drv        );
	 MEMMAP( 0xE82000, 0xE83FFF, NULL,            NULL,            X68000_VCTRL_R,   X68000_VCTRL_W,   drv        );
	 MEMMAP( 0xE84000, 0xE85FFF, NULL,            NULL,            X68DMA_Read,      X68DMA_Write,     drv->dma   );
	 MEMMAP( 0xE86000, 0xE87FFF, NULL,            NULL,            NULL,             X68000_DUMMY_W,   drv        );  /* XXX Area set  リード側はバスエラー */
	 MEMMAP( 0xE88000, 0xE89FFF, NULL,            NULL,            X68MFP_Read,      X68MFP_Write,     drv->mfp   );
	 MEMMAP( 0xE8A000, 0xE8BFFF, NULL,            NULL,            X68RTC_Read,      X68RTC_Write,     drv->rtc   );
	 MEMMAP( 0xE8C000, 0xE8DFFF, NULL,            NULL,            X68000_DUMMY_R,   X68000_DUMMY_W,   drv        );  /* XXX Printer  W/O バスエラーは出ない */
	 MEMMAP( 0xE8E000, 0xE8FFFF, NULL,            NULL,            X68000_SYSPORT_R, X68000_SYSPORT_W, drv        );
	 MEMMAP( 0xE90000, 0xE91FFF, NULL,            NULL,            X68OPM_Read,      X68OPM_Write,     drv->opm   );
	 MEMMAP( 0xE92000, 0xE93FFF, NULL,            NULL,            X68ADPCM_Read,    X68ADPCM_Write,   drv->adpcm );
	 MEMMAP( 0xE94000, 0xE95FFF, NULL,            NULL,            X68FDC_Read,      X68FDC_Write,     drv->fdc   );
	 MEMMAP( 0xE96000, 0xE97FFF, NULL,            NULL,            X68SASI_Read,     X68SASI_Write,    drv->sasi  );
	 MEMMAP( 0xE98000, 0xE99FFF, NULL,            NULL,            X68SCC_Read,      X68SCC_Write,     drv->scc   );
	 MEMMAP( 0xE9A000, 0xE9BFFF, NULL,            NULL,            X68000_PPI_R,     X68000_PPI_W,     drv        );
	 MEMMAP( 0xE9C000, 0xE9DFFF, NULL,            NULL,            X68IOC_Read,      X68IOC_Write,     drv->ioc   );
	 MEMMAP( 0xEAE000, 0xEAFFFF, NULL,            NULL,            X68MIDI_Read,     X68MIDI_Write,    drv->midi  );  /* MIDI  $EAE000-EAFFFFだが、デコードされている箇所（0xEAFA00-0xEAFA0Fなど）以外はバスエラー */
	 MEMMAP( 0xEB0000, 0xEBFFFF, drv->vid->bgram, drv->vid->bgram, NULL,             X68000_BGRAM_W,   drv        );  /* BGズレ情報更新のために書き込み側はハンドラでもフック */
	 MEMMAP( 0xED0000, 0xED3FFF, NULL,            NULL,            X68000_SRAM_R,    X68000_SRAM_W,    drv        );
	 MEMMAP( 0xF00000, 0xFBFFFF, font_endian,     NULL,            NULL,             NULL,             NULL       );
	 MEMMAP( 0xFC0000, 0xFDFFFF, ipl_endian,      NULL,            NULL,             NULL,             NULL       );  /* SASIモデルはここでIPLのミラーが読める（コラムス） */
	 MEMMAP( 0xFE0000, 0xFFFFFF, ipl_endian,      NULL,            NULL,             NULL,             NULL       );
}


// --------------------------------------------------------------------------
//   システムドライバ定義
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
	// 基本的に13bitブロック（0x2000）単位でよい
	drv->mem = Mem16_Init((void*)drv, &X68000_ReadOpenBus, &X68000_WriteOpenBus);
	if ( !drv->mem ) {
		DRIVER_INIT_ERROR("Init Error : Memory Handler");
		break;
	}

	// CPU用にRAMは先にマッピングする
	SetupRamMap(drv);

	// 起動時の SP/PC を IPL からコピー
	memcpy(drv->ram, ipl_endian+0x010000, 8);

	// CPU
	drv->cpu = X68CPU_Init(_MAINTIMER_, drv->mem, X68000_CPU_CLK, X68CPU_68000, MAKESTATEID('C','P','U','1'));
	if ( !drv->cpu ) {
		DRIVER_INIT_ERROR("M68000 core : initialization failed");
	}
	DRIVER_ADDCPU(X68CPU_Exec, drv->cpu, MAKESTATEID('C','P','U','1'));
	X68CPU_SetIrqCallback(drv->cpu, &X68000_IrqVectorCb, (void*)drv);

	// リセット命令をフックしてデバイス類を初期化しないとIPLの「エラーが発生しました」が出る場合がある
	X68CPU_SetResetCallback(drv->cpu, &ResetInternal, (void*)drv);
	// IRQ6の遅延設定（X68000_SyncCb() 冒頭のコメント参照）
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

	// サウンドフィルタ
	drv->hpf_adpcm = SndFilter_Create(drv->drv.sound);
	drv->lpf_adpcm = SndFilter_Create(drv->drv.sound);
	drv->lpf_opm   = SndFilter_Create(drv->drv.sound);
	SndFilter_SetPrmHighPass(drv->hpf_adpcm,   115/*Hz*/, 0.7f/*Q*/);  // DCカット用（エトプリSE65の波形から。計算上は280Hzらしい？）
	SndFilter_SetPrmLowPass (drv->lpf_adpcm,  3700/*Hz*/, 0.7f/*Q*/);  // ローパス（標準理論値3.7kHzらしい、録音と比較しても大体よさげ）
	SndFilter_SetPrmLowPass (drv->lpf_opm,   16000/*Hz*/, 0.6f/*Q*/);  // ノイズ入力でのスペクトラムより、大体このくらい？
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

	// コントラスト段階変化用タイマ 変化があるときだけピリオド変えて起動する
	drv->tm_contrast = Timer_CreateItem(_MAINTIMER_, TIMER_NORMAL, TIMERPERIOD_NEVER, &X68000_ContrastCb, (void*)drv, 0, MAKESTATEID('C','T','T','M'));
	// キーバッファ補充用タイマ
	drv->tm_keybuf = Timer_CreateItem(_MAINTIMER_, TIMER_NORMAL, TIMERPERIOD_NEVER, &X68000_KeyBufCb, (void*)drv, 0, MAKESTATEID('K','B','T','M'));

	// その他のデバイス
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

	// デバイスが揃ったらメモリをマッピング
	SetupMemoryMap(drv);

	// 各種デバイス初期化
	InitDevices(drv);
	InitSRAM(drv);

	// SCCのコールバック
	X68SCC_SetMouseCallback(drv->scc, &X68000_MouseStatusCb, (void*)drv);

	// サウンドデバイス系コールバック
	X68OPM_SetIntCallback(drv->opm, &X68000_OpmIntCb, (void*)drv);
	X68OPM_SetPort(drv->opm, &X68000_OpmCtCb, (void*)drv);
	X68ADPCM_SetCallback(drv->adpcm, &X68000_AdpcmUpdateCb, (void*)drv);

	// DMA用READYコールバック
	// XXX #2 は拡張スロット用
	X68DMA_SetReadyCb(drv->dma, 0, &X68FDC_IsDataReady,   drv->fdc   );  // FDC
	X68DMA_SetReadyCb(drv->dma, 1, &X68SASI_IsDataReady,  drv->sasi  );  // SASI
	X68DMA_SetReadyCb(drv->dma, 3, &X68ADPCM_IsDataReady,  drv->adpcm);  // ADPCM

	DRIVER_INIT_END(X68k);
}

void X68kDriver_Cleanup(EMUDRIVER* __drv)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);

	// ディスクイメージは内部でメモリ確保してコピー取ってるので、それを破棄する必要がある
	// 割り込みなどで他デバイスを呼ぶ可能性があるので、デバイス破棄される前に最初に呼ぶ必要あり
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
	DRIVER_EXEC_END();  // こいつがタイマ駆動を呼んでます
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

	// スクリーンバッファのクリップ復元
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
//   追加のI/F群
// --------------------------------------------------------------------------
// 本体リセット
void X68kDriver_Reset(EMUDRIVER* __drv)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	// デバイスリセット
	ResetInternal(drv);
	// 起動時の SP/PC を IPL からコピー
	memcpy(drv->ram, ipl_endian+0x010000, 8);
	// CPUリセット
	X68CPU_Reset(drv->cpu);
}

// クロック切り替え
void X68kDriver_SetCpuClock(EMUDRIVER* __drv, UINT32 clk_idx)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	if ( clk_idx < X68K_CLK_INDEX_MAX ) {
		SetCpuClock(drv, clk_idx);
		drv->cpu->freq = drv->cpu_clk;
		drv->t.update = TRUE;  // タイミングパラメータ更新
	}
}

// RAMサイズ変更
void X68kDriver_SetMemorySize(EMUDRIVER* __drv, UINT32 sz_mb)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);

	if ( sz_mb < 2 ) sz_mb = 2;
	if ( sz_mb > 12 ) sz_mb = 12;

	drv->ram_size = sz_mb << 20;
	SetupRamMap(drv);
}

// SRAM取得
UINT8* X68kDriver_GetSramPtr(EMUDRIVER* __drv, UINT32* p_sz)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER) NULL;
	if ( p_sz ) *p_sz = sizeof(drv->sram);
	return drv->sram;
}

// 書き込みの起こったフロッピーディスクがイジェクトされる際呼ばれるコールバックを登録
void X68kDriver_SetEjectCallback(EMUDRIVER* __drv, DISKEJECTCB cb, void* cbprm)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	X68FDD_SetEjectCallback(drv->fdd, cb, cbprm);
}

// フロッピーディスクのドライブへの挿入
void X68kDriver_SetDisk(EMUDRIVER* __drv, UINT32 drive, const UINT8* image, UINT32 image_sz, X68K_DISK_TYPE type, BOOL wr_protect)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	// 先にイジェクトを発行
	X68FDD_EjectDisk(drv->fdd, drive, TRUE);  // XXX 強制排出
	// XXX イジェクト即挿入を行うと割り込みエラーになる可能性がある（FDD側に割り込み遅延を入れるのが良い？）
	//     もしくはドライバ側で挿入を遅らせる？（イメージの管理問題の関係上、割り込み遅延が現実的）
	X68FDD_SetDisk(drv->fdd, drive, image, image_sz, type);
	X68FDD_SetWriteProtect(drv->fdd, drive, wr_protect);
}

// フロッピーディスクのドライブからの取り出し
void X68kDriver_EjectDisk(EMUDRIVER* __drv, UINT32 drive, BOOL force)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	X68FDD_EjectDisk(drv->fdd, drive, force);
}

// 挿入中のフロッピーディスクのイメージデータを取得（データが書き込まれたディスクを保存したい場合など用）
// XXX 現行ではイジェクト時（アプリ終了時含む）にコールバックが飛んでくるので、自主的に取りに行く必要はないはず
const UINT8* X68kDriver_GetDiskImage(EMUDRIVER* __drv, UINT32 drive, UINT32* p_imagesz)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER) NULL;
	return X68FDD_GetDiskImage(drv->fdd, drive, p_imagesz);
}

// フロッピードライブのLED情報構造体へのポインタを得る（上位層でFDDアクセス表示などを実装したい場合に使う）
const INFO_X68FDD_LED* X68kDriver_GetDriveLED(EMUDRIVER* __drv)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER) NULL;
	return X68FDD_GetInfoLED(drv->fdd);
}

// フロッピーディスクアクセスの高速化設定
void X68kDriver_SetFastFddAccess(EMUDRIVER* __drv, BOOL fast)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	drv->fast_fdd = fast;
}

// HDDドライブのLED情報を得る
X68FDD_LED_STATE X68kDriver_GetHddLED(EMUDRIVER* __drv)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER) X68FDD_LED_OFF;
	return X68SASI_GetLedState(drv->sasi);
}

// キー入力（仮）
void X68kDriver_KeyInput(EMUDRIVER* __drv, UINT32 key)
{
	// XXX 本来はキーボードデバイスを別途用意して、キーリピートなどの管理もそちらで行うべき
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	UINT32 sw = ( ( key >> 7 ) & 1 ) ^ 1;  // Press/Release
	UINT32 i = ( key >> 5 ) & 3;
	UINT32 bit = key & 31;
	drv->keybuf.key_req[i] &= ~(1 << bit);
	drv->keybuf.key_req[i] |= sw << bit;
	if ( drv->keybuf.key_req[i] != drv->keybuf.key_state[i] ) {
		if ( Timer_GetPeriod(drv->tm_keybuf) == TIMERPERIOD_NEVER ) {
			Timer_ChangePeriod(_MAINTIMER_, drv->tm_keybuf, TIMERPERIOD_HZ(240));  // 2400bpsなのでこれくらい？
		}
	}
}
void X68kDriver_KeyClear(EMUDRIVER* __drv)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	memset(drv->keybuf.key_req, 0, sizeof(drv->keybuf.key_req));
	if ( Timer_GetPeriod(drv->tm_keybuf) == TIMERPERIOD_NEVER ) {
		Timer_ChangePeriod(_MAINTIMER_, drv->tm_keybuf, TIMERPERIOD_HZ(240));  // 2400bpsなのでこれくらい？
	}
}

// ジョイパッド入力
void X68kDriver_JoyInput(EMUDRIVER* __drv, UINT32 joy1, UINT32 joy2)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	drv->joyport[0] = joy1;
	drv->joyport[1] = joy2;
}

// マウス入力
void X68kDriver_MouseInput(EMUDRIVER* __drv, SINT32 dx, SINT32 dy, UINT32 btn)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	drv->mouse.x += dx;
	drv->mouse.y += dy;
	drv->mouse.stat = btn;
	// 適当にリミットしておく（ここまでは行かないと思うが）
	drv->mouse.x = NUMLIMIT(drv->mouse.x, -65535, 65535);
	drv->mouse.y = NUMLIMIT(drv->mouse.y, -65535, 65535);
}

// CRTCタイミング取得
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
	// XXX フィルタ値直指定できるI/Fの方が自由度は高いが、そこまで必要かね…？
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	static const float X68OPM_FILTER[][2]   = { { 0.0f, 0.0f }, { 16000/*Hz*/, 0.6f/*Q*/ } };
	static const float ADPCM_FILTER[][2] = { { 0.0f, 0.0f }, {  3700/*Hz*/, 0.7f/*Q*/ }, { 16000/*Hz*/, 0.7f/*Q*/ }  };  // HQはほぼフルレンジ出て、且つ折り返しノイズを絞れる辺りに設定

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

// MIDI送信コールバック登録
void X68kDriver_SetMidiCallback(EMUDRIVER* __drv, MIDIFUNCCB func, void* cbprm)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	X68MIDI_SetCallback(drv->midi, func, cbprm);
}

// SASIファイルアクセスコールバック登録
void X68kDriver_SetSasiCallback(EMUDRIVER* __drv, SASIFUNCCB func, void* cbprm)
{
	DRIVER_CHECK_STRUCT(X68000_DRIVER);
	X68SASI_SetCallback(drv->sasi, func, cbprm);
}
