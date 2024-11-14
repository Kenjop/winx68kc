// --------------------------------------------------------------------------
//   GR�ʃ��C������
// --------------------------------------------------------------------------

/*
	EXON�AB/P�AG/G �ɂ����4��ނɕ������

	EXON=0 (MODE=0)
	  �� �ŉ��ʃr�b�g�͂���AEX�t���O�Ȃ��i�ʏ�`��j

	EXON=1
	  �� B/P=1 (MODE=1)
	       �� �ŉ��ʃr�b�g�����AEX�t���O��1st���C���̍ŉ��ʃr�b�g�i�ʏ�EX�j
	  �� B/P=0 G/G�Ȃ� (MODE=2)
	       �� �ŉ��ʃr�b�g�͂���AEX�t���O�͑S���C����I�r�b�g�i����EX1�j
	  �� B/P=0 G/G���� (MODE=3)
	       �� �ŉ��ʃr�b�g�����A����J���[�Q�ƁAEX�t���O�͑S���C����I�r�b�g�i����EX2�j
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
//   ��������֐��{�̒�`
// ------------------------------------
static void GRFUNC_DRAW_4BIT_H(X68000_VIDEO* vid, UINT32 line)
{
	// 16�F 1024x1024 1�ʃ��[�h
	// �uJESUS II�v��OP�Ȃ�
	const UINT16* pal = vid->pal;
	const UINT32 w = vid->h_sz;
	const UINT32 scrx = CRTC(12);  // �X�N���[�����W�X�^�́iInside X68000 ��M����Ȃ�j0�ԂŌŒ�
	const UINT32 scry = CRTC(13);
	const UINT32 sy = ( line + scry ) & 0x3FF;  // �Q�ƃ��C���i0�`1023�j
	const UINT32 basesft = ( sy & 0x200 ) >> 6; // 0�`511:GD0/1(sft=0), 512�`1023:GD2/3(sft=8) 
	const UINT16* src = vid->gvram + ( ( sy & 0x1FF ) << 9 );  // 512 WORDS / line
	UINT32 sx = scrx & 0x3FF;  // �J�nX���W�i0�`1023�j
	UINT32 dx = 0;
	UINT32* dst = LINEBUF_GR + LINEBUF_OFS;

	for ( ; dx<w; dx++, sx=(sx+1)&0x3FF) {
		const UINT32 sft = ( sx & 0x200 ) >> 7;  // 0�`511:GD0/2(sft=0), 512�`1023:GD1/3(sft=4) 
		const UINT32 p = basesft + sft;
		const UINT32 d = src[sx&0x1FF];
		const UINT32 pix = ( d >> p ) & 0x0F;
		*dst = pal[PIX_COND] | TOP_EXBIT;
		dst++;
	}
}

static void GRFUNC_DRAW_4BIT(X68000_VIDEO* vid, UINT32 line, UINT32 layer, BOOL opaq)
{
	// 16�F 4�ʃ��[�h
	// �h���L����Stage6�`�{�X�ӂ�Ȃ�
	const UINT16* pal = vid->pal;
	const UINT32 w = vid->h_sz;
	const UINT32 vctrl1 = vid->vctrl1 >> ( layer*2 );
	const UINT32 p = ( vctrl1 & 3 ) << 2;
	const UINT32 scrx = CRTC(12+(p>>1));       // �X�N���[�����W�X�^��GD�ԍ��ɑΉ�
	const UINT32 scry = CRTC(13+(p>>1));
	const UINT16* src = vid->gvram + ( ((line+scry)&0x1FF) << 9 );  // 512 WORDS / line
	UINT32 sx = scrx & 0x1FF;
	UINT32 dx = 0;
	UINT32* dst = LINEBUF_GR + LINEBUF_OFS;

	if ( layer==0 ) {
		// �g�b�v���C��
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
		// 2nd�ȉ�
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
	// 256�F 2�ʃ��[�h
	// �㉺ nibble �ŃX�N���[���l���قȂ�ꍇ���l���i�X�N���[���l����v����ꍇ�͏����𕪂��������������A�R�[�h��������̂Łc�j
	const UINT16* pal = vid->pal;
	const UINT32 w = vid->h_sz;
	const UINT32 vctrl1 = vid->vctrl1 >> ( ( layer ) ? 4 : 0 );
	const UINT32 gd0 = (vctrl1>>0) & 3;      // �Q�Ƃ���GD
	const UINT32 gd1 = (vctrl1>>2) & 3;
	const UINT32 p0 = gd0 << 2;              // ���ꂼ��� GD �̃r�b�g�V�t�g�l
	const UINT32 p1 = gd1 << 2;
	const UINT32 scrx0 = CRTC(12+(gd0<<1));  // �X�N���[�����W�X�^��GD�ԍ��ɑΉ��i�h���L���� Stage1 �`���Ȃǁj
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
		�Е��� GD ���� ON �̂悤�ȓ����Ԃ̋���
		�uHEAVY NOVA�v�̋������������A�p���b�g�Q�Ƃ̍ۂɂ͎g�p����邪�A�Ή���GR�ʂ̔����F�i0�ԁj
		����̍ۂɂ� OFF �� GD �̃r�b�g�Ɋւ��Ă� 0 �Ƃ��Ĉ�����H

		��jGD0=0x0 �� ON�AGD1=0x1 �� OFF�A�̏ꍇ�A�p���b�g�� pal[0x10] ���Q�Ƃ���邪�A�����F�����
			�� 0x00 �Ƃ݂Ȃ���AIndex#0 �Ƃ��Ĕ����F�ɂȂ�H
	*/
	const UINT32 pix_mask = ( ( vid->vctrl2 & (1<<0) ) ? 0x0F : 0x00 ) | ( ( vid->vctrl2 & (1<<1) ) ? 0xF0 : 0x00 );

	if ( layer==0 ) {
		// �g�b�v���C��
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
	// 65536�F 1�ʃ��[�h
	// �d�g��Ys�Ȃ�
	const UINT8* pal = (const UINT8*)vid->pal;
	const UINT32 w = vid->h_sz;
	const UINT32 vctrl1 = vid->vctrl1;
	const UINT32 p0 = ( (vctrl1>>0) & 3 ) << 2;  // �Q�Ƃ���GD�i���ł̃r�b�g�V�t�g�p�� 0/4/8/12 �ɕϊ����Ă���j
	const UINT32 p1 = ( (vctrl1>>2) & 3 ) << 2;
	const UINT32 p2 = ( (vctrl1>>4) & 3 ) << 2;
	const UINT32 p3 = ( (vctrl1>>6) & 3 ) << 2;
	const UINT32 scrx0 = CRTC(12+(p0>>1));       // �X�N���[�����W�X�^��GD�ԍ��ɑΉ�
	const UINT32 scrx1 = CRTC(12+(p1>>1));
	const UINT32 scrx2 = CRTC(12+(p2>>1));
	const UINT32 scrx3 = CRTC(12+(p3>>1));
	const UINT32 scry0 = CRTC(13+(p0>>1));
	const UINT32 scry1 = CRTC(13+(p1>>1));
	const UINT32 scry2 = CRTC(13+(p2>>1));
	const UINT32 scry3 = CRTC(13+(p3>>1));
	/*
		XXX
		GD0�`GD3 �̈ꕔ���� ON �̂悤�ȓ����Ԃ͍l�����Ă��Ȃ�
		256�F���[�h�̋�������\�z����ƁA�F����ɂ͉e�����Ȃ���65536�F�̏ꍇ�����F���肪
		�������Ȃ����߁A���炭�������Ƌ����͕ς��Ȃ��H
	*/

	// ��ʓI�Ȏg�p�@�̏ꍇ�͊ȑf�����Ă���
	if ( ((vctrl1&0xFF)==0xE4) && (scrx0==scrx1) && (scrx0==scrx2) && (scrx0==scrx3) && (scry0==scry1) && (scry0==scry2) && (scry0==scry3) ) {
		// ��ʓI��65536�F���[�h�̎g�p���@
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
		// �X�N���[���񓯊��E����g�p�@
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