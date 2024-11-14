// --------------------------------------------------------------------------
//   GR面半透明MIX処理
// --------------------------------------------------------------------------

/*
	B/Pによって2種類に分かれる

	B/P=1
	  → c1&1 なら、pal[c1&~1] と pal[c2&|1] で合成
	     1面のみの場合、c&1なら pal[c&~1] と pal[c|1] で合成
	
	B/P=0
	  → Iビットが特殊フラグとして機能する、という点以外複雑すぎて詳細不明
	     現状の処理は下の実装参照
*/


#ifdef GR_MIX_BP1  // B/P=1 通常動作

	#define BLEND_PIX(_p0mask,_p1mask) \
		const UINT32 _p0 = pix0 & ~1;                          \
		const UINT32 _p1 = pix1 & ~1;                          \
	    if ( _p0 & _p0mask ) {                                 \
	        if ( pix0 & 1 ) {                                  \
	            const UINT32 c0 = pal[_p0  ];                  \
	            const UINT32 c1 = pal[_p1|1];                  \
	            *dst = ADD_TRANS_PIXEL(c0, c1) | VID_PLANE_EX; \
	        } else {                                           \
	            *dst = pal[_p0];                               \
	        }                                                  \
	    }

	#define BLEND_PIX_ELSE_OPAQ(_p1mask) \
	    else {                                                 \
	        *dst = ( _p1 & _p1mask ) ? pal[_p1] : pal[0];      \
	    }

	#define BLEND_PIX_ELSE_NORMAL(_p1mask) \
	    else {                                                 \
	        if ( _p1 & _p1mask ) *dst = pal[_p1];              \
	    }

	/* 未検証 16bitはテストが面倒すぎる… */
	#define BLEND_PIX16() \
	    if ( l & 1 ) {                                         \
	        UINT32 c0, c1;                                     \
	        c0  = pal[((h>>1)<<2)+((h&1)^LE_ADDR_SWAP)+2] << 8; \
	        c1  = c0;                                          \
	        c0 |= pal[((l>>1)<<2)+(0    ^LE_ADDR_SWAP)+0];     \
	        c1 |= pal[((l>>1)<<2)+(1    ^LE_ADDR_SWAP)+2];     \
	        *dst = ADD_TRANS_PIXEL(c0, c1) | VID_PLANE_EX;     \
	    } else {                                               \
	        UINT32 c0;                                         \
	        c0  = pal[((h>>1)<<2)+((h&1)^LE_ADDR_SWAP)+2] << 8; \
	        c0 |= pal[((l>>1)<<2)+(0    ^LE_ADDR_SWAP)+0];     \
	        *dst = c0;                                         \
	    }

