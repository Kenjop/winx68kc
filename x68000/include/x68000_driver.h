/* -----------------------------------------------------------------------------------
  "SHARP X68000" System Driver
                                                      (c) 2005-11 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_driver_h_
#define _x68000_driver_h_

#include "emu_driver.h"

// --------------------------------------------------------------------------
//   定数類類
// --------------------------------------------------------------------------
#define X68K_BASE_SCREEN_W  768
#define X68K_BASE_SCREEN_H  512


// --------------------------------------------------------------------------
//   起動時オプション類
// --------------------------------------------------------------------------
#define X68K_BOOT_PARAMS_SHIFT_CLK   0

enum {
	X68K_CLK_10MHZ = 0,
	X68K_CLK_16MHZ,
	X68K_CLK_24MHZ,
	X68K_CLK_INDEX_MAX  // インデックス数取得用、選択不可
};


// --------------------------------------------------------------------------
//   ディスクイメージタイプ
// --------------------------------------------------------------------------
// XXX 現状 XDF と DIM（不完全な可能性あり）しか対応していない
typedef enum {
	X68K_DISK_XDF = 0,
	X68K_DISK_DIM,
	X68K_DISK_D88,
	X68K_DISK_MAX
} X68K_DISK_TYPE;

typedef void (CALLBACK *DISKEJECTCB)(void* prm, UINT32 drive, const UINT8* p_image, UINT32 image_sz);


// --------------------------------------------------------------------------
//   FDD の LED 情報
// --------------------------------------------------------------------------
typedef enum {
	X68FDD_LED_OFF = 0,
	X68FDD_LED_GREEN,
	X68FDD_LED_RED
} X68FDD_LED_STATE;

typedef struct {
	X68FDD_LED_STATE  access[2];   // アクセスランプ状態（Drive#0/#1）
	X68FDD_LED_STATE  eject[2];    // イジェクトボタンのランプ状態（Drive#0/#1）
	BOOL              inserted[2]; // ディスクが入ってるかどうか
} INFO_X68FDD_LED;


// --------------------------------------------------------------------------
//   キーコード
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
	X68K_KEY_MINUS,     // フルキーの -
	X68K_KEY_EXP,       // ^
	X68K_KEY_YEN,       // 円記号・バックスラッシュ
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
	X68K_KEY_ENTER,     // フルキーの Enter
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
	X68K_KEY_BACKSLASH, // バックスラッシュ・アンダーバー（キーボード右下）
	X68K_KEY_SPACE,     // スペースバー
	X68K_KEY_HOME,
	X68K_KEY_DEL,
	X68K_KEY_ROLLUP,
	X68K_KEY_ROLLDOWN,
	X68K_KEY_UNDO,
	X68K_KEY_LEFT,      // カーソルキー ←
	X68K_KEY_UP,        // カーソルキー ↑
	X68K_KEY_RIGHT,     // カーソルキー →
	X68K_KEY_DOWN,      // カーソルキー ↓
	X68K_KEY_CLR,
	// $40
	X68K_KEY_NUMDIV,    // テンキー /
	X68K_KEY_NUMMUL,    // テンキー *
	X68K_KEY_NUMMINUS,  // テンキー -
	X68K_KEY_NUM7,      // テンキー 7
	X68K_KEY_NUM8,      // テンキー 8
	X68K_KEY_NUM9,      // テンキー 9
	X68K_KEY_NUMPLUS,   // テンキー +
	X68K_KEY_NUM4,      // テンキー 4
	X68K_KEY_NUM5,      // テンキー 5
	X68K_KEY_NUM6,      // テンキー 6
	X68K_KEY_NUMEQUAL,  // テンキー =
	X68K_KEY_NUM1,      // テンキー 1
	X68K_KEY_NUM2,      // テンキー 2
	X68K_KEY_NUM3,      // テンキー 3
	X68K_KEY_NUMENTER,  // テンキー Enter
	X68K_KEY_NUM0,      // テンキー 0
	// $50
	X68K_KEY_NUMCOMMA,  // テンキー ,
	X68K_KEY_NUMPERIOD, // テンキー .
	X68K_KEY_KIGOU,     // 「記号入力」
	X68K_KEY_TOUROKU,   // 「登録」
	X68K_KEY_HELP,
	X68K_KEY_XF1,
	X68K_KEY_XF2,
	X68K_KEY_XF3,
	X68K_KEY_XF4,
	X68K_KEY_XF5,
	X68K_KEY_KANA,      // 「かな」
	X68K_KEY_ROMAJI,    // 「ローマ字」
	X68K_KEY_CODEIN,    // 「コード入力」 Inside X68000 の「カナ入力」記述はミス
	X68K_KEY_CAPS,
	X68K_KEY_INS,
	X68K_KEY_HIRAGANA,  // 「ひらがな」
	// $60
	X68K_KEY_ZENKAKU,   // 「全角」
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
	X68K_KEY_UNUSED_6D, // このコードを返すキーはない
	X68K_KEY_UNUSED_6E, // このコードを返すキーはない
	X68K_KEY_UNUSED_6F, // このコードを返すキーはない
	// $70
	X68K_KEY_SHIFT,     // 右SHIFTも同じ（左SHIFTと区別なし）
	X68K_KEY_CTRL,
	X68K_KEY_OPT1,
	X68K_KEY_OPT2,
	X68K_KEY_UNUSED_74, // このコードを返すキーはない
	X68K_KEY_UNUSED_75, // このコードを返すキーはない
	X68K_KEY_UNUSED_76, // このコードを返すキーはない
	X68K_KEY_UNUSED_77, // このコードを返すキーはない
	X68K_KEY_UNUSED_78, // このコードを返すキーはない
	X68K_KEY_UNUSED_79, // このコードを返すキーはない
	X68K_KEY_UNUSED_7A, // このコードを返すキーはない
	X68K_KEY_UNUSED_7B, // このコードを返すキーはない
	X68K_KEY_UNUSED_7C, // このコードを返すキーはない
	X68K_KEY_UNUSED_7D, // このコードを返すキーはない
	X68K_KEY_UNUSED_7E, // このコードを返すキーはない
	X68K_KEY_UNUSED_7F, // このコードを返すキーはない

	X68K_KEY_MAX_CODE
};

// キーを押したのか離したのかのフラグ（キーコードと OR で渡す）
#define X68K_KEYFLAG_PRESS    0x00
#define X68K_KEYFLAG_RELEASE  0x80


// --------------------------------------------------------------------------
//   ジョイスティック情報
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
//   マウスボタン情報
// --------------------------------------------------------------------------
enum {
	X68K_MOUSE_BTN_L = 0x01,
	X68K_MOUSE_BTN_R = 0x02
};


// --------------------------------------------------------------------------
//   描画範囲情報
// --------------------------------------------------------------------------
typedef struct {
	SINT32 x1, y1;
	SINT32 x2, y2;
} ST_RECT;

typedef struct {
	ST_RECT  scrn;  // それっぽく求めたCRTの表示可能領域
	ST_RECT  disp;  // 現在のCRTC設定での実際の表示領域
} ST_DISPAREA;


// --------------------------------------------------------------------------
//   描画範囲情報
// --------------------------------------------------------------------------
typedef enum {
	X68K_SOUND_OPM = 0,
	X68K_SOUND_ADPCM,

	X68K_SOUND_MAX
} X68K_SOUND_DEVICE;


// --------------------------------------------------------------------------
//   MIDI関連
// --------------------------------------------------------------------------
typedef enum {
	MIDIFUNC_RESET = 0,
	MIDIFUNC_DATAOUT,
} MIDI_FUNCTIONS;

typedef void (CALLBACK *MIDIFUNCCB)(void* prm, MIDI_FUNCTIONS func, UINT8 data);


// --------------------------------------------------------------------------
//   MIDI関連
// --------------------------------------------------------------------------
typedef enum {
	SASIFUNC_IS_READY = 0,
	SASIFUNC_READ,
	SASIFUNC_WRITE,
} SASI_FUNCTIONS;

typedef BOOL (CALLBACK *SASIFUNCCB)(void* prm, SASI_FUNCTIONS func, UINT32 devid, UINT32 pos, UINT8* data, UINT32 size);


// --------------------------------------------------------------------------
//   エミュI/F定義
// --------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

// 基本I/F
EMUDRIVER* X68kDriver_Initialize(const UINT8* rom_ipl, const UINT8* rom_font, UINT32 sndfreq);
void X68kDriver_Cleanup(EMUDRIVER* __drv);
UINT32 X68kDriver_Exec(EMUDRIVER* __drv, TUNIT period);
void X68kDriver_LoadState(EMUDRIVER* __drv, STATE* state);
void X68kDriver_SaveState(EMUDRIVER* __drv, STATE* state);

// 本体リセット
void X68kDriver_Reset(EMUDRIVER* __drv);

// クロック切り替え
void X68kDriver_SetCpuClock(EMUDRIVER* __drv, UINT32 clk_idx);

// RAMサイズ変更
void X68kDriver_SetMemorySize(EMUDRIVER* __drv, UINT32 sz_mb);

// SRAM取得
UINT8* X68kDriver_GetSramPtr(EMUDRIVER* __drv, UINT32* p_sz);

// 書き込みの起こったフロッピーディスクがイジェクトされる際呼ばれるコールバックを登録
void X68kDriver_SetEjectCallback(EMUDRIVER* prm, DISKEJECTCB cb, void* cbprm);

// フロッピーディスクのドライブへの挿入
void X68kDriver_SetDisk(EMUDRIVER* prm, UINT32 drive, const UINT8* image, UINT32 image_sz, X68K_DISK_TYPE type, BOOL wr_protect);

// フロッピーディスクのドライブからの取り出し（force=TRUE の時はイジェクト禁止設定を無視して強制排出）
void X68kDriver_EjectDisk(EMUDRIVER* prm, UINT32 drive, BOOL force);

// フロッピーディスクのイメージデータを取得（データが書き込まれたディスクを保存したい場合など用）
// XXX 現行ではイジェクト時（アプリ終了時含む）にコールバックが飛んでくるので、自主的に取りに行く必要はないはず
const UINT8* X68kDriver_GetDiskImage(EMUDRIVER* prm, UINT32 drive, UINT32* p_imagesz);

// フロッピードライブのLED情報構造体へのポインタを得る（上位層でFDDアクセス表示などを実装したい場合に使う）
const INFO_X68FDD_LED* X68kDriver_GetDriveLED(EMUDRIVER* prm);

// フロッピーディスクアクセスの高速化設定
void X68kDriver_SetFastFddAccess(EMUDRIVER* prm, BOOL fast);

// HDDドライブのLED情報を得る
X68FDD_LED_STATE X68kDriver_GetHddLED(EMUDRIVER* prm);

// キー入力（仮）
void X68kDriver_KeyInput(EMUDRIVER* __drv, UINT32 key);
void X68kDriver_KeyClear(EMUDRIVER* __drv);

// ジョイパッド入力
void X68kDriver_JoyInput(EMUDRIVER* __drv, UINT32 joy1, UINT32 joy2);

// マウス入力
void X68kDriver_MouseInput(EMUDRIVER* __drv, SINT32 dx, SINT32 dy, UINT32 btn);

// CRTCベースの描画情報取得
BOOL X68kDriver_GetDrawInfo(EMUDRIVER* __drv, ST_DISPAREA* area);

// CRT周波数情報取得
float X68kDriver_GetHSyncFreq(EMUDRIVER* __drv);
float X68kDriver_GetVSyncFreq(EMUDRIVER* __drv);

// サウンド設定
void X68kDriver_SetVolume(EMUDRIVER* __drv, X68K_SOUND_DEVICE device, float db);
void X68kDriver_SetFilter(EMUDRIVER* __drv, X68K_SOUND_DEVICE device, UINT32 filter_idx);

// MIDI送信コールバック登録
void X68kDriver_SetMidiCallback(EMUDRIVER* __drv, MIDIFUNCCB func, void* cbprm);

// SASIファイルアクセスコールバック登録
void X68kDriver_SetSasiCallback(EMUDRIVER* __drv, SASIFUNCCB func, void* cbprm);

#ifdef __cplusplus
}
#endif


#endif // of _x68000_driver_h_
