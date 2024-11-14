// --------------------------------------------------------------------------
//   GR面ライン生成
// --------------------------------------------------------------------------

/*
	EXON、B/P、G/G によって4種類に分かれる

	EXON=0 (MODE=0)
	  → 最下位ビットはあり、EXフラグなし（通常描画）

	EXON=1
	  → B/P=1 (MODE=1)
	       → 最下位ビット無視、EXフラグは1stレイヤの最下位ビット（通常EX）
	  → B/P=0 G/Gなし (MODE=2)
	       → 最下位ビットはあり、EXフラグは全レイヤのIビット（特殊EX1）
	  → B/P=0 G/Gあり (MODE=3)
	       → 最下位ビット無視、特殊カラー参照、EXフラグは全レイヤのIビット（特殊EX2）
*/

#if GR_DRAW_MODE==0
	#define PIX_COND     pix
	#define TOP_EXBIT    0
	#define LOW_EXBIT    0
	#define PIX_COND16   (l&1)
	#define TOP_EXBIT16  0
#elif GR_DRAW_MODE==1
	#define PIX_COND     pix&~1
	#define TOP_EXBIT    ((pix&1)<<VID_SHIFT_EX)
	#define LOW_EXBIT    0
	#define PIX_COND16   0
	#define TOP_EXBIT16  ((l&1)<<VID_SHIFT_EX)
#elif GR_DRAW_MODE==2
	#define PIX_COND     pix
	#define TOP_EXBIT    ((pal[pix]&1)<<VID_SHIFT_EX)
	#define LOW_EXBIT    ((pal[pix]&1)<<VID_SHIFT_EX)
	#define PIX_COND16   (l&1)
	#define TOP_EXBIT16  ((c&1)<<VID_SHIFT_EX)
#else  // GR_DRAW_MODE==3
	#define PIX_COND     pix&~1
	#define TOP_EXBIT    ((pal[pix]&1)<<VID_SHIFT_EX)
	#define LOW_EXBIT    ((pal[pix]&1)<<VID_SHIFT_EX)
	#define PIX_COND16   0
	#define TOP_EXBIT16  ((c&1)<<VID_SHIFT_EX)

	#define MODE3_OPAQ \
	    const UINT32 c = pal[PIX_COND];                   \
	    if ( c & 1 ) {  /* I=1 */                         \
	        *dst = ADD_TRANS_PIXEL(c, pal[0]) | VID_PLANE_EX; \
	    } else {                                          \
	        *dst = ( pix & 1 ) ? pal[1] : c;              \
	    }

	#define MODE3_NORMAL \
	    if ( pix ) { MODE3_OPAQ }
#endif



// ------------------------------------
//   ここから関数本体定義
// ------------------------------------
static void GRFUNC_DRAW_4BIT_H(X68000_VIDEO* vid, UINT32 line)
{
	// 16色 1024x1024 1面モード
	// 「JESUS II」のOPなど
	const UINT16* pal = vid->pal;
	const UINT32 w = vid->h_sz;
	const UINT32 scrx = CRTC(12);  // スクロールレジスタは（Inside X68000 を信じるなら）0番で固定
	const UINT32 scry = CRTC(13);
	const UINT32 sy = ( line + scry ) & 0x3FF;  // 参照ライン（0～1023）
	const UINT32 basesft = ( sy & 0x200 ) >> 6; // 0～511:GD0/1(sft=0), 512～1023:GD2/3(sft=8) 
	const UINT16* src = vid->gvram + ( ( sy & 0x1FF ) << 9 );  // 512 WORDS / line
	UINT32 sx = scrx & 0x3FF;  // 開始X座標（0～1023）
	UINT32 dx = 0;
	UINT32* dst = LINEBUF_GR + LINEBUF_OFS;

	for ( ; dx<w; dx++, sx=(sx+1)&0x3FF) {
		const UINT32 sft = ( sx & 0x200 ) >> 7;  // 0～511:GD0/2(sft=0), 512～1023:GD1/3(sft=4) 
		const UINT32 p = basesft + sft;
		const UINT32 d = src[sx&0x1FF];
		const UINT32 pix = ( d >> p ) & 0x0F;
		*dst = pal[PIX_COND] | TOP_EXBIT;
		dst++;
	}
}