#else              // B/P=0 特殊動作

	// もう理屈が分からん
	// 実機検証の限りは割と近いはず。もう少しロジカルにできそうな気もするんだけど…

	#define BLEND_PIX(_p0mask,_p1mask) \
		const UINT32 _p0 = pix0 & ~1; /* 最下位無視でのIndex */\
		const UINT32 _p1 = pix1 & ~1;                          \
	    const UINT32 c0 = pal[_p0];   /* 最下位無視での色 */   \
	    const UINT32 c1 = pal[_p1];                            \
	    const UINT32 i0 = c0 & 1;     /* 上記色のIビット */    \
	    const UINT32 i1 = c1 & 1;                              \
	    if ( pix0 & _p0mask ) {                                \
	        if ( ( pix0 & 1 ) && ( !i0 || (pix0==1) ) ) {      \
	            if ( pix1 ) {                                  \
	                if ( i0 & i1 ) {                           \
	                    *dst = ADD_TRANS_PIXEL(c0, pal[_p1|1]) | ((i0&i1)<<VID_SHIFT_EX); \
	                } else {                                   \
	                    *dst = pal[_p1|1];                     \
	                }                                          \
	            } else {                                       \
	                *dst = pal[1] | ((i0&i1)<<VID_SHIFT_EX);   \
	            }                                              \
	        } else {                                           \
	            if ( i0 ) {                                    \
	                if ( pix1 ) {                              \
	                    *dst = ADD_TRANS_PIXEL(c0, pal[_p1|1]) | (i0<<VID_SHIFT_EX); \
	                } else {                                   \
	                    *dst = ADD_TRANS_PIXEL(c0, pal[1]) | (i0<<VID_SHIFT_EX); \
	                }                                          \
	            } else {                                       \
	                *dst = c0;                                 \
	            }                                              \
	        }                                                  \
	    }

	#define BLEND_PIX_ELSE_OPAQ(_p1mask) \
	    else {                                                 \
	        if ( pix1 ) {                                      \
	            if ( i1 ) {                                    \
	                if ( _p1 & _p1mask ) {                    \
	                    *dst = ADD_TRANS_PIXEL(c1, pal[_p1|1]) | (i1<<VID_SHIFT_EX); \
	                } else {                                   \
	                    *dst = ADD_TRANS_PIXEL(c1, pal[1]) | (i1<<VID_SHIFT_EX); \
	                }                                          \
	            } else {                                       \
	                *dst = pal[pix1] | (i1<<VID_SHIFT_EX); \
	            }                                              \
	        } else {                                           \
	            *dst = pal[0] | (i0<<VID_SHIFT_EX);            \
	        }                                                  \
	    }

	#define BLEND_PIX_ELSE_NORMAL(_p1mask) \
	    else {                                                 \
	        if ( pix1 ) {                                      \
	            if ( i1 ) {                                    \
	                if ( _p1 & _p1mask ) {                    \
	                    *dst = ADD_TRANS_PIXEL(c1, pal[_p1|1]) | (i1<<VID_SHIFT_EX); \
	                 } else {                                  \
	                    *dst = ADD_TRANS_PIXEL(c1, pal[1]) | (i1<<VID_SHIFT_EX); \
	                 }                                         \
	            } else {                                       \
	                *dst = pal[pix1] | (i1<<VID_SHIFT_EX); \
	            }                                              \
	        }                                                  \
	    }

	/* 未検証 */
	#define BLEND_PIX16() \
	    UINT32 c0, c1;                                         \
	    c0  = pal[((h>>1)<<2)+((h&1)^LE_ADDR_SWAP)+2] << 8;    \
	    c0 |= pal[((l>>1)<<2)+(0    ^LE_ADDR_SWAP)+0];         \
	    c1  = c0;                                              \
	    if ( l || h ) {                                        \
	        if ( c0 & 1 ) {  /* I=1 */                         \
	            *dst = ADD_TRANS_PIXEL(c0, c1) | VID_PLANE_EX; \
	        } else {         /* I=0 */                         \
	            *dst = c0;                                     \
	        }                                                  \
	    } else {                                               \
	        *dst = pal[0] | ((pal[0]&1)<<VID_SHIFT_EX);        \
	    }                                                      \

#endif


// ------------------------------------
//   ここから関数本体定義
// ------------------------------------
static void GRFUNC_MIX_4BIT_H(X68000_VIDEO* vid, UINT32 line)
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
		const UINT32 pix0 = ( d >> p ) & 0x0F;
		const UINT32 pix1 = pix0;
		BLEND_PIX( ~0, ~0 )
		BLEND_PIX_ELSE_OPAQ( ~0 )
		dst++;
	}
}

static void GRFUNC_MIX_4BIT(X68000_VIDEO* vid, UINT32 line, BOOL opaq)
{
	// 16色 4面モード 上位2面のG/G半透明
	const UINT16* pal = vid->pal;
	const UINT32 w = vid->h_sz;
	const UINT32 vctrl1 = vid->vctrl1;
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
	// ここが呼ばれる時点で pix0 の方が非表示な状態は存在しない。よって pix1 のみ判定
	const UINT32 pix1_mask = ( vid->vctrl2 & (1<<1) ) ? 0x0F : 0x00;

	if ( opaq ) {
		for ( ; dx<w; dx++, sx0=(sx0+1)&0x1FF, sx1=(sx1+1)&0x1FF) {
			const UINT32 d0 = src0[sx0];
			const UINT32 d1 = src1[sx1];
			const UINT32 pix0 = ( d0 >> p0 ) & 0x0F;
			const UINT32 pix1 = ( d1 >> p1 ) & 0x0F;
			// 非表示時は透明扱い（ドラキュラStage6骨ドラボス）
			BLEND_PIX( ~0, pix1_mask )
			BLEND_PIX_ELSE_OPAQ( pix1_mask )    // OPAQ扱い
			dst++;
		}
	} else {
		for ( ; dx<w; dx++, sx0=(sx0+1)&0x1FF, sx1=(sx1+1)&0x1FF) {
			const UINT32 d0 = src0[sx0];
			const UINT32 d1 = src1[sx1];
			const UINT32 pix0 = ( d0 >> p0 ) & 0x0F;
			const UINT32 pix1 = ( d1 >> p1 ) & 0x0F;
			// 非表示時は透明扱い（ドラキュラStage6骨ドラボス）
			BLEND_PIX( ~0, pix1_mask )
			BLEND_PIX_ELSE_NORMAL( pix1_mask )  // 抜き色透過
			dst++;
		}
	}
}

static void GRFUNC_MIX_8BIT(X68000_VIDEO* vid, UINT32 line)
{
	// 256色 2面モード G/G半透明
	const UINT16* pal = vid->pal;
	const UINT32 w = vid->h_sz;
	const UINT32 vctrl1 = vid->vctrl1;
	const UINT32 gd0 = (vctrl1>>0) & 3;      // 参照するGD
	const UINT32 gd1 = (vctrl1>>2) & 3;
	const UINT32 gd2 = (vctrl1>>4) & 3;
	const UINT32 gd3 = (vctrl1>>6) & 3;
	const UINT32 p0 = gd0 << 2;              // それぞれの GD のビットシフト値
	const UINT32 p1 = gd1 << 2;
	const UINT32 p2 = gd2 << 2;
	const UINT32 p3 = gd3 << 2;
	const UINT32 scrx0 = CRTC(12+(gd0<<1));  // スクロールレジスタはGD番号に対応（ドラキュラ Stage1 冒頭など）
	const UINT32 scrx1 = CRTC(12+(gd1<<1));
	const UINT32 scrx2 = CRTC(12+(gd2<<1));
	const UINT32 scrx3 = CRTC(12+(gd3<<1));
	const UINT32 scry0 = CRTC(13+(gd0<<1));
	const UINT32 scry1 = CRTC(13+(gd1<<1));
	const UINT32 scry2 = CRTC(13+(gd2<<1));
	const UINT32 scry3 = CRTC(13+(gd3<<1));
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
	const UINT32 pix0_mask = ( ( vid->vctrl2 & (1<<0) ) ? 0x0F : 0x00 ) | ( ( vid->vctrl2 & (1<<1) ) ? 0xF0 : 0x00 );
	const UINT32 pix1_mask = ( ( vid->vctrl2 & (1<<2) ) ? 0x0F : 0x00 ) | ( ( vid->vctrl2 & (1<<3) ) ? 0xF0 : 0x00 );

	for ( ; dx<w; dx++, sx0=(sx0+1)&0x1FF, sx1=(sx1+1)&0x1FF, sx2=(sx2+1)&0x1FF, sx3=(sx3+1)&0x1FF) {
		const UINT32 d0 = src0[sx0];
		const UINT32 d1 = src1[sx1];
		const UINT32 d2 = src2[sx2];
		const UINT32 d3 = src3[sx3];
		const UINT32 pix0 = ((d0>>p0)&0x0F) | (((d1>>p1)&0x0F)<<4);
		const UINT32 pix1 = ((d2>>p2)&0x0F) | (((d3>>p3)&0x0F)<<4);
		BLEND_PIX( pix0_mask, pix1_mask )
		BLEND_PIX_ELSE_OPAQ( pix1_mask )  // 256色は2面なので必ずOPAQ扱い
		dst++;
	}
}

static void GRFUNC_MIX_16BIT(X68000_VIDEO* vid, UINT32 line)
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
			BLEND_PIX16()
			dst++;
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
			BLEND_PIX16()
			dst++;
		}
	}
}


#undef BLEND_PIX
#undef BLEND_PIX_ELSE_OPAQ
#undef BLEND_PIX_ELSE_NORMAL
#undef BLEND_PIX16

#undef GR_MIX_BP1
#undef GRFUNC_MIX_4BIT_H
#undef GRFUNC_MIX_4BIT
#undef GRFUNC_MIX_8BIT
#undef GRFUNC_MIX_16BIT
