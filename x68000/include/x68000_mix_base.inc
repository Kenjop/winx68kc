// --------------------------------------------------------------------------
//   1ライン合成
// --------------------------------------------------------------------------
/*
	GR/SP(TX)の内部合成時は、カラー番号で透明処理（Index#0が透明）。
	画面間の透明色判定はカラー番号ではなく、パレットカラーで決定されるっぽい？
	（0x0000 が透明扱い）。
	
	特殊プライオリティに関しては、0番カラー以外の 0x0000 は黒として扱われる模様
	（ツインビー）。
*/

// --------------------------------------------------------------------------
//   合成関数本体
// --------------------------------------------------------------------------
static void FASTCALL FUNC_NAME(X68000_VIDEO* vid, const UINT32 line)
{
#if defined(GR_MIDDLE)
	const UINT32 gr_mask = vid->mix_func_grmask;
#endif
	const UINT32 w = _MIN(vid->h_sz, vid->vscr_w);
	UINT16* dst = VSCR + (line*vid->vscr_w);
	UINT32 x;

	// ----------------------
	// ラインバッファ用意
	// ----------------------
#ifdef LINEBUF_1ST
	const UINT32* buf1 = LINEBUF_1ST + LINEBUF_OFS;
#endif
#ifdef LINEBUF_2ND
	const UINT32* buf2 = LINEBUF_2ND + LINEBUF_OFS;
#endif

	// ----------------------
	// 半透明対象フラグ
	// ----------------------
#if ENABLE_TRANS
	const UINT32 tr_target = ( ( vid->vctrl2 & 0x0100 ) ? (VID_PLANE_TX|VID_PLANE_TX0|VID_PLANE_SP) : 0 );
#endif

	// ----------------------
	// TX0半透明用のTX0固定値
	// ----------------------
#if ENABLE_TRTX0
	const UINT32 tx0 = vid->pal[0x100];
#endif

	// ----------------------
	// ここから合成処理
	// ----------------------
	for (x=0; x < w; x++)
	{
#if defined(LINEBUF_1ST) && defined(LINEBUF_2ND)
		// 2面ある場合

		UINT32 c = buf2[x];
		UINT32 n = buf1[x];

#if defined(GR_TOP) || defined(GR_MIDDLE)
	// c=TX/SP, n=GR

#if defined(GR_TOP)
	// GR=TOPならば描画条件は特にない
	#define PIXEL_DRAW_ENABLE  1  // 「if ( 1 ) ～」の形の条件式は最適化が消してくれるはず
#else
	// GR=MIDならばTXかSPのどちらかでのマスク条件考慮
	#define PIXEL_DRAW_ENABLE  !( c & 0xFFFF ) || !( c & gr_mask )  // TX/SPが透明色か、マスク対象ではない
#endif

	#if ENABLE_TRANS
		if ( PIXEL_DRAW_ENABLE ) {
			if ( ( n & VID_PLANE_EX ) && ( c & tr_target ) ) {
				c = ADD_TRANS_PIXEL(c, n);
			} else if ( n & 0xFFFF ) {
				c = n;
			}
		}
	#elif ENABLE_TRTX0
		if ( PIXEL_DRAW_ENABLE ) {  // TX0半透明はGR全体（透明色も含む）に掛かる？
			c = ADD_TRANS_PIXEL(tx0, n);
		}
	#elif ENABLE_SPPRI
		if ( n & VID_PLANE_EX ) {  // TX/SPの状態に関係なく、まず特殊PRIピクセルかをチェック
			c = n;  // 特殊PRIピクセルは黒（#$0000）でも不透過
		} else if ( PIXEL_DRAW_ENABLE ) {
			if ( n & 0xFFFF ) {
				c = n;
			}
		}
	#else
		if ( PIXEL_DRAW_ENABLE ) {
			if ( n & 0xFFFF ) {
				c = n;
			}
		}
	#endif

#else  // of defined(GR_TOP) || defined(GR_MIDDLE)
	// c=GR, n=TX/SP に入れ替わるので注意  XXX 固定化した方が読みやすいか

	// GR=BOTTOMならば、TX/SPが描画できるかどうかの条件
	#define PIXEL_DRAW_ENABLE  ( n & 0xFFFF )  // TX/SPが色付き（TX0かのチェックは不要（伊忍道））

	#if ENABLE_SPPRI  // 特殊PRIのみ、cが最前面かどうかの確認が必要
		if ( PIXEL_DRAW_ENABLE && !( c & VID_PLANE_EX ) ) {
			c = n;
		}
	#else
		if ( PIXEL_DRAW_ENABLE ) {
			c = n;
		}
	#if ENABLE_TRTX0
		else {  // TX/SPがない場所（GR）にTX0合成
			c = ADD_TRANS_PIXEL(tx0, c);
		}
	#endif
	#endif

#endif  // of  defined(GR_TOP) || defined(GR_MIDDLE)

#elif defined(LINEBUF_1ST)
		// 上面だけ
		UINT32 c = buf1[x];
#elif defined(LINEBUF_2ND)
		// 下面だけ
		UINT32 c = buf2[x];
#else
		// どちらもない
		UINT32 c = 0;
#endif

		// ----------------------
		// 最終値を書きだし
		// ----------------------
		*dst++ = (UINT16)c;
	}
}

#undef FUNC_NAME
#undef LINEBUF_1ST
#undef LINEBUF_2ND
#undef PIXEL_DRAW_ENABLE
