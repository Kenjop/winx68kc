// --------------------------------------------------------------------------
//   1���C������
// --------------------------------------------------------------------------
/*
	GR/SP(TX)�̓����������́A�J���[�ԍ��œ��������iIndex#0�������j�B
	��ʊԂ̓����F����̓J���[�ԍ��ł͂Ȃ��A�p���b�g�J���[�Ō��肳�����ۂ��H
	�i0x0000 �����������j�B
	
	����v���C�I���e�B�Ɋւ��ẮA0�ԃJ���[�ȊO�� 0x0000 �͍��Ƃ��Ĉ�����͗l
	�i�c�C���r�[�j�B
*/

// --------------------------------------------------------------------------
//   �����֐��{��
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
	// ���C���o�b�t�@�p��
	// ----------------------
#ifdef LINEBUF_1ST
	const UINT32* buf1 = LINEBUF_1ST + LINEBUF_OFS;
#endif
#ifdef LINEBUF_2ND
	const UINT32* buf2 = LINEBUF_2ND + LINEBUF_OFS;
#endif

	// ----------------------
	// �������Ώۃt���O
	// ----------------------
#if ENABLE_TRANS
	const UINT32 tr_target = ( ( vid->vctrl2 & 0x0100 ) ? (VID_PLANE_TX|VID_PLANE_TX0|VID_PLANE_SP) : 0 );
#endif

	// ----------------------
	// TX0�������p��TX0�Œ�l
	// ----------------------
#if ENABLE_TRTX0
	const UINT32 tx0 = vid->pal[0x100];
#endif

	// ----------------------
	// �������獇������
	// ----------------------
	for (x=0; x < w; x++)
	{
#if defined(LINEBUF_1ST) && defined(LINEBUF_2ND)
		// 2�ʂ���ꍇ

		UINT32 c = buf2[x];
		UINT32 n = buf1[x];

#if defined(GR_TOP) || defined(GR_MIDDLE)
	// c=TX/SP, n=GR

#if defined(GR_TOP)
	// GR=TOP�Ȃ�Ε`������͓��ɂȂ�
	#define PIXEL_DRAW_ENABLE  1  // �uif ( 1 ) �`�v�̌`�̏������͍œK���������Ă����͂�
#else
	// GR=MID�Ȃ��TX��SP�̂ǂ��炩�ł̃}�X�N�����l��
	#define PIXEL_DRAW_ENABLE  !( c & 0xFFFF ) || !( c & gr_mask )  // TX/SP�������F���A�}�X�N�Ώۂł͂Ȃ�
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
		if ( PIXEL_DRAW_ENABLE ) {  // TX0��������GR�S�́i�����F���܂ށj�Ɋ|����H
			c = ADD_TRANS_PIXEL(tx0, n);
		}
	#elif ENABLE_SPPRI
		if ( n & VID_PLANE_EX ) {  // TX/SP�̏�ԂɊ֌W�Ȃ��A�܂�����PRI�s�N�Z�������`�F�b�N
			c = n;  // ����PRI�s�N�Z���͍��i#$0000�j�ł��s����
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
	// c=GR, n=TX/SP �ɓ���ւ��̂Œ���  XXX �Œ艻���������ǂ݂₷����

	// GR=BOTTOM�Ȃ�΁ATX/SP���`��ł��邩�ǂ����̏���
	#define PIXEL_DRAW_ENABLE  ( n & 0xFFFF )  // TX/SP���F�t���iTX0���̃`�F�b�N�͕s�v�i�ɔE���j�j

	#if ENABLE_SPPRI  // ����PRI�̂݁Ac���őO�ʂ��ǂ����̊m�F���K�v
		if ( PIXEL_DRAW_ENABLE && !( c & VID_PLANE_EX ) ) {
			c = n;
		}
	#else
		if ( PIXEL_DRAW_ENABLE ) {
			c = n;
		}
	#if ENABLE_TRTX0
		else {  // TX/SP���Ȃ��ꏊ�iGR�j��TX0����
			c = ADD_TRANS_PIXEL(tx0, c);
		}
	#endif
	#endif

#endif  // of  defined(GR_TOP) || defined(GR_MIDDLE)

#elif defined(LINEBUF_1ST)
		// ��ʂ���
		UINT32 c = buf1[x];
#elif defined(LINEBUF_2ND)
		// ���ʂ���
		UINT32 c = buf2[x];
#else
		// �ǂ�����Ȃ�
		UINT32 c = 0;
#endif

		// ----------------------
		// �ŏI�l����������
		// ----------------------
		*dst++ = (UINT16)c;
	}
}

#undef FUNC_NAME
#undef LINEBUF_1ST
#undef LINEBUF_2ND
#undef PIXEL_DRAW_ENABLE