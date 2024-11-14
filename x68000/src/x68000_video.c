/* -----------------------------------------------------------------------------------
  "SHARP X68000" Video Driver
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "osconfig.h"
#include "x68000_driver.h"
#include "x68000_video.h"

/*
	Vctrl2 で EXON=1 且つ B/P=0 に設定した際の挙動検証不足（複雑すぎる）
*/

/*
	MIX処理で使用するラインバッファ構造：
	*****--- ****---- -------- --------  空き（将来的に必要になった場合使う）
	-----PPP -------- -------- --------  SP/BGの合成用内部プライオリティ
	-------- ----E--- -------- --------  EXフラグ（半透明・特殊PRIの対象ピクセルフラグ）
	-------- -----S-- -------- --------  SP面フラグ（GR面のマスク、及び半透明対象判定で使う）
	-------- ------T- -------- --------  TX面フラグ（GR面のマスク、及び半透明対象判定で使う）
	-------- -------X -------- --------  TX#0番カラーフラグ（バックドロップ処理用）
	-------- -------- CCCCCCCC CCCCCCCC  ピクセルカラー（パレット参照後の16bit）
*/

// --------------------------------------------------------------------------
//   定数定義類
// --------------------------------------------------------------------------
#define VID_SHIFT_EX   (19)
#define VID_SHIFT_SP   (18)
#define VID_SHIFT_TX   (17)
#define VID_SHIFT_TX0  (16)

#define VID_PLANE_EX   (1<<VID_SHIFT_EX)
#define VID_PLANE_SP   (1<<VID_SHIFT_SP)
#define VID_PLANE_TX   (1<<VID_SHIFT_TX)
#define VID_PLANE_TX0  (1<<VID_SHIFT_TX0)

#define VID_SP_PRI_SFT (24)
#define VID_SP_PRI_MAX (7<<VID_SP_PRI_SFT)
#define VID_SP_PRI_BGL (3<<VID_SP_PRI_SFT)
#define VID_SP_PRI_BGH (5<<VID_SP_PRI_SFT)

#define LINEBUF_OFS    16  // 処理を簡略化するためのパディング
#define CRTC(r)        (READBEWORD(&vid->crtc[(r)*2]))

// BGRI5551 2値の加算半透明
#define ADD_TRANS_PIXEL(_a,_b)  ( ( ( ((_a)&~0xFFFF0842) + ((_b)&~0xFFFF0842) ) >> 1 ) + ( ( (_a) & (_b) ) & 0x0842 ) )


// --------------------------------------------------------------------------
//   内部使用のstatic変数（ステート保存不要なもの）
// --------------------------------------------------------------------------
// レイヤごとの一時出力ラインバッファ（最大1024ドット＋左右16ドットパディングを確保）
static UINT32 LINEBUF_GR[1024+LINEBUF_OFS*2];
static UINT32 LINEBUF_TX[1024+LINEBUF_OFS*2];

// ラインバッファ展開において、GR ラインバッファの一番下のプレーンかどうか
static BOOL GR_OPAQ = FALSE;

// ラインバッファ展開において、TX ラインバッファの一番下のプレーンかどうか
static BOOL TX_OPAQ = FALSE;

// MIX関数テーブルへのポインタ
static void (FASTCALL **X68K_MIX_FUNC)(X68000_VIDEO*,const UINT32) = NULL;


// --------------------------------------------------------------------------
//   内部使用のstatic変数（ステート保存必要）
// --------------------------------------------------------------------------
// ライン描画結果をため込む仮想スクリーン（ティアリング回避用）
static UINT16  VSCR[ X68_MAX_VSCR_WIDTH * X68_MAX_VSCR_HEIGHT ];


// --------------------------------------------------------------------------
//   ライン合成マクロ
// --------------------------------------------------------------------------
#include "x68000_mix_normal.inc"  // 通常
#include "x68000_mix_sppri.inc"   // 特殊プライオリティ
#include "x68000_mix_trans.inc"   // 半透明
#include "x68000_mix_trtx0.inc"   // 半透明（TX0）

static void (FASTCALL **MIX_FUNC_TABLE[4])(X68000_VIDEO*,const UINT32) = {
	MIX_N, MIX_S, MIX_T, MIX_0
};

enum {  // 上の並びと一致させること
	MIX_FUNC_IDX_N = 0,
	MIX_FUNC_IDX_S,
	MIX_FUNC_IDX_T,
	MIX_FUNC_IDX_0,
};


// --------------------------------------------------------------------------
//   テーブル
// --------------------------------------------------------------------------
// GRBI5551→ARGB8888 変換用（輝度ごとに16段階用意）
static UINT32 PAL2RGB[16][0x10000];

// Planed→Packed 変換用（TX展開用、テーブル使う方が若干速い）
static UINT32 PLANE2PACK[0x100];

static void SetupTables(void)
{
	UINT32 c;
	UINT32 contrast;
	for (contrast=0; contrast<16; contrast++) {
		for (c=0x0000; c<=0xFFFF; c++) {
			UINT32 i = c&1;
			UINT32 g = ( ( (c&0xF800) >> 11 ) << 1 ) | i;
			UINT32 r = ( ( (c&0x07C0) >>  6 ) << 1 ) | i;
			UINT32 b = ( ( (c&0x003E) >>  1 ) << 1 ) | i;
			g = ((g*255)*contrast) / (63*15);
			r = ((r*255)*contrast) / (63*15);
			b = ((b*255)*contrast) / (63*15);
			PAL2RGB[contrast][c] = 0xFF000000 | (r<<16) | (g<<8) | (b<<0);
		}
	}

	for (c=0x00; c<=0xFF; c++) {
		UINT32 i;
		UINT32 out = 0;
		for (i=0; i<8; i++) {
			out <<= 4;
			if ( c & (1<<i) ) out |= 1;
		}
		PLANE2PACK[c] = out;
	}
}


// --------------------------------------------------------------------------
//   テキスト面
// --------------------------------------------------------------------------
static void TxLineUpdate(X68000_VIDEO* vid, UINT32 line)
{
	// dirty が立っているラインの、planed -> packed 変換（planed は扱いづらいので）
	const UINT32 pf = 1 << (line&31);
	UINT32* pd = &vid->tx_dirty[line>>5];

	if ( pd[0] & pf )  // Dirtyなら
	{
		const UINT16* src = vid->tvram + (line<<6);  // 64 words / line
		UINT32* dst = vid->tx_buf + (line<<7); // 128 dwords / line
		UINT32 w = 1024/16;

		do {
			// 16bit幅で最上位が画面左
			const UINT32 s0 = src[0x00000];  // UINT16* なので、1プレーン辺り 64k unit
			const UINT32 s1 = src[0x10000];
			const UINT32 s2 = src[0x20000];
			const UINT32 s3 = src[0x30000];
			UINT32 o;
			/*
				メモリアクセス増えてもテーブル使う方が速いっぽい
				64bitテーブルで16bit分纏めて処理するのはあまり効果なし（x64環境でのチェック、Win32環境では遅くなるかも）
				よって32bitテーブルを2回（x 4プレーン）参照する形にしておく
			*/
#if 0
			o = 0;   o |= ((s0&0x8000)<<13) | ((s1&0x8000)<<14) | ((s2&0x8000)<<15) | ((s3&0x8000)<<16);
			o >>= 4; o |= ((s0&0x4000)<<14) | ((s1&0x4000)<<15) | ((s2&0x4000)<<16) | ((s3&0x4000)<<17);
			o >>= 4; o |= ((s0&0x2000)<<15) | ((s1&0x2000)<<16) | ((s2&0x2000)<<17) | ((s3&0x2000)<<18);
			o >>= 4; o |= ((s0&0x1000)<<16) | ((s1&0x1000)<<17) | ((s2&0x1000)<<18) | ((s3&0x1000)<<19);
			o >>= 4; o |= ((s0&0x0800)<<17) | ((s1&0x0800)<<18) | ((s2&0x0800)<<19) | ((s3&0x0800)<<20);
			o >>= 4; o |= ((s0&0x0400)<<18) | ((s1&0x0400)<<19) | ((s2&0x0400)<<20) | ((s3&0x0400)<<21);
			o >>= 4; o |= ((s0&0x0200)<<19) | ((s1&0x0200)<<20) | ((s2&0x0200)<<21) | ((s3&0x0200)<<22);
			o >>= 4; o |= ((s0&0x0100)<<20) | ((s1&0x0100)<<21) | ((s2&0x0100)<<22) | ((s3&0x0100)<<23);
			*dst++ = o;
			o = 0;   o |= ((s0&0x0080)<<21) | ((s1&0x0080)<<22) | ((s2&0x0080)<<23) | ((s3&0x0080)<<24);
			o >>= 4; o |= ((s0&0x0040)<<22) | ((s1&0x0040)<<23) | ((s2&0x0040)<<24) | ((s3&0x0040)<<25);
			o >>= 4; o |= ((s0&0x0020)<<23) | ((s1&0x0020)<<24) | ((s2&0x0020)<<25) | ((s3&0x0020)<<26);
			o >>= 4; o |= ((s0&0x0010)<<24) | ((s1&0x0010)<<25) | ((s2&0x0010)<<26) | ((s3&0x0010)<<27);
			o >>= 4; o |= ((s0&0x0008)<<25) | ((s1&0x0008)<<26) | ((s2&0x0008)<<27) | ((s3&0x0008)<<28);
			o >>= 4; o |= ((s0&0x0004)<<26) | ((s1&0x0004)<<27) | ((s2&0x0004)<<28) | ((s3&0x0004)<<29);
			o >>= 4; o |= ((s0&0x0002)<<27) | ((s1&0x0002)<<28) | ((s2&0x0002)<<29) | ((s3&0x0002)<<30);
			o >>= 4; o |= ((s0&0x0001)<<28) | ((s1&0x0001)<<29) | ((s2&0x0001)<<30) | ((s3&0x0001)<<31);
			*dst++ = o;
#else
			o = ( PLANE2PACK[s0 >> 8] << 0 ) | ( PLANE2PACK[s1 >> 8] << 1 ) | ( PLANE2PACK[s2 >> 8] << 2 ) | ( PLANE2PACK[s3 >> 8] << 3 );
			*dst++ = o;
			o = ( PLANE2PACK[s0&0xFF] << 0 ) | ( PLANE2PACK[s1&0xFF] << 1 ) | ( PLANE2PACK[s2&0xFF] << 2 ) | ( PLANE2PACK[s3&0xFF] << 3 );
			*dst++ = o;
#endif
			src++;
		} while ( --w );

		// Dirtyを落とす
		pd[0] &= ~pf;
	}
}

static void TxDrawLine(X68000_VIDEO* vid, UINT32 line)
{
	const UINT32 vctrl2 = vid->vctrl2;
	if ( vctrl2 & 0x0020 ) {
		// スクロール値を参照して、画面ライン→VRAMラインを計算
		const UINT32 scrx = CRTC(10);
		const UINT32 scry = CRTC(11);
		const UINT32 vram_line = ( scry + line ) & 0x3FF;

		// VRAMの該当ラインがdirtyなら更新
		TxLineUpdate(vid, vram_line);

		// TXパレット（レイヤフラグ付き）更新
		// BGも同じ処理すべきかもだけど、あっちはプライオリティもあるのであまり効果ないかも
		if ( vid->txpal_dirty ) {
			const UINT16* org = vid->pal + 0x100;  // TX/SP pal は pal[0x100] ～
			UINT32* txpal = vid->txpal;
			UINT32 i;
			*txpal++ = ( *org++ ) | VID_PLANE_TX0;
			for (i=1; i<16; i++) {
				*txpal++ = ( *org++ ) | VID_PLANE_TX;
			}
			vid->txpal_dirty = FALSE;
		}

		{
			const UINT32* pal = vid->txpal;
			const SINT32 w = vid->h_sz;
			const UINT32* src = vid->tx_buf + (vram_line<<7);
			SINT32 dx = -(SINT32)(scrx&7);
			SINT32 sx = (scrx>>3) & 0x7F;
			UINT32* dst = LINEBUF_TX + LINEBUF_OFS + dx;
			if ( TX_OPAQ ) {
				for ( ; dx<w; dx+=8, sx=(sx+1)&0x7F) {
					// レイヤフラグは上のパレットに組み込み済みなのでそのまま出力する
					UINT32 pix = src[sx];
					{ const UINT32 c = pix&15; *dst = pal[c]; dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; *dst = pal[c]; dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; *dst = pal[c]; dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; *dst = pal[c]; dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; *dst = pal[c]; dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; *dst = pal[c]; dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; *dst = pal[c]; dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; *dst = pal[c]; dst++;            }
				}
				TX_OPAQ = FALSE;
			} else {
				for ( ; dx<w; dx+=8, sx=(sx+1)&0x7F) {
					UINT32 pix = src[sx];
					{ const UINT32 c = pix&15; if ( c ) { *dst = pal[c]; } dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; if ( c ) { *dst = pal[c]; } dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; if ( c ) { *dst = pal[c]; } dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; if ( c ) { *dst = pal[c]; } dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; if ( c ) { *dst = pal[c]; } dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; if ( c ) { *dst = pal[c]; } dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; if ( c ) { *dst = pal[c]; } dst++; pix >>= 4; }
					{ const UINT32 c = pix&15; if ( c ) { *dst = pal[c]; } dst++;            }
				}
			}
		}
	}
}


// --------------------------------------------------------------------------
//   BG/SPR面
// --------------------------------------------------------------------------
// バックドロップカラーも半透明対象なので、抜き色描画の際には VID_PLANE_SP を立てておく必要あり
// また、抜き色の対象が TX0 の場合は自身の色と差し替える
#define DRAW_BGCHIP \
	{ const UINT32 c = (data>>28)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>24)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>20)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>16)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>12)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>> 8)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>> 4)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>> 0)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; }

#define DRAW_BGCHIP_FLIP \
	{ const UINT32 c = (data>> 0)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>> 4)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>> 8)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>12)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>16)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>20)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>24)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>28)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else if ( *dst & VID_PLANE_TX0 ) { *dst = pal[c] | VID_PLANE_SP; } dst++; }

#define DRAW_BGCHIP_OPAQ \
	{ const UINT32 c = (data>>28)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>24)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>20)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>16)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>12)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>> 8)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>> 4)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>> 0)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; }

#define DRAW_BGCHIP_OPAQ_FLIP \
	{ const UINT32 c = (data>> 0)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>> 4)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>> 8)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>12)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>16)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>20)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>24)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; } \
	{ const UINT32 c = (data>>28)&0x0F; if ( c ) { *dst = pal[c] | base_pri; } else { *dst = pal[c] | VID_PLANE_SP; } dst++; }

static void BgDrawLine256(X68000_VIDEO* vid, UINT32 plane, UINT32 line)
{
	// 256ドットモードBG
	// BGもスプライト同様、パレット参照後の色のみで抜き色判定する（ドラキュラ 入城直後）
	const UINT16* palbase = vid->pal + 0x100;
	const UINT32 bgarea = ( ( vid->bgram[0x808/2] >> (plane*3+1) ) & 1 ) << 12;  // 1ブロック 0x1000 WORDS
	const UINT16* bgram = vid->bgram + (0x0C000/2) + bgarea;
	const UINT16* chrram = vid->bgram + (0x08000/2);
	const UINT32 scrx = vid->bgram[(0x800/2)+(plane<<1)] - vid->sp_ofs_x;
	const UINT32 scry = vid->bgram[(0x802/2)+(plane<<1)] - vid->sp_ofs_y;
	const UINT32 vram_line = ( scry + line ) & 0x1FF;
	const SINT32 w = vid->h_sz;
	const UINT32 src_yofs = ( (vram_line) & 7 ) << 1;
	const UINT16* src = bgram + ((vram_line>>3)<<6);    // 0x40 chars / line
	const UINT32 base_pri = VID_PLANE_SP | ( ( plane ) ? VID_SP_PRI_BGL : VID_SP_PRI_BGH );
	SINT32 dx = -(SINT32)(scrx&7);
	SINT32 sx = (scrx>>3) & 0x3F;
	UINT32* dst = LINEBUF_TX + LINEBUF_OFS + dx;

	if ( TX_OPAQ ) {
		// OPAQの場合、Index#0のピクセルも（パレット含め）描く（スーパーハングオン）
		for ( ; dx<w; dx+=8, sx=(sx+1)&0x3F) {
			const UINT32 tile = src[sx];
			const UINT16* pchr = chrram + ((tile&0x00FF)<<4);  // 0x10 WORDS / char
			const UINT16* pal = palbase + ( (tile&0x0F00) >> 4 );
			UINT32 data;
			pchr += ( tile & 0x8000 ) ? ((7<<1)-src_yofs) : src_yofs;  // V flip
			data = (pchr[0]<<16) | pchr[1];
			if ( !( tile & 0x4000 ) ) {
				// H normal
				DRAW_BGCHIP_OPAQ;
			} else {
				// H flip
				DRAW_BGCHIP_OPAQ_FLIP;
			}
		}
		TX_OPAQ = FALSE;
	} else {
		for ( ; dx<w; dx+=8, sx=(sx+1)&0x3F) {
			const UINT32 tile = src[sx];
			const UINT16* pchr = chrram + ((tile&0x00FF)<<4);  // 0x10 WORDS / char
			const UINT16* pal = palbase + ( (tile&0x0F00) >> 4 );
			UINT32 data;
			pchr += ( tile & 0x8000 ) ? ((7<<1)-src_yofs) : src_yofs;  // V flip
			data = (pchr[0]<<16) | pchr[1];
			if ( !( tile & 0x4000 ) ) {
				// H normal
				DRAW_BGCHIP;
			} else {
				// H flip
				DRAW_BGCHIP_FLIP;
			}
		}
	}
}

static void BgDrawLine512(X68000_VIDEO* vid, UINT32 line)
{
	// 512ドットモードBG
	// ファンタジーゾーンのタイトルなど
	const UINT16* palbase = vid->pal + 0x100;
	const UINT32 bgarea = ( ( vid->bgram[0x808/2] >> 1 ) & 1 ) << 12;  // 1ブロック 0x1000 WORDS
	const UINT16* bgram = vid->bgram + (0x0C000/2) + bgarea;
	const UINT16* chrram = vid->bgram + (0x08000/2);
	const UINT32 scrx = vid->bgram[(0x800/2)] - vid->sp_ofs_x;
	const UINT32 scry = vid->bgram[(0x802/2)] - vid->sp_ofs_y;
	const UINT32 vram_line = ( scry + line ) & 0x3FF;
	const SINT32 w = vid->h_sz;
	const UINT32 src_yofs = ( (vram_line) & 15 ) << 1;
	const UINT16* src = bgram + ((vram_line>>4)<<6);    // 0x40 chars / line
	const UINT32 base_pri = VID_PLANE_SP | VID_SP_PRI_BGH;
	SINT32 dx = -(SINT32)(scrx&15);
	SINT32 sx = (scrx>>4) & 0x3F;
	UINT32* dst = LINEBUF_TX + LINEBUF_OFS + dx;

	if ( TX_OPAQ ) {
		// OPAQの場合、Index#0のピクセルも（パレット含め）描く（スーパーハングオン）
		for ( ; dx<w; dx+=16, sx=(sx+1)&0x3F) {
			const UINT32 tile = src[sx];
			const UINT16* pchr = chrram + ((tile&0x00FF)<<6);  // 0x40 WORDS / char
			const UINT16* pal = palbase + ( (tile&0x0F00) >> 4 );
			UINT32 data;
			pchr += ( tile & 0x8000 ) ? ((15<<1)-src_yofs) : src_yofs;  // V flip
			/*
				16x16 chr は 8x8 を以下のように配置したのと同等
					---------
					| 0 | 2 |
					---------
					| 1 | 3 |
					---------
			*/
			if ( !( tile & 0x4000 ) ) {
				// H normal
				data = (pchr[0x00]<<16) | pchr[0x01];
				DRAW_BGCHIP_OPAQ;
				data = (pchr[0x20]<<16) | pchr[0x21];
				DRAW_BGCHIP_OPAQ;
			} else {
				// H flip
				data = (pchr[0x20]<<16) | pchr[0x21];
				DRAW_BGCHIP_OPAQ_FLIP;
				data = (pchr[0x00]<<16) | pchr[0x01];
				DRAW_BGCHIP_OPAQ_FLIP;
			}
		}
		TX_OPAQ = FALSE;
	} else {
		for ( ; dx<w; dx+=16, sx=(sx+1)&0x3F) {
			const UINT32 tile = src[sx];
			const UINT16* pchr = chrram + ((tile&0x00FF)<<6);  // 0x40 WORDS / char
			const UINT16* pal = palbase + ( (tile&0x0F00) >> 4 );
			UINT32 data;
			pchr += ( tile & 0x8000 ) ? ((15<<1)-src_yofs) : src_yofs;  // V flip
			if ( !( tile & 0x4000 ) ) {
				// H normal
				data = (pchr[0x00]<<16) | pchr[0x01];
				DRAW_BGCHIP;
				data = (pchr[0x20]<<16) | pchr[0x21];
				DRAW_BGCHIP;
			} else {
				// H flip
				data = (pchr[0x20]<<16) | pchr[0x21];
				DRAW_BGCHIP_FLIP;
				data = (pchr[0x00]<<16) | pchr[0x01];
				DRAW_BGCHIP_FLIP;
			}
		}
	}
}

/*
	BGよりもPRIが低い場合、色コードは弄らずにPRIのみVID_SP_PRI_MAXにすることで、以降のSPをマスクする
	（これにより、後ろのSPから順にバッファ上に上描きすることでライン画像を生成している（と思われる）
	ハードウェア動作を再現する）
*/
#define DRAW_SPCHIP \
	{ const UINT32 c = (data>>28)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>>24)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>>20)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>>16)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>>12)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>> 8)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>> 4)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>> 0)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; }

#define DRAW_SPCHIP_FLIP \
	{ const UINT32 c = (data>> 0)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>> 4)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>> 8)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>>12)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>>16)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>>20)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>>24)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; } \
	{ const UINT32 c = (data>>28)&0x0F; if ( c ) { if (  *dst<pri ) { *dst = pal[c] | flags; } else { *dst |= VID_SP_PRI_MAX; } } dst++; }

static void SpriteDrawLine(X68000_VIDEO* vid, UINT32 line)
{
	const UINT16* palbase = vid->pal + 0x100;
	const SINT32 screen_w = vid->h_sz;  // クレイジークライマー2は水平256ドットモードで横272表示（256でリミットすると表示が切れる）
	const UINT16* p = vid->bgram + (0x00000/2);
	const UINT16* pe = p + (128*4);
	const UINT16* chrram = vid->bgram + (0x08000/2);

	// スプライトは 0 番が一番手前
	while ( p<pe ) {
		const UINT32 prw = p[3] & 0x0003;
		if ( prw ) {  // 0 以外なら表示
			const SINT32 dx = (SINT32)(p[0]&0x3FF) - 16 + vid->sp_ofs_x;
			const SINT32 dy = (SINT32)(p[1]&0x3FF) - 16 + vid->sp_ofs_y;
			const UINT32 srcy = (UINT32)(line-dy);
			if ( srcy<16 && dx>=-15 && dx<screen_w ) {  // 表示範囲内
				const UINT32 src_yofs = srcy << 1;
				const UINT32 tile = p[2];
				const UINT16* pchr = chrram + ((tile&0x00FF)<<6);  // 0x40 WORDS / char (0x10 WORDS x 4)
				const UINT16* pal = palbase + ( (tile&0x0F00) >> 4 );
				const UINT32 pri = prw << (VID_SP_PRI_SFT+1);  // 1/2/3 → 0x00200000/0x00400000/0x00600000
				const UINT32 flags = VID_PLANE_SP | VID_SP_PRI_MAX;
				UINT32* dst = LINEBUF_TX + LINEBUF_OFS + dx;
				UINT32 data;
				pchr += ( tile & 0x8000 ) ? ((15<<1)-src_yofs) : src_yofs;  // V flip
				/*
					16x16 chr のデータ構成は512ドットモードBGと同じ
				*/
				if ( !( tile & 0x4000 ) ) {
					// H normal
					data = (pchr[0x00]<<16) | pchr[0x01];
					DRAW_SPCHIP;
					data = (pchr[0x20]<<16) | pchr[0x21];
					DRAW_SPCHIP;
				} else {
					// H flip
					data = (pchr[0x20]<<16) | pchr[0x21];
					DRAW_SPCHIP_FLIP;
					data = (pchr[0x00]<<16) | pchr[0x01];
					DRAW_SPCHIP_FLIP;
				}
			}
		}
		p += 4;
	}
}

static void SpDrawLine(X68000_VIDEO* vid, UINT32 line)
{
	const UINT32 bg_ctrl = vid->bgram[0x808/2];
	const UINT32 vctrl2 = vid->vctrl2;
	const BOOL draw = ( ( bg_ctrl & 0x0200 ) && ( vctrl2 & 0x0040 ) ) ? TRUE : FALSE;

	// BG描画
	if ( draw ) {  // SP=ON & D/C=D（BG/SP描画）
		const UINT32 bgsp_reso = vid->bgram[0x810/2];  // bg_reso & 0x01 で 512 ドットモード
		const UINT32 crtc_reso = CRTC(20);

		// 垂直解像度がCRTCとBG/SPで異なる場合の処理
		if ( ( crtc_reso ^ bgsp_reso ) & 0x04 ) {
			if ( crtc_reso & 0x04 ) {
				// 垂直512ラインモード、且つBG/SPが垂直256ライン設定（サイバリオン タイトル）
				line >>= 1;  // 垂直2倍描画（同ラインを2回描く）
			} else {
				// 垂直256ラインモード、且つBG/SPが垂直512ライン設定（使用タイトルあるのか不明）
				line <<= 1;  // 間引いて偶数ラインのみ表示（インタレース表示にすべき？）
			}
		}

		if ( bgsp_reso & 0x01 ) {
			// 水平 512 dot
			// 512ドットの場合1面しか存在しない（Inside X68000 p.173） マジカルショットでゴミが出てた問題への対処
			if ( bg_ctrl & 0x0001 ) BgDrawLine512(vid, line);     // BG0
		} else {
			// 水平 256 dot
			if ( bg_ctrl & 0x0008 ) BgDrawLine256(vid, 1, line);  // BG1（必ず下）
			if ( bg_ctrl & 0x0001 ) BgDrawLine256(vid, 0, line);  // BG0
		}
	}

	// この時点で TX_OPAQ が残っている＝BG/SP面自体の表示OFF(Vctrl2)、もしくは D/C(BgCtrl)=CPU（BG/SP描画停止）、もしくはBG両面とも表示OFF(BgCtrl)
	// この場合は pal[0x100] で埋めておく（出たツイ ランキング表示からコナミロゴに移る辺りの挙動、ラプラスOP）
	if ( ( TX_OPAQ ) && ( vctrl2 & 0x0060 ) ) {  // TX/SPの片方以上がON時のみ（デスブリンガーOP）
		const UINT32 c = vid->pal[0x100] | VID_PLANE_TX0;
		UINT32* p = LINEBUF_TX + LINEBUF_OFS;
		UINT32* pe = p + vid->h_sz;  // クレイジークライマー2は水平256ドットモードで横272表示（256しか消さないとゴミが残る）
		while ( p < pe ) *p++ = c;
		TX_OPAQ = FALSE;
	}

	// SP描画
	if ( draw ) {  // SP=ON & D/C=D（BG/SP描画）
		SpriteDrawLine(vid, line);
	}
}


// --------------------------------------------------------------------------
//   グラフィック面
// --------------------------------------------------------------------------
// 通常描画用：EXON、B/P、G/G によって4種類に分かれる
#define GR_DRAW_MODE        0
#define GRFUNC_DRAW_4BIT_H  GrDrawLine4bit1024_MODE0
#define GRFUNC_DRAW_4BIT    GrDrawLine4bit_MODE0
#define GRFUNC_DRAW_8BIT    GrDrawLine8bit_MODE0
#define GRFUNC_DRAW_16BIT   GrDrawLine16bit_MODE0
#include "x68000_gr_draw.inc"

#define GR_DRAW_MODE        1
#define GRFUNC_DRAW_4BIT_H  GrDrawLine4bit1024_MODE1
#define GRFUNC_DRAW_4BIT    GrDrawLine4bit_MODE1
#define GRFUNC_DRAW_8BIT    GrDrawLine8bit_MODE1
#define GRFUNC_DRAW_16BIT   GrDrawLine16bit_MODE1
#include "x68000_gr_draw.inc"

#define GR_DRAW_MODE        2
#define GRFUNC_DRAW_4BIT_H  GrDrawLine4bit1024_MODE2
#define GRFUNC_DRAW_4BIT    GrDrawLine4bit_MODE2
#define GRFUNC_DRAW_8BIT    GrDrawLine8bit_MODE2
#define GRFUNC_DRAW_16BIT   GrDrawLine16bit_MODE2
#include "x68000_gr_draw.inc"

#define GR_DRAW_MODE        3
#define GRFUNC_DRAW_4BIT_H  GrDrawLine4bit1024_MODE3
#define GRFUNC_DRAW_4BIT    GrDrawLine4bit_MODE3
#define GRFUNC_DRAW_8BIT    GrDrawLine8bit_MODE3
#define GRFUNC_DRAW_16BIT   GrDrawLine16bit_MODE3
#include "x68000_gr_draw.inc"

// G/G MIX用：B/Pビット動作再現のため、B/P=0版とB/P=1版の2種類のGR MIX関数を用意する
#define GR_MIX_BP1
#define GRFUNC_MIX_4BIT_H   GrMixLine4bit1024_BP1
#define GRFUNC_MIX_4BIT     GrMixLine4bit_BP1
#define GRFUNC_MIX_8BIT     GrMixLine8bit_BP1
#define GRFUNC_MIX_16BIT    GrMixLine16bit_BP1
#include "x68000_gr_mix.inc"

#undef  GR_MIX_BP1
#define GRFUNC_MIX_4BIT_H   GrMixLine4bit1024_BP0
#define GRFUNC_MIX_4BIT     GrMixLine4bit_BP0
#define GRFUNC_MIX_8BIT     GrMixLine8bit_BP0
#define GRFUNC_MIX_16BIT    GrMixLine16bit_BP0
#include "x68000_gr_mix.inc"


// 上記をまとめて切り替えるための関数ポインタリスト
typedef struct {
	void (*GrDrawLine4bit1024)(X68000_VIDEO* vid, UINT32 line);
	void (*GrDrawLine4bit)(X68000_VIDEO* vid, UINT32 line, UINT32 layer, BOOL opaq);
	void (*GrDrawLine8bit)(X68000_VIDEO* vid, UINT32 line, UINT32 layer, BOOL opaq);
	void (*GrDrawLine16bit)(X68000_VIDEO* vid, UINT32 line);
} ST_GR_DRAW_FUNCS;

static const ST_GR_DRAW_FUNCS GR_DRAW_FUNCTIONS[4] =
{
	{
		GrDrawLine4bit1024_MODE0,
		GrDrawLine4bit_MODE0,
		GrDrawLine8bit_MODE0,
		GrDrawLine16bit_MODE0
	},
	{
		GrDrawLine4bit1024_MODE1,
		GrDrawLine4bit_MODE1,
		GrDrawLine8bit_MODE1,
		GrDrawLine16bit_MODE1
	},
	{
		GrDrawLine4bit1024_MODE2,
		GrDrawLine4bit_MODE2,
		GrDrawLine8bit_MODE2,
		GrDrawLine16bit_MODE2
	},
	{
		GrDrawLine4bit1024_MODE3,
		GrDrawLine4bit_MODE3,
		GrDrawLine8bit_MODE3,
		GrDrawLine16bit_MODE3
	}
};


typedef struct {
	void (*GrMixLine4bit1024)(X68000_VIDEO* vid, UINT32 line);
	void (*GrMixLine4bit)(X68000_VIDEO* vid, UINT32 line, BOOL opaq);
	void (*GrMixLine8bit)(X68000_VIDEO* vid, UINT32 line);
	void (*GrMixLine16bit)(X68000_VIDEO* vid, UINT32 line);
} ST_GR_MIX_FUNCS;

static const ST_GR_MIX_FUNCS GR_MIX_FUNCTIONS[2] =
{
	{
		GrMixLine4bit1024_BP0,
		GrMixLine4bit_BP0,
		GrMixLine8bit_BP0,
		GrMixLine16bit_BP0
	},
	{
		GrMixLine4bit1024_BP1,
		GrMixLine4bit_BP1,
		GrMixLine8bit_BP1,
		GrMixLine16bit_BP1
	}
};