static void GRFUNC_DRAW_4BIT(X68000_VIDEO* vid, UINT32 line, UINT32 layer, BOOL opaq)
{
	// 16色 4面モード
	// ドラキュラStage6～ボス辺りなど
	const UINT16* pal = vid->pal;
	const UINT32 w = vid->h_sz;
	const UINT32 vctrl1 = vid->vctrl1 >> ( layer*2 );
	const UINT32 p = ( vctrl1 & 3 ) << 2;
	const UINT32 scrx = CRTC(12+(p>>1));       // スクロールレジスタはGD番号に対応
	const UINT32 scry = CRTC(13+(p>>1));
	const UINT16* src = vid->gvram + ( ((line+scry)&0x1FF) << 9 );  // 512 WORDS / line
	UINT32 sx = scrx & 0x1FF;
	UINT32 dx = 0;
	UINT32* dst = LINEBUF_GR + LINEBUF_OFS;

	if ( layer==0 ) {
		// トップレイヤ
		if ( opaq ) {
			for ( ; dx<w; dx++, sx=(sx+1)&0x1FF) {
				const UINT32 d = src[sx];
				const UINT32 pix = ( d >> p ) & 0x0F;
				*dst = pal[PIX_COND] | TOP_EXBIT;
				dst++;
			}
		} else {
			for ( ; dx<w; dx++, sx=(sx+1)&0x1FF) {
				const UINT32 d = src[sx];
				const UINT32 pix = ( d >> p ) & 0x0F;
				if ( PIX_COND ) {
					*dst = pal[PIX_COND] | TOP_EXBIT;
				}
				dst++;
			}
		}
	} else {
		// 2nd以下
		if ( opaq ) {
			for ( ; dx<w; dx++, sx=(sx+1)&0x1FF) {
				const UINT32 d = src[sx];
				const UINT32 pix = ( d >> p ) & 0x0F;
#if GR_DRAW_MODE==3
				MODE3_OPAQ
#else
				*dst = pal[PIX_COND] | LOW_EXBIT;
#endif
				dst++;
			}
		} else {
			for ( ; dx<w; dx++, sx=(sx+1)&0x1FF) {
				const UINT32 d = src[sx];
				const UINT32 pix = ( d >> p ) & 0x0F;
#if GR_DRAW_MODE==3
				MODE3_NORMAL
#else
				if ( PIX_COND ) {
					*dst = pal[PIX_COND] | LOW_EXBIT;
				}
#endif
				dst++;
			}
		}
	}
}

static void GRFUNC_DRAW_8BIT(X68000_VIDEO* vid, UINT32 line, UINT32 layer, BOOL opaq)
{
	// 256色 2面モード
	// 上下 nibble でスクロール値が異なる場合も考慮（スクロール値が一致する場合は処理を分けた方が速いが、コードごちゃつくので…）
	const UINT16* pal = vid->pal;
	const UINT32 w = vid->h_sz;
	const UINT32 vctrl1 = vid->vctrl1 >> ( ( layer ) ? 4 : 0 );
	const UINT32 gd0 = (vctrl1>>0) & 3;      // 参照するGD
	const UINT32 gd1 = (vctrl1>>2) & 3;
	const UINT32 p0 = gd0 << 2;              // それぞれの GD のビットシフト値
	const UINT32 p1 = gd1 << 2;
	const UINT32 scrx0 = CRTC(12+(gd0<<1));  // スクロールレジスタはGD番号に対応（ドラキュラ Stage1 冒頭など）
	const UINT32 scrx1 = CRTC(12+(gd1<<1));
	const UINT32 scry0 = CRTC(13+(gd0<<1));
	const UINT32 scry1 = CRTC(13+(gd1<<1));
	const UINT16* src0 = vid->gvram + ( ((line+scry0)&0x1FF) << 9 );  // 512 WORDS / line
	const UINT16* src1 = vid->gvram + ( ((line+scry1)&0x1FF) << 9 );
	UINT32 sx0 = scrx0 & 0x1FF;
	UINT32 sx1 = scrx1 & 0x1FF;
	UINT32 dx = 0;
	UINT32* dst = LINEBUF_GR + LINEBUF_OFS;
	/*
		片方の GD だけ ON のような特殊状態の挙動
		「HEAVY NOVA」の挙動を見る限り、パレット参照の際には使用されるが、対下位GR面の抜き色（0番）
		判定の際には OFF の GD のビットに関しては 0 として扱われる？

		例）GD0=0x0 で ON、GD1=0x1 で OFF、の場合、パレットは pal[0x10] が参照されるが、抜き色判定で
			は 0x00 とみなされ、Index#0 として抜き色になる？
	*/
	const UINT32 pix_mask = ( ( vid->vctrl2 & (1<<0) ) ? 0x0F : 0x00 ) | ( ( vid->vctrl2 & (1<<1) ) ? 0xF0 : 0x00 );

	if ( layer==0 ) {
		// トップレイヤ
		if ( opaq ) {
			for ( ; dx<w; dx++, sx0=(sx0+1)&0x1FF, sx1=(sx1+1)&0x1FF) {
				const UINT32 d0 = src0[sx0];
				const UINT32 d1 = src1[sx1];
				const UINT32 pix = ((d0>>p0)&0x0F) | (((d1>>p1)&0x0F)<<4);
				*dst = pal[PIX_COND] | TOP_EXBIT;
				dst++;
			}
		} else {
			for ( ; dx<w; dx++, sx0=(sx0+1)&0x1FF, sx1=(sx1+1)&0x1FF) {
				const UINT32 d0 = src0[sx0];
				const UINT32 d1 = src1[sx1];
				const UINT32 pix = ((d0>>p0)&0x0F) | (((d1>>p1)&0x0F)<<4);
				if ( PIX_COND & pix_mask ) {
					*dst = pal[PIX_COND] | TOP_EXBIT;
				}
				dst++;
			}
		}
	} else {
		// 2nd
		if ( opaq ) {
			for ( ; dx<w; dx++, sx0=(sx0+1)&0x1FF, sx1=(sx1+1)&0x1FF) {
				const UINT32 d0 = src0[sx0];
				const UINT32 d1 = src1[sx1];
				const UINT32 pix = ((d0>>p0)&0x0F) | (((d1>>p1)&0x0F)<<4);
#if GR_DRAW_MODE==3
				MODE3_OPAQ
#else
				*dst = pal[PIX_COND] | LOW_EXBIT;
#endif
				dst++;
			}
		} else {
			for ( ; dx<w; dx++, sx0=(sx0+1)&0x1FF, sx1=(sx1+1)&0x1FF) {
				const UINT32 d0 = src0[sx0];
				const UINT32 d1 = src1[sx1];
				const UINT32 pix = ((d0>>p0)&0x0F) | (((d1>>p1)&0x0F)<<4);
#if GR_DRAW_MODE==3
				MODE3_NORMAL
#else
				if ( PIX_COND & pix_mask ) {
					*dst = pal[PIX_COND] | LOW_EXBIT;
				}
#endif
				dst++;
			}
		}
	}
}

static void GRFUNC_DRAW_16BIT(X68000_VIDEO* vid, UINT32 line)
{
	// 65536色 1面モード
	// 電波版Ysなど
	const UINT8* pal = (const UINT8*)vid->pal;
	const UINT32 w = vid->h_sz;
	const UINT32 vctrl1 = vid->vctrl1;
	const UINT32 p0 = ( (vctrl1>>0) & 3 ) << 2;  // 参照するGD（下でのビットシフト用に 0/4/8/12 に変換してある）
	const UINT32 p1 = ( (vctrl1>>2) & 3 ) << 2;
	const UINT32 p2 = ( (vctrl1>>4) & 3 ) << 2;
	const UINT32 p3 = ( (vctrl1>>6) & 3 ) << 2;
	const UINT32 scrx0 = CRTC(12+(p0>>1));       // スクロールレジスタはGD番号に対応
	const UINT32 scrx1 = CRTC(12+(p1>>1));
	const UINT32 scrx2 = CRTC(12+(p2>>1));
	const UINT32 scrx3 = CRTC(12+(p3>>1));
	const UINT32 scry0 = CRTC(13+(p0>>1));
	const UINT32 scry1 = CRTC(13+(p1>>1));
	const UINT32 scry2 = CRTC(13+(p2>>1));
	const UINT32 scry3 = CRTC(13+(p3>>1));
	/*
		XXX
		GD0～GD3 の一部だけ ON のような特殊状態は考慮していない
		256色モードの挙動から予想すると、色判定には影響しない＆65536色の場合抜き色判定が
		発生しないため、恐らく現実装と挙動は変わらない？
	*/

	// 一般的な使用法の場合は簡素化しておく
	if ( ((vctrl1&0xFF)==0xE4) && (scrx0==scrx1) && (scrx0==scrx2) && (scrx0==scrx3) && (scry0==scry1) && (scry0==scry2) && (scry0==scry3) ) {
		// 一般的な65536色モードの使用方法
		const UINT16* src0 = vid->gvram + ( ((line+scry0)&0x1FF) << 9 );  // 512 WORDS / line
		UINT32 sx0 = scrx0 & 0x1FF;
		UINT32 dx = 0;
		UINT32* dst = LINEBUF_GR + LINEBUF_OFS;

		for ( ; dx<w; dx++, sx0=(sx0+1)&0x1FF) {
			const UINT32 pix = src0[sx0];
			UINT32 l = pix & 0xFF;
			UINT32 h = pix >> 8;
			UINT32 c;
			c  = pal[((l>>1)<<2)+(PIX_COND16^LE_ADDR_SWAP)+0];
			c |= pal[((h>>1)<<2)+((h&1)     ^LE_ADDR_SWAP)+2] << 8;
			*dst++ = c | TOP_EXBIT16;
		}
	} else {
		// スクロール非同期・特殊使用法
		const UINT16* src0 = vid->gvram + ( ((line+scry0)&0x1FF) << 9 );  // 512 WORDS / line
		const UINT16* src1 = vid->gvram + ( ((line+scry1)&0x1FF) << 9 );
		const UINT16* src2 = vid->gvram + ( ((line+scry2)&0x1FF) << 9 );
		const UINT16* src3 = vid->gvram + ( ((line+scry3)&0x1FF) << 9 );
		UINT32 sx0 = scrx0 & 0x1FF;
		UINT32 sx1 = scrx1 & 0x1FF;
		UINT32 sx2 = scrx2 & 0x1FF;
		UINT32 sx3 = scrx3 & 0x1FF;
		UINT32 dx = 0;
		UINT32* dst = LINEBUF_GR + LINEBUF_OFS;

		for ( ; dx<w; dx++, sx0=(sx0+1)&0x1FF, sx1=(sx1+1)&0x1FF, sx2=(sx2+1)&0x1FF, sx3=(sx3+1)&0x1FF) {
			const UINT32 d0 = src0[sx0];
			const UINT32 d1 = src1[sx1];
			const UINT32 d2 = src2[sx2];
			const UINT32 d3 = src3[sx3];
			UINT32 l = ((d0>>p0)&0x0F) | (((d1>>p1)&0x0F)<<4);
			UINT32 h = ((d2>>p2)&0x0F) | (((d3>>p3)&0x0F)<<4);
			UINT32 c;
			c  = pal[((l>>1)<<2)+(PIX_COND16^LE_ADDR_SWAP)+0];
			c |= pal[((h>>1)<<2)+((h&1)     ^LE_ADDR_SWAP)+2] << 8;
			*dst++ = c | TOP_EXBIT16;
		}
	}
}


#undef PIX_COND
#undef TOP_EXBIT
#undef LOW_EXBIT
#undef PIX_COND16
#undef TOP_EXBIT16
#undef MODE3_OPAQ
#undef MODE3_NORMAL

#undef GR_DRAW_MODE
#undef GRFUNC_DRAW_4BIT_H
#undef GRFUNC_DRAW_4BIT
#undef GRFUNC_DRAW_8BIT
#undef GRFUNC_DRAW_16BIT