// GR面生成入り口
static void GrDrawLine(X68000_VIDEO* vid, UINT32 line)
{
	/*
		〇 半透明に関する注意事項：

		半透明に関しては Inside X68000 p.209 の図の流れに沿って行われる。
		G/G及びG/T両方の半透明がONになっている場合、まずG/G（グラフィック間）半透明処理が行われ、
		その時の半透明フラグ（EXフラグ）を持ち越したまま、G/T（対TX/SP）半透明処理が行われる。
		そのため、両方の半透明がONになっているピクセルに於いて、グラフィック面のベースページの色
		は最終的に1/4の量まで減衰する（1/2合成が2度起こるため）。

		なお、GRが1面しかないモードでもG/G合成は可能。
		この場合、c をカラーインデックスとして、pal[c&~1] と pal[c|1] の合成となる。


		〇 Vctrl2 の EXON=1 時に B/P を 0 に設定した際の特殊挙動（仕様外動作）：

		B/P=0 だとカラーコードの最下位ビットではなく、パレットカラーの最下位（Iビット）がEXフラグ
		（半透明・特殊PRI判定用フラグ）として機能するようになる。
		メタルオレンジEXのタイトルが出るところで使用されているのは確認。

		挙動が複雑なので、検証しながらある程度実機相当の表示になるよう場当たり的に対応してある。
		正直ロジックがよく分からん…。
		現状の実装自体は x68000_gr_draw.inc / x68000_gr_mix.inc 参照。きれいに書き直せる人求む。
	*/
	const UINT32 vctrl0 = vid->vctrl0;
	const UINT32 vctrl2 = vid->vctrl2;

	// EXON / B/P / G/G によって関数セット切り替え
	const ST_GR_DRAW_FUNCS* f_draw = &GR_DRAW_FUNCTIONS[vid->gr_draw_func_idx];
	const ST_GR_MIX_FUNCS*  f_mix = &GR_MIX_FUNCTIONS[vid->gr_mix_func_idx];

	// EXあり、半透明ON、G/G ON、ベースページON、を満たすとG/G半透明が実行される（Layer#1はOFFでも起こる）
	const BOOL mix_gg = ( ( vctrl2 & 0x1A00 ) == 0x1A00 ) ? TRUE : FALSE;

	if ( vctrl0 & 4 ) {
		// 16色 1024ドットモード
		if ( vctrl2 & 0x0010 ) {
			if ( mix_gg  ) {
				// G/G半透明
				f_mix->GrMixLine4bit1024(vid, line);
			} else {
				// 通常描画
				f_draw->GrDrawLine4bit1024(vid, line);
			}
			GR_OPAQ = FALSE;
		}
	} else {
		// 512ドットモード
		switch ( vctrl0 & 3 )
		{
		case 1:  // 256色 x 2面
		case 2:
		default:
			if ( ( mix_gg ) && ( vctrl2 & 0x0003 ) ) {
				// G/G半透明
				f_mix->GrMixLine8bit(vid, line);
				GR_OPAQ = FALSE;
			} else {
				// 通常描画
				if ( vctrl2 & 0x000C ) {
					f_draw->GrDrawLine8bit(vid, line, 1, GR_OPAQ);
					GR_OPAQ = FALSE;
				}
				if ( vctrl2 & 0x0003 ) {
					f_draw->GrDrawLine8bit(vid, line, 0, GR_OPAQ);
					GR_OPAQ = FALSE;
				}
			}
			break;

		case 0:  // 16色 x 4面
			if ( vctrl2 & 0x0008 ) {
				f_draw->GrDrawLine4bit(vid, line, 3, GR_OPAQ);
				GR_OPAQ = FALSE;
			}
			if ( vctrl2 & 0x0004 ) {
				f_draw->GrDrawLine4bit(vid, line, 2, GR_OPAQ);
				GR_OPAQ = FALSE;
			}
			if ( ( mix_gg ) && ( vctrl2 & 0x0001 ) ) {
				// G/G半透明
				f_mix->GrMixLine4bit(vid, line, GR_OPAQ);
				GR_OPAQ = FALSE;
			} else {
				// 通常描画
				if ( vctrl2 & 0x0002 ) {
					f_draw->GrDrawLine4bit(vid, line, 1, GR_OPAQ);
					GR_OPAQ = FALSE;
				}
				if ( vctrl2 & 0x0001 ) {
					f_draw->GrDrawLine4bit(vid, line, 0, GR_OPAQ);
					GR_OPAQ = FALSE;
				}
			}
			break;

		case 3:  // 63356色 ｘ 1面
			if ( vctrl2 & 0x000F ) {
				if ( mix_gg  ) {
					// G/G半透明
					f_mix->GrMixLine16bit(vid, line);
				} else {
					// 通常描画
					f_draw->GrDrawLine16bit(vid, line);
				}
				GR_OPAQ = FALSE;
			}
			break;
		}
	}
}


// --------------------------------------------------------------------------
//   公開関数
// --------------------------------------------------------------------------
X68000_VIDEO* X68Video_Init(void)
{
	X68000_VIDEO* vid = (X68000_VIDEO*)_MALLOC(sizeof(X68000_VIDEO), "Video Driver");
	do {
		if ( !vid ) {
			DRIVER_INIT_ERROR("VDP struct malloc error");
		}
		memset(vid, 0, sizeof(X68000_VIDEO));
		SetupTables();
		X68Video_Reset(vid);
		LOG(("X68000 VDP : initialize OK"));
		return vid;
	} while ( 0 );

	X68Video_Cleanup(vid);
	return NULL;
}


void X68Video_Cleanup(X68000_VIDEO* vid)
{
	if ( vid ) {
		_MFREE(vid);
	}
}


void X68Video_Reset(X68000_VIDEO* vid)
{
	if ( vid ) {
		memset(vid, 0, sizeof(X68000_VIDEO));
		X68Video_UpdateMixFunc(vid);
	}
}

void X68Video_UpdateMixFunc(X68000_VIDEO* vid)
{
	// MIX関数は vctrl1/2 だけであらかた決まるので、ライン描画で毎回計算せずに vctrl 更新時に計算しておく
	const UINT32 vctrl1 = vid->vctrl1;
	const UINT32 vctrl2 = vid->vctrl2;
	UINT32 pri_gr = ( vctrl1 >>  8 ) & 3;
	UINT32 pri_tx = ( vctrl1 >> 10 ) & 3;
	UINT32 pri_sp = ( vctrl1 >> 12 ) & 3;

	// GRの描画関数・GR間MIX関数設定
	// B/Pによって関数セット切り替え（B/P=0 の特殊処理関数は EXON=1 の場合のみ使用）
	if ( !( vctrl2 & 0x1000 ) ) {
		// EXON=0
		vid->gr_draw_func_idx = 0;
	} else {
		// EXON=1
		if ( vctrl2 & 0x0400 ) {
			// B/P=1
			vid->gr_draw_func_idx = 1;
		} else {
			// B/P=0
			if ( ( vctrl2 & 0x0A00 ) != 0x0A00 ) {
				// G/G OFF
				vid->gr_draw_func_idx = 2;
			} else {
				// G/G ON
				vid->gr_draw_func_idx = 3;
			}
		}
	}
	vid->gr_mix_func_idx = ( vctrl2 >> 10 ) & 1;  // B/P

	// 特殊プライオリティ・半透明の有無、及びレイヤのプライオリティに合わせてレイヤ間MIX関数を決める
	/*
		PRI値挙動について：

		1. GRがPRI=3の場合、GR以外のレイヤは消える
		2. GRがPRI=0～2の場合、そのPRI値でGRのプライオリティが確定（そのPRIをGRが占拠）
		   例えばGRがPRI=1だった場合、他のレイヤがPRI=1指定されていてもGRが1として扱われる
		3. その後、PRI0～2の残り二枠をTXとSPが埋めるが、この際考慮されるのはSPとTXのPRI値比較
		   結果（値の小さい方が、空いてるPRI枠の高優先度の方に配置される）。
		   同じ値だった場合はTX優先。また、TXかSPに3がある場合はそのまま3（最低優先度）として
		   判定される

		…というアルゴリズムのはず（テストプログラムによる実機での調査結果より）
		「ジーザスII」のOPのタイトル表示の再現のためには上記処理が必要
	*/
	switch ( pri_gr )
	{
	case 0:  // GR=0、1と2をTX/SPで分け合う
		if ( pri_tx <= pri_sp ) {  // TXの方が値が小さい（TX優先）
			pri_tx = 1; pri_sp = 2;
		} else {
			pri_tx = 2; pri_sp = 1;
		}
		break;
	case 1:  // GR=1、0と2をTX/SPで分け合う
		if ( pri_tx <= pri_sp ) {  // TXの方が値が小さい（TX優先）
			pri_tx = 0; pri_sp = 2;
		} else {
			pri_tx = 2; pri_sp = 0;
		}
		break;
	case 2:  // GR=2、0と1をTX/SPで分け合う
		if ( pri_tx <= pri_sp ) {  // TXの方が値が小さい（TX優先）
			pri_tx = 0; pri_sp = 1;
		} else {
			pri_tx = 1; pri_sp = 0;
		}
		break;
	case 3:  // GR以外消える
	default:
		break;
	}

	vid->mix_func_grmask = 0;  // なくても平気なはずだが一応

	// この段階でPRI同一値は解消されている（pri_gr=3時を除いて同一値は存在しない）
	if ( pri_gr == 3 ) {
		// GR以外消える
		// この設定は他の条件より優先される
		vid->mix_func_offset= 12;
	} else if ( ( pri_tx < pri_gr ) && ( pri_sp < pri_gr ) ) {
		// GR が一番下
		// GR -> TX の順でミックス
		vid->mix_func_offset = 8;
	} else if ( ( pri_tx > pri_gr ) && ( pri_sp > pri_gr ) ) {
		// GR が一番上
		// TX -> GR の順でミックス、マスクなし
		vid->mix_func_offset = 0;
	} else {
		// それ以外
		// TX -> GR の順でミックス、マスクあり
		if ( pri_tx < pri_gr ) vid->mix_func_grmask = VID_PLANE_TX;  // TX > GR なのでGRをTXでマスクする
		if ( pri_sp < pri_gr ) vid->mix_func_grmask = VID_PLANE_SP;  // SP > GR なのでGRをSPでマスクする
		// 上記2つは排他、どちらかのみが立つはず
		vid->mix_func_offset= 4;
	}

	// idx は、+0:GR最上位、+4:GR中間、+8:GR最下位、+12:GR以外OFF(GR PRI=3)
	switch ( vctrl2 & 0x5800 )
	{
	case 0x0000:  // 通常
	case 0x0800:
		vid->mix_func_idx = MIX_FUNC_IDX_N;
		break;
	case 0x1000:  // 特殊PRI
		vid->mix_func_idx = MIX_FUNC_IDX_S;
		break;
	case 0x1800:  // 半透明
		vid->mix_func_idx = MIX_FUNC_IDX_T;
		break;
	default:      // TX0半透明
		vid->mix_func_idx = MIX_FUNC_IDX_0;
		break;
	}

	// MIX関数テーブル設定
	// ステートで復帰しやすいように、直接ポインタではなく一旦 idx と offset にしてからテーブル参照
	X68K_MIX_FUNC = MIX_FUNC_TABLE[vid->mix_func_idx] + vid->mix_func_offset;
}

// 1ライン生成
void FASTCALL X68Video_LineUpdate(X68000_VIDEO* vid, UINT32 line)
{
	const UINT32 h = _MIN(vid->v_sz, vid->vscr_h);

	if ( line < h )
	{
		const UINT32 pri_tx = ( vid->vctrl1 >> 10 ) & 3;
		const UINT32 pri_sp = ( vid->vctrl1 >> 12 ) & 3;
		UINT32 srcline = line;  // 参照元ライン（疑似1024ラインモード？用）
		UINT32 idx;             // MIX関数idx

		/*
			現状のライン合成アルゴリズムは以下の通り。

			1. TX と BG/SP は同一のラインバッファに展開する。この際 TX と SP(BG) のプライオリティ
			   によって、どちらを先に描くかを変える（低い方が先）。

			2. SP が下の場合、最下層の BG チップのパレット参照カラーで埋められる。この際、Index
			   #0（抜き色）の色もそのまま描かれる。BG 非表示の場合、pal[0x100] で埋める。
			   その上に SP が BG とのプライオリティ、並びに SP 間のプライオリティを考慮のうえで
			   描画され、最後にその上に TX が Index#0 を抜き色として描画される。

			3. TX が下の場合、ラインバッファは TX パレット参照カラーで埋められる。この際、Index
			   #0（抜き色）の色もそのまま描かれる（以後このピクセルを TX0 と呼称）。
			   その上に BG/SP が Index#0 を抜き色とした描画を行う。SP は BG とのプライオリティ、
			   並びに SP 間のプライオリティ（メモリ上で前のものほど優先）を考慮のうえで描画（抜
			   き色は Index#0）。

			   但し、BG 側が抜き色で、その対象先が TX0 だった場合、TX0 は BG 側の色（pal[0xN0]
			   の色）で上書きされる（デスブリンガーOPの挙動）。
			   つまり、実際には TX が上でも下でも基本挙動は2.（BG が ON であるならば、BG の透過
			   色がバックドロップになる）であり、透過色以外の TX-BG 間プライオリティが2.と逆に
			   なっている状態が3.、だと思われる。

			4. GR は上記とは別のバッファに、下のレイヤから順に描画される。この際、最下位レイヤは
			   Index #0 を含めた全色がパレット参照カラーで描かれ、上位レイヤは Index #0 を抜き色
			   として上描きされる（但し VidCtrl2 の B/P が 0 の際は特殊動作がある。GrDrawLine() 
			   冒頭のコメント参照）。

			5. TX/SP/BG のバッファと、GR のバッファを合成する。
			   この際、抜き色としてパレットカラーで黒（$0000）が用いられる。それ以外は（例え Index
			   は0番であっても）不透過。
			   また、旧版では TX0 カラーは強制抜き色としていたが、実際には TX0 であっても上記のルー
			   ルは変わらない模様（代わりに2.に条件追加）。

			6. プライオリティ値が同じものがある場合は扱いが特殊。詳しくは UpdateMixFunc() のコメ
			   ント参照。

			7. 特殊プライオリティは必ず一番上に描く（対象レイヤビットは機能しない）

			8. 特殊プライオリティ時、ベースプレーンの Index#0 以外の黒は、透明ではなく黒として扱
			   われる（構造的に恐らく半透明時も同じ）

			9. 半透明は GR より低いプライオリティのレイヤに対してのみ行っている（※）。

			（※）けろぴーのソースコメントの中に「GrpよりTextが上にある場合にTextとの半透明を行う
			      と、SPのプライオリティもTextに引きずられる？（つまり、Grpより下にあってもSPが表
				  示される？）」という表記があるが、具体的なタイトルやシーンは不明。
				  現状このコード内では特にそのような処理は行っていない。


			透明色の扱いに関して注意すべき挙動の参考例：

			TX0についてはこの3つが並立しなければならない（上記3.の挙動）
			- デスブリンガーOP（SP>TX>GR） 0番は色がついてても透過 TX0=$FFFE  BG窓はパレット#1 Index#0（Color=$0000）
			- 伊忍道（SP>TX>GR） TX0が抜き色にならない BG0/1はOFF  TX0=$0001
			- イシター（SP>TX>GR） 0番着色             GRはOFF     TX0=$5000

			その他、過去に問題が出ることが多かった部分
			- レミングス 起動時DMAロゴ 0番着色（SP>GR>TX） 但しBGはOFF  TX0=$FFFE
			- 出たツイ コナミロゴ 0番透過しない（TX>SP>GR） 但しBGはD/C=Cで消えてる  TX0=$0842
			- スーパーハングオン 0番着色、パレットも反映（TX>GR>SP）
			- ラプラスの魔 0番着色（TX>SP>GR） SPはOFF
			- ラプラスの魔 8番（黒）透過（TX>SP>GR） SPはOFF
			- スタークルーザー TXの1番（黒）黒表示（TX>GR>SP）BG0/1 OFF、TX/SP/GR ON
			- 桃太郎伝説 SP/BGの3番？（黒）黒表示（TX=SP=GR=0）
		*/

		// ドラゴンバスターのロード画面で使われてる特殊モード（疑似1024ライン？）
		if ( ( CRTC(20) & 0x18 ) == 0x18 ) {
			// 実機の挙動見る限り VD=2/VD=3 共にこの表示っぽい
			srcline <<= 1;
		}

		// GR 用ラインバッファへの生成
		GR_OPAQ = TRUE;
		GrDrawLine(vid, srcline);

		// TX/BG/SP 用ラインバッファへの生成
		TX_OPAQ = TRUE;
		if ( pri_tx <= pri_sp ) {  // TX > SP
			// SP → TX の順
			// SPはOFFでも pal[0x100] 埋めを行いたいので、ここでONチェックはしない（ラプラス）
			SpDrawLine(vid, srcline);
			TxDrawLine(vid, srcline);
		} else {                   // SP > TX
			// TX → SP の順
			TxDrawLine(vid, srcline);
			SpDrawLine(vid, srcline);
		}

		// 表示状態によってMIX関数を決める
		// idx は bit1=GR面のON/OFF、bit0=TX/SP面のON/OFF
		idx = ( (!GR_OPAQ) ? 2 : 0 ) |  ( (!TX_OPAQ) ? 1 : 0 );  // OPAQが落ちてるなら何らかの描画があった

		// MIX実行
		X68K_MIX_FUNC[idx](vid, line);
	}
}

// 画面の生成
void FASTCALL X68Video_Update(X68000_VIDEO* vid, INFO_SCRNBUF* scr)
{
	// 仮想スクリーンからテクスチャへの転送
	const UINT32* rgbtbl = PAL2RGB[vid->contrast];
	const SINT32 bpp = scr->bpp;
	const SINT32 bpl = scr->bpl;
	const UINT32 w = _MIN(scr->w, vid->vscr_w);
	const UINT32 h = _MIN(scr->h, vid->vscr_h);
	const UINT16* src = VSCR;
	UINT8* dst = scr->ptr;
	UINT32 y = h;
	while ( y-- ) {
		const UINT16* s = src;
		UINT8* d = dst;
		UINT32 x = w;
		while ( x-- ) {
			UINT32 c = rgbtbl[*s++];
			WRITEENDIANDWORD(d, c);
			d += bpp;
		}
		src += vid->vscr_w;
		dst += bpl;
	}
}

// ステートロード/セーブ
void X68Video_LoadState(X68000_VIDEO* vid, STATE* state, UINT32 id)
{
	if ( vid && state ) {
		ReadState(state, id, MAKESTATEID('V','I','D','S'), vid, sizeof(X68000_VIDEO));
		ReadState(state, id, MAKESTATEID('V','S','C','R'), VSCR, vid->vscr_w * vid->vscr_h * sizeof(VSCR[0]));
		// MIX関数テーブル復帰
		X68K_MIX_FUNC = MIX_FUNC_TABLE[vid->mix_func_idx] + vid->mix_func_offset;
	}
}
void X68Video_SaveState(X68000_VIDEO* vid, STATE* state, UINT32 id)
{
	if ( vid && state ) {
		WriteState(state, id, MAKESTATEID('V','I','D','S'), vid, sizeof(X68000_VIDEO));
		WriteState(state, id, MAKESTATEID('V','S','C','R'), VSCR, vid->vscr_w * vid->vscr_h * sizeof(VSCR[0]));
	}
}
