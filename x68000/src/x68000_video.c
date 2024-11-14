/* -----------------------------------------------------------------------------------
  "SHARP X68000" Video Driver
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "osconfig.h"
#include "x68000_driver.h"
#include "x68000_video.h"

/*
	Vctrl2 �� EXON=1 ���� B/P=0 �ɐݒ肵���ۂ̋������ؕs���i���G������j
*/

/*
	MIX�����Ŏg�p���郉�C���o�b�t�@�\���F
	*****--- ****---- -------- --------  �󂫁i�����I�ɕK�v�ɂȂ����ꍇ�g���j
	-----PPP -------- -------- --------  SP/BG�̍����p�����v���C�I���e�B
	-------- ----E--- -------- --------  EX�t���O�i�������E����PRI�̑Ώۃs�N�Z���t���O�j
	-------- -----S-- -------- --------  SP�ʃt���O�iGR�ʂ̃}�X�N�A�y�є������Ώ۔���Ŏg���j
	-------- ------T- -------- --------  TX�ʃt���O�iGR�ʂ̃}�X�N�A�y�є������Ώ۔���Ŏg���j
	-------- -------X -------- --------  TX#0�ԃJ���[�t���O�i�o�b�N�h���b�v�����p�j
	-------- -------- CCCCCCCC CCCCCCCC  �s�N�Z���J���[�i�p���b�g�Q�ƌ��16bit�j
*/

// --------------------------------------------------------------------------
//   �萔��`��
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

#define LINEBUF_OFS    16  // �������ȗ������邽�߂̃p�f�B���O
#define CRTC(r)        (READBEWORD(&vid->crtc[(r)*2]))

// BGRI5551 2�l�̉��Z������
#define ADD_TRANS_PIXEL(_a,_b)  ( ( ( ((_a)&~0xFFFF0842) + ((_b)&~0xFFFF0842) ) >> 1 ) + ( ( (_a) & (_b) ) & 0x0842 ) )


// --------------------------------------------------------------------------
//   �����g�p��static�ϐ��i�X�e�[�g�ۑ��s�v�Ȃ��́j
// --------------------------------------------------------------------------
// ���C�����Ƃ̈ꎞ�o�̓��C���o�b�t�@�i�ő�1024�h�b�g�{���E16�h�b�g�p�f�B���O���m�ہj
static UINT32 LINEBUF_GR[1024+LINEBUF_OFS*2];
static UINT32 LINEBUF_TX[1024+LINEBUF_OFS*2];

// ���C���o�b�t�@�W�J�ɂ����āAGR ���C���o�b�t�@�̈�ԉ��̃v���[�����ǂ���
static BOOL GR_OPAQ = FALSE;

// ���C���o�b�t�@�W�J�ɂ����āATX ���C���o�b�t�@�̈�ԉ��̃v���[�����ǂ���
static BOOL TX_OPAQ = FALSE;

// MIX�֐��e�[�u���ւ̃|�C���^
static void (FASTCALL **X68K_MIX_FUNC)(X68000_VIDEO*,const UINT32) = NULL;


// --------------------------------------------------------------------------
//   �����g�p��static�ϐ��i�X�e�[�g�ۑ��K�v�j
// --------------------------------------------------------------------------
// ���C���`�挋�ʂ����ߍ��މ��z�X�N���[���i�e�B�A�����O���p�j
static UINT16  VSCR[ X68_MAX_VSCR_WIDTH * X68_MAX_VSCR_HEIGHT ];


// --------------------------------------------------------------------------
//   ���C�������}�N��
// --------------------------------------------------------------------------
#include "x68000_mix_normal.inc"  // �ʏ�
#include "x68000_mix_sppri.inc"   // ����v���C�I���e�B
#include "x68000_mix_trans.inc"   // ������
#include "x68000_mix_trtx0.inc"   // �������iTX0�j

static void (FASTCALL **MIX_FUNC_TABLE[4])(X68000_VIDEO*,const UINT32) = {
	MIX_N, MIX_S, MIX_T, MIX_0
};

enum {  // ��̕��тƈ�v�����邱��
	MIX_FUNC_IDX_N = 0,
	MIX_FUNC_IDX_S,
	MIX_FUNC_IDX_T,
	MIX_FUNC_IDX_0,
};


// --------------------------------------------------------------------------
//   �e�[�u��
// --------------------------------------------------------------------------
// GRBI5551��ARGB8888 �ϊ��p�i�P�x���Ƃ�16�i�K�p�Ӂj
static UINT32 PAL2RGB[16][0x10000];

// Planed��Packed �ϊ��p�iTX�W�J�p�A�e�[�u���g�������኱�����j
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
//   �e�L�X�g��
// --------------------------------------------------------------------------
static void TxLineUpdate(X68000_VIDEO* vid, UINT32 line)
{
	// dirty �������Ă��郉�C���́Aplaned -> packed �ϊ��iplaned �͈����Â炢�̂Łj
	const UINT32 pf = 1 << (line&31);
	UINT32* pd = &vid->tx_dirty[line>>5];

	if ( pd[0] & pf )  // Dirty�Ȃ�
	{
		const UINT16* src = vid->tvram + (line<<6);  // 64 words / line
		UINT32* dst = vid->tx_buf + (line<<7); // 128 dwords / line
		UINT32 w = 1024/16;

		do {
			// 16bit���ōŏ�ʂ���ʍ�
			const UINT32 s0 = src[0x00000];  // UINT16* �Ȃ̂ŁA1�v���[���ӂ� 64k unit
			const UINT32 s1 = src[0x10000];
			const UINT32 s2 = src[0x20000];
			const UINT32 s3 = src[0x30000];
			UINT32 o;
			/*
				�������A�N�Z�X�����Ă��e�[�u���g�������������ۂ�
				64bit�e�[�u����16bit���Z�߂ď�������̂͂��܂���ʂȂ��ix64���ł̃`�F�b�N�AWin32���ł͒x���Ȃ邩���j
				�����32bit�e�[�u����2��ix 4�v���[���j�Q�Ƃ���`�ɂ��Ă���
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

		// Dirty�𗎂Ƃ�
		pd[0] &= ~pf;
	}
}

static void TxDrawLine(X68000_VIDEO* vid, UINT32 line)
{
	const UINT32 vctrl2 = vid->vctrl2;
	if ( vctrl2 & 0x0020 ) {
		// �X�N���[���l���Q�Ƃ��āA��ʃ��C����VRAM���C�����v�Z
		const UINT32 scrx = CRTC(10);
		const UINT32 scry = CRTC(11);
		const UINT32 vram_line = ( scry + line ) & 0x3FF;

		// VRAM�̊Y�����C����dirty�Ȃ�X�V
		TxLineUpdate(vid, vram_line);

		// TX�p���b�g�i���C���t���O�t���j�X�V
		// BG�������������ׂ����������ǁA�������̓v���C�I���e�B������̂ł��܂���ʂȂ�����
		if ( vid->txpal_dirty ) {
			const UINT16* org = vid->pal + 0x100;  // TX/SP pal �� pal[0x100] �`
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
					// ���C���t���O�͏�̃p���b�g�ɑg�ݍ��ݍς݂Ȃ̂ł��̂܂܏o�͂���
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
//   BG/SPR��
// --------------------------------------------------------------------------
// �o�b�N�h���b�v�J���[���������ΏۂȂ̂ŁA�����F�`��̍ۂɂ� VID_PLANE_SP �𗧂ĂĂ����K�v����
// �܂��A�����F�̑Ώۂ� TX0 �̏ꍇ�͎��g�̐F�ƍ����ւ���
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
	// 256�h�b�g���[�hBG
	// BG���X�v���C�g���l�A�p���b�g�Q�ƌ�̐F�݂̂Ŕ����F���肷��i�h���L���� ���钼��j
	const UINT16* palbase = vid->pal + 0x100;
	const UINT32 bgarea = ( ( vid->bgram[0x808/2] >> (plane*3+1) ) & 1 ) << 12;  // 1�u���b�N 0x1000 WORDS
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
		// OPAQ�̏ꍇ�AIndex#0�̃s�N�Z�����i�p���b�g�܂߁j�`���i�X�[�p�[�n���O�I���j
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
	// 512�h�b�g���[�hBG
	// �t�@���^�W�[�]�[���̃^�C�g���Ȃ�
	const UINT16* palbase = vid->pal + 0x100;
	const UINT32 bgarea = ( ( vid->bgram[0x808/2] >> 1 ) & 1 ) << 12;  // 1�u���b�N 0x1000 WORDS
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
		// OPAQ�̏ꍇ�AIndex#0�̃s�N�Z�����i�p���b�g�܂߁j�`���i�X�[�p�[�n���O�I���j
		for ( ; dx<w; dx+=16, sx=(sx+1)&0x3F) {
			const UINT32 tile = src[sx];
			const UINT16* pchr = chrram + ((tile&0x00FF)<<6);  // 0x40 WORDS / char
			const UINT16* pal = palbase + ( (tile&0x0F00) >> 4 );
			UINT32 data;
			pchr += ( tile & 0x8000 ) ? ((15<<1)-src_yofs) : src_yofs;  // V flip
			/*
				16x16 chr �� 8x8 ���ȉ��̂悤�ɔz�u�����̂Ɠ���
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
	BG����PRI���Ⴂ�ꍇ�A�F�R�[�h�͘M�炸��PRI�̂�VID_SP_PRI_MAX�ɂ��邱�ƂŁA�ȍ~��SP���}�X�N����
	�i����ɂ��A����SP���珇�Ƀo�b�t�@��ɏ�`�����邱�ƂŃ��C���摜�𐶐����Ă���i�Ǝv����j
	�n�[�h�E�F�A������Č�����j
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
	const SINT32 screen_w = vid->h_sz;  // �N���C�W�[�N���C�}�[2�͐���256�h�b�g���[�h�ŉ�272�\���i256�Ń��~�b�g����ƕ\�����؂��j
	const UINT16* p = vid->bgram + (0x00000/2);
	const UINT16* pe = p + (128*4);
	const UINT16* chrram = vid->bgram + (0x08000/2);

	// �X�v���C�g�� 0 �Ԃ���Ԏ�O
	while ( p<pe ) {
		const UINT32 prw = p[3] & 0x0003;
		if ( prw ) {  // 0 �ȊO�Ȃ�\��
			const SINT32 dx = (SINT32)(p[0]&0x3FF) - 16 + vid->sp_ofs_x;
			const SINT32 dy = (SINT32)(p[1]&0x3FF) - 16 + vid->sp_ofs_y;
			const UINT32 srcy = (UINT32)(line-dy);
			if ( srcy<16 && dx>=-15 && dx<screen_w ) {  // �\���͈͓�
				const UINT32 src_yofs = srcy << 1;
				const UINT32 tile = p[2];
				const UINT16* pchr = chrram + ((tile&0x00FF)<<6);  // 0x40 WORDS / char (0x10 WORDS x 4)
				const UINT16* pal = palbase + ( (tile&0x0F00) >> 4 );
				const UINT32 pri = prw << (VID_SP_PRI_SFT+1);  // 1/2/3 �� 0x00200000/0x00400000/0x00600000
				const UINT32 flags = VID_PLANE_SP | VID_SP_PRI_MAX;
				UINT32* dst = LINEBUF_TX + LINEBUF_OFS + dx;
				UINT32 data;
				pchr += ( tile & 0x8000 ) ? ((15<<1)-src_yofs) : src_yofs;  // V flip
				/*
					16x16 chr �̃f�[�^�\����512�h�b�g���[�hBG�Ɠ���
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

	// BG�`��
	if ( draw ) {  // SP=ON & D/C=D�iBG/SP�`��j
		const UINT32 bgsp_reso = vid->bgram[0x810/2];  // bg_reso & 0x01 �� 512 �h�b�g���[�h
		const UINT32 crtc_reso = CRTC(20);

		// �����𑜓x��CRTC��BG/SP�ňقȂ�ꍇ�̏���
		if ( ( crtc_reso ^ bgsp_reso ) & 0x04 ) {
			if ( crtc_reso & 0x04 ) {
				// ����512���C�����[�h�A����BG/SP������256���C���ݒ�i�T�C�o���I�� �^�C�g���j
				line >>= 1;  // ����2�{�`��i�����C����2��`���j
			} else {
				// ����256���C�����[�h�A����BG/SP������512���C���ݒ�i�g�p�^�C�g������̂��s���j
				line <<= 1;  // �Ԉ����ċ������C���̂ݕ\���i�C���^���[�X�\���ɂ��ׂ��H�j
			}
		}

		if ( bgsp_reso & 0x01 ) {
			// ���� 512 dot
			// 512�h�b�g�̏ꍇ1�ʂ������݂��Ȃ��iInside X68000 p.173�j �}�W�J���V���b�g�ŃS�~���o�Ă����ւ̑Ώ�
			if ( bg_ctrl & 0x0001 ) BgDrawLine512(vid, line);     // BG0
		} else {
			// ���� 256 dot
			if ( bg_ctrl & 0x0008 ) BgDrawLine256(vid, 1, line);  // BG1�i�K�����j
			if ( bg_ctrl & 0x0001 ) BgDrawLine256(vid, 0, line);  // BG0
		}
	}

	// ���̎��_�� TX_OPAQ ���c���Ă��遁BG/SP�ʎ��̂̕\��OFF(Vctrl2)�A�������� D/C(BgCtrl)=CPU�iBG/SP�`���~�j�A��������BG���ʂƂ��\��OFF(BgCtrl)
	// ���̏ꍇ�� pal[0x100] �Ŗ��߂Ă����i�o���c�C �����L���O�\������R�i�~���S�Ɉڂ�ӂ�̋����A���v���XOP�j
	if ( ( TX_OPAQ ) && ( vctrl2 & 0x0060 ) ) {  // TX/SP�̕Е��ȏオON���̂݁i�f�X�u�����K�[OP�j
		const UINT32 c = vid->pal[0x100] | VID_PLANE_TX0;
		UINT32* p = LINEBUF_TX + LINEBUF_OFS;
		UINT32* pe = p + vid->h_sz;  // �N���C�W�[�N���C�}�[2�͐���256�h�b�g���[�h�ŉ�272�\���i256���������Ȃ��ƃS�~���c��j
		while ( p < pe ) *p++ = c;
		TX_OPAQ = FALSE;
	}

	// SP�`��
	if ( draw ) {  // SP=ON & D/C=D�iBG/SP�`��j
		SpriteDrawLine(vid, line);
	}
}


// --------------------------------------------------------------------------
//   �O���t�B�b�N��
// --------------------------------------------------------------------------
// �ʏ�`��p�FEXON�AB/P�AG/G �ɂ����4��ނɕ������
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

// G/G MIX�p�FB/P�r�b�g����Č��̂��߁AB/P=0�ł�B/P=1�ł�2��ނ�GR MIX�֐���p�ӂ���
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


// ��L���܂Ƃ߂Đ؂�ւ��邽�߂̊֐��|�C���^���X�g
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


// GR�ʐ��������
static void GrDrawLine(X68000_VIDEO* vid, UINT32 line)
{
	/*
		�Z �������Ɋւ��钍�ӎ����F

		�������Ɋւ��Ă� Inside X68000 p.209 �̐}�̗���ɉ����čs����B
		G/G�y��G/T�����̔�������ON�ɂȂ��Ă���ꍇ�A�܂�G/G�i�O���t�B�b�N�ԁj�������������s���A
		���̎��̔������t���O�iEX�t���O�j�������z�����܂܁AG/T�i��TX/SP�j�������������s����B
		���̂��߁A�����̔�������ON�ɂȂ��Ă���s�N�Z���ɉ����āA�O���t�B�b�N�ʂ̃x�[�X�y�[�W�̐F
		�͍ŏI�I��1/4�̗ʂ܂Ō�������i1/2������2�x�N���邽�߁j�B

		�Ȃ��AGR��1�ʂ����Ȃ����[�h�ł�G/G�����͉\�B
		���̏ꍇ�Ac ���J���[�C���f�b�N�X�Ƃ��āApal[c&~1] �� pal[c|1] �̍����ƂȂ�B


		�Z Vctrl2 �� EXON=1 ���� B/P �� 0 �ɐݒ肵���ۂ̓��ꋓ���i�d�l�O����j�F

		B/P=0 ���ƃJ���[�R�[�h�̍ŉ��ʃr�b�g�ł͂Ȃ��A�p���b�g�J���[�̍ŉ��ʁiI�r�b�g�j��EX�t���O
		�i�������E����PRI����p�t���O�j�Ƃ��ċ@�\����悤�ɂȂ�B
		���^���I�����WEX�̃^�C�g�����o��Ƃ���Ŏg�p����Ă���̂͊m�F�B

		���������G�Ȃ̂ŁA���؂��Ȃ��炠����x���@�����̕\���ɂȂ�悤�ꓖ����I�ɑΉ����Ă���B
		�������W�b�N���悭�������c�B
		����̎������̂� x68000_gr_draw.inc / x68000_gr_mix.inc �Q�ƁB���ꂢ�ɏ���������l���ށB
	*/
	const UINT32 vctrl0 = vid->vctrl0;
	const UINT32 vctrl2 = vid->vctrl2;

	// EXON / B/P / G/G �ɂ���Ċ֐��Z�b�g�؂�ւ�
	const ST_GR_DRAW_FUNCS* f_draw = &GR_DRAW_FUNCTIONS[vid->gr_draw_func_idx];
	const ST_GR_MIX_FUNCS*  f_mix = &GR_MIX_FUNCTIONS[vid->gr_mix_func_idx];

	// EX����A������ON�AG/G ON�A�x�[�X�y�[�WON�A�𖞂�����G/G�����������s�����iLayer#1��OFF�ł��N����j
	const BOOL mix_gg = ( ( vctrl2 & 0x1A00 ) == 0x1A00 ) ? TRUE : FALSE;

	if ( vctrl0 & 4 ) {
		// 16�F 1024�h�b�g���[�h
		if ( vctrl2 & 0x0010 ) {
			if ( mix_gg  ) {
				// G/G������
				f_mix->GrMixLine4bit1024(vid, line);
			} else {
				// �ʏ�`��
				f_draw->GrDrawLine4bit1024(vid, line);
			}
			GR_OPAQ = FALSE;
		}
	} else {
		// 512�h�b�g���[�h
		switch ( vctrl0 & 3 )
		{
		case 1:  // 256�F x 2��
		case 2:
		default:
			if ( ( mix_gg ) && ( vctrl2 & 0x0003 ) ) {
				// G/G������
				f_mix->GrMixLine8bit(vid, line);
				GR_OPAQ = FALSE;
			} else {
				// �ʏ�`��
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

		case 0:  // 16�F x 4��
			if ( vctrl2 & 0x0008 ) {
				f_draw->GrDrawLine4bit(vid, line, 3, GR_OPAQ);
				GR_OPAQ = FALSE;
			}
			if ( vctrl2 & 0x0004 ) {
				f_draw->GrDrawLine4bit(vid, line, 2, GR_OPAQ);
				GR_OPAQ = FALSE;
			}
			if ( ( mix_gg ) && ( vctrl2 & 0x0001 ) ) {
				// G/G������
				f_mix->GrMixLine4bit(vid, line, GR_OPAQ);
				GR_OPAQ = FALSE;
			} else {
				// �ʏ�`��
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

		case 3:  // 63356�F �� 1��
			if ( vctrl2 & 0x000F ) {
				if ( mix_gg  ) {
					// G/G������
					f_mix->GrMixLine16bit(vid, line);
				} else {
					// �ʏ�`��
					f_draw->GrDrawLine16bit(vid, line);
				}
				GR_OPAQ = FALSE;
			}
			break;
		}
	}
}


// --------------------------------------------------------------------------
//   ���J�֐�
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
	// MIX�֐��� vctrl1/2 �����ł��炩�����܂�̂ŁA���C���`��Ŗ���v�Z������ vctrl �X�V���Ɍv�Z���Ă���
	const UINT32 vctrl1 = vid->vctrl1;
	const UINT32 vctrl2 = vid->vctrl2;
	UINT32 pri_gr = ( vctrl1 >>  8 ) & 3;
	UINT32 pri_tx = ( vctrl1 >> 10 ) & 3;
	UINT32 pri_sp = ( vctrl1 >> 12 ) & 3;

	// GR�̕`��֐��EGR��MIX�֐��ݒ�
	// B/P�ɂ���Ċ֐��Z�b�g�؂�ւ��iB/P=0 �̓��ꏈ���֐��� EXON=1 �̏ꍇ�̂ݎg�p�j
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

	// ����v���C�I���e�B�E�������̗L���A�y�у��C���̃v���C�I���e�B�ɍ��킹�ă��C����MIX�֐������߂�
	/*
		PRI�l�����ɂ��āF

		1. GR��PRI=3�̏ꍇ�AGR�ȊO�̃��C���͏�����
		2. GR��PRI=0�`2�̏ꍇ�A����PRI�l��GR�̃v���C�I���e�B���m��i����PRI��GR���苒�j
		   �Ⴆ��GR��PRI=1�������ꍇ�A���̃��C����PRI=1�w�肳��Ă��Ă�GR��1�Ƃ��Ĉ�����
		3. ���̌�APRI0�`2�̎c���g��TX��SP�����߂邪�A���̍ۍl�������̂�SP��TX��PRI�l��r
		   ���ʁi�l�̏����������A�󂢂Ă�PRI�g�̍��D��x�̕��ɔz�u�����j�B
		   �����l�������ꍇ��TX�D��B�܂��ATX��SP��3������ꍇ�͂��̂܂�3�i�Œ�D��x�j�Ƃ���
		   ���肳���

		�c�Ƃ����A���S���Y���̂͂��i�e�X�g�v���O�����ɂ����@�ł̒������ʂ��j
		�u�W�[�U�XII�v��OP�̃^�C�g���\���̍Č��̂��߂ɂ͏�L�������K�v
	*/
	switch ( pri_gr )
	{
	case 0:  // GR=0�A1��2��TX/SP�ŕ�������
		if ( pri_tx <= pri_sp ) {  // TX�̕����l���������iTX�D��j
			pri_tx = 1; pri_sp = 2;
		} else {
			pri_tx = 2; pri_sp = 1;
		}
		break;
	case 1:  // GR=1�A0��2��TX/SP�ŕ�������
		if ( pri_tx <= pri_sp ) {  // TX�̕����l���������iTX�D��j
			pri_tx = 0; pri_sp = 2;
		} else {
			pri_tx = 2; pri_sp = 0;
		}
		break;
	case 2:  // GR=2�A0��1��TX/SP�ŕ�������
		if ( pri_tx <= pri_sp ) {  // TX�̕����l���������iTX�D��j
			pri_tx = 0; pri_sp = 1;
		} else {
			pri_tx = 1; pri_sp = 0;
		}
		break;
	case 3:  // GR�ȊO������
	default:
		break;
	}

	vid->mix_func_grmask = 0;  // �Ȃ��Ă����C�Ȃ͂������ꉞ

	// ���̒i�K��PRI����l�͉�������Ă���ipri_gr=3���������ē���l�͑��݂��Ȃ��j
	if ( pri_gr == 3 ) {
		// GR�ȊO������
		// ���̐ݒ�͑��̏������D�悳���
		vid->mix_func_offset= 12;
	} else if ( ( pri_tx < pri_gr ) && ( pri_sp < pri_gr ) ) {
		// GR ����ԉ�
		// GR -> TX �̏��Ń~�b�N�X
		vid->mix_func_offset = 8;
	} else if ( ( pri_tx > pri_gr ) && ( pri_sp > pri_gr ) ) {
		// GR ����ԏ�
		// TX -> GR �̏��Ń~�b�N�X�A�}�X�N�Ȃ�
		vid->mix_func_offset = 0;
	} else {
		// ����ȊO
		// TX -> GR �̏��Ń~�b�N�X�A�}�X�N����
		if ( pri_tx < pri_gr ) vid->mix_func_grmask = VID_PLANE_TX;  // TX > GR �Ȃ̂�GR��TX�Ń}�X�N����
		if ( pri_sp < pri_gr ) vid->mix_func_grmask = VID_PLANE_SP;  // SP > GR �Ȃ̂�GR��SP�Ń}�X�N����
		// ��L2�͔r���A�ǂ��炩�݂̂����͂�
		vid->mix_func_offset= 4;
	}

	// idx �́A+0:GR�ŏ�ʁA+4:GR���ԁA+8:GR�ŉ��ʁA+12:GR�ȊOOFF(GR PRI=3)
	switch ( vctrl2 & 0x5800 )
	{
	case 0x0000:  // �ʏ�
	case 0x0800:
		vid->mix_func_idx = MIX_FUNC_IDX_N;
		break;
	case 0x1000:  // ����PRI
		vid->mix_func_idx = MIX_FUNC_IDX_S;
		break;
	case 0x1800:  // ������
		vid->mix_func_idx = MIX_FUNC_IDX_T;
		break;
	default:      // TX0������
		vid->mix_func_idx = MIX_FUNC_IDX_0;
		break;
	}

	// MIX�֐��e�[�u���ݒ�
	// �X�e�[�g�ŕ��A���₷���悤�ɁA���ڃ|�C���^�ł͂Ȃ���U idx �� offset �ɂ��Ă���e�[�u���Q��
	X68K_MIX_FUNC = MIX_FUNC_TABLE[vid->mix_func_idx] + vid->mix_func_offset;
}

// 1���C������
void FASTCALL X68Video_LineUpdate(X68000_VIDEO* vid, UINT32 line)
{
	const UINT32 h = _MIN(vid->v_sz, vid->vscr_h);

	if ( line < h )
	{
		const UINT32 pri_tx = ( vid->vctrl1 >> 10 ) & 3;
		const UINT32 pri_sp = ( vid->vctrl1 >> 12 ) & 3;
		UINT32 srcline = line;  // �Q�ƌ����C���i�^��1024���C�����[�h�H�p�j
		UINT32 idx;             // MIX�֐�idx

		/*
			����̃��C�������A���S���Y���͈ȉ��̒ʂ�B

			1. TX �� BG/SP �͓���̃��C���o�b�t�@�ɓW�J����B���̍� TX �� SP(BG) �̃v���C�I���e�B
			   �ɂ���āA�ǂ�����ɕ`������ς���i�Ⴂ������j�B

			2. SP �����̏ꍇ�A�ŉ��w�� BG �`�b�v�̃p���b�g�Q�ƃJ���[�Ŗ��߂���B���̍ہAIndex
			   #0�i�����F�j�̐F�����̂܂ܕ`�����BBG ��\���̏ꍇ�Apal[0x100] �Ŗ��߂�B
			   ���̏�� SP �� BG �Ƃ̃v���C�I���e�B�A���т� SP �Ԃ̃v���C�I���e�B���l���̂�����
			   �`�悳��A�Ō�ɂ��̏�� TX �� Index#0 �𔲂��F�Ƃ��ĕ`�悳���B

			3. TX �����̏ꍇ�A���C���o�b�t�@�� TX �p���b�g�Q�ƃJ���[�Ŗ��߂���B���̍ہAIndex
			   #0�i�����F�j�̐F�����̂܂ܕ`�����i�Ȍケ�̃s�N�Z���� TX0 �ƌď́j�B
			   ���̏�� BG/SP �� Index#0 �𔲂��F�Ƃ����`����s���BSP �� BG �Ƃ̃v���C�I���e�B�A
			   ���т� SP �Ԃ̃v���C�I���e�B�i��������őO�̂��̂قǗD��j���l���̂����ŕ`��i��
			   ���F�� Index#0�j�B

			   �A���ABG ���������F�ŁA���̑Ώې悪 TX0 �������ꍇ�ATX0 �� BG ���̐F�ipal[0xN0]
			   �̐F�j�ŏ㏑�������i�f�X�u�����K�[OP�̋����j�B
			   �܂�A���ۂɂ� TX ����ł����ł���{������2.�iBG �� ON �ł���Ȃ�΁ABG �̓���
			   �F���o�b�N�h���b�v�ɂȂ�j�ł���A���ߐF�ȊO�� TX-BG �ԃv���C�I���e�B��2.�Ƌt��
			   �Ȃ��Ă����Ԃ�3.�A���Ǝv����B

			4. GR �͏�L�Ƃ͕ʂ̃o�b�t�@�ɁA���̃��C�����珇�ɕ`�悳���B���̍ہA�ŉ��ʃ��C����
			   Index #0 ���܂߂��S�F���p���b�g�Q�ƃJ���[�ŕ`����A��ʃ��C���� Index #0 �𔲂��F
			   �Ƃ��ď�`�������i�A�� VidCtrl2 �� B/P �� 0 �̍ۂ͓��ꓮ�삪����BGrDrawLine() 
			   �`���̃R�����g�Q�Ɓj�B

			5. TX/SP/BG �̃o�b�t�@�ƁAGR �̃o�b�t�@����������B
			   ���̍ہA�����F�Ƃ��ăp���b�g�J���[�ō��i$0000�j���p������B����ȊO�́i�Ⴆ Index
			   ��0�Ԃł����Ă��j�s���߁B
			   �܂��A���łł� TX0 �J���[�͋��������F�Ƃ��Ă������A���ۂɂ� TX0 �ł����Ă���L�̃��[
			   ���͕ς��Ȃ��͗l�i�����2.�ɏ����ǉ��j�B

			6. �v���C�I���e�B�l���������̂�����ꍇ�͈���������B�ڂ����� UpdateMixFunc() �̃R��
			   ���g�Q�ƁB

			7. ����v���C�I���e�B�͕K����ԏ�ɕ`���i�Ώۃ��C���r�b�g�͋@�\���Ȃ��j

			8. ����v���C�I���e�B���A�x�[�X�v���[���� Index#0 �ȊO�̍��́A�����ł͂Ȃ����Ƃ��Ĉ�
			   ����i�\���I�ɋ��炭���������������j

			9. �������� GR ���Ⴂ�v���C�I���e�B�̃��C���ɑ΂��Ă̂ݍs���Ă���i���j�B

			�i���j����ҁ[�̃\�[�X�R�����g�̒��ɁuGrp���Text����ɂ���ꍇ��Text�Ƃ̔��������s��
			      �ƁASP�̃v���C�I���e�B��Text�Ɉ���������H�i�܂�AGrp��艺�ɂ����Ă�SP���\
				  �������H�j�v�Ƃ����\�L�����邪�A��̓I�ȃ^�C�g����V�[���͕s���B
				  ���󂱂̃R�[�h���ł͓��ɂ��̂悤�ȏ����͍s���Ă��Ȃ��B


			�����F�̈����Ɋւ��Ē��ӂ��ׂ������̎Q�l��F

			TX0�ɂ��Ă͂���3���������Ȃ���΂Ȃ�Ȃ��i��L3.�̋����j
			- �f�X�u�����K�[OP�iSP>TX>GR�j 0�Ԃ͐F�����ĂĂ����� TX0=$FFFE  BG���̓p���b�g#1 Index#0�iColor=$0000�j
			- �ɔE���iSP>TX>GR�j TX0�������F�ɂȂ�Ȃ� BG0/1��OFF  TX0=$0001
			- �C�V�^�[�iSP>TX>GR�j 0�Ԓ��F             GR��OFF     TX0=$5000

			���̑��A�ߋ��ɖ�肪�o�邱�Ƃ�������������
			- ���~���O�X �N����DMA���S 0�Ԓ��F�iSP>GR>TX�j �A��BG��OFF  TX0=$FFFE
			- �o���c�C �R�i�~���S 0�ԓ��߂��Ȃ��iTX>SP>GR�j �A��BG��D/C=C�ŏ����Ă�  TX0=$0842
			- �X�[�p�[�n���O�I�� 0�Ԓ��F�A�p���b�g�����f�iTX>GR>SP�j
			- ���v���X�̖� 0�Ԓ��F�iTX>SP>GR�j SP��OFF
			- ���v���X�̖� 8�ԁi���j���߁iTX>SP>GR�j SP��OFF
			- �X�^�[�N���[�U�[ TX��1�ԁi���j���\���iTX>GR>SP�jBG0/1 OFF�ATX/SP/GR ON
			- �����Y�`�� SP/BG��3�ԁH�i���j���\���iTX=SP=GR=0�j
		*/

		// �h���S���o�X�^�[�̃��[�h��ʂŎg���Ă���ꃂ�[�h�i�^��1024���C���H�j
		if ( ( CRTC(20) & 0x18 ) == 0x18 ) {
			// ���@�̋���������� VD=2/VD=3 ���ɂ��̕\�����ۂ�
			srcline <<= 1;
		}

		// GR �p���C���o�b�t�@�ւ̐���
		GR_OPAQ = TRUE;
		GrDrawLine(vid, srcline);

		// TX/BG/SP �p���C���o�b�t�@�ւ̐���
		TX_OPAQ = TRUE;
		if ( pri_tx <= pri_sp ) {  // TX > SP
			// SP �� TX �̏�
			// SP��OFF�ł� pal[0x100] ���߂��s�������̂ŁA������ON�`�F�b�N�͂��Ȃ��i���v���X�j
			SpDrawLine(vid, srcline);
			TxDrawLine(vid, srcline);
		} else {                   // SP > TX
			// TX �� SP �̏�
			TxDrawLine(vid, srcline);
			SpDrawLine(vid, srcline);
		}

		// �\����Ԃɂ����MIX�֐������߂�
		// idx �� bit1=GR�ʂ�ON/OFF�Abit0=TX/SP�ʂ�ON/OFF
		idx = ( (!GR_OPAQ) ? 2 : 0 ) |  ( (!TX_OPAQ) ? 1 : 0 );  // OPAQ�������Ă�Ȃ牽�炩�̕`�悪������

		// MIX���s
		X68K_MIX_FUNC[idx](vid, line);
	}
}

// ��ʂ̐���
void FASTCALL X68Video_Update(X68000_VIDEO* vid, INFO_SCRNBUF* scr)
{
	// ���z�X�N���[������e�N�X�`���ւ̓]��
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

// �X�e�[�g���[�h/�Z�[�u
void X68Video_LoadState(X68000_VIDEO* vid, STATE* state, UINT32 id)
{
	if ( vid && state ) {
		ReadState(state, id, MAKESTATEID('V','I','D','S'), vid, sizeof(X68000_VIDEO));
		ReadState(state, id, MAKESTATEID('V','S','C','R'), VSCR, vid->vscr_w * vid->vscr_h * sizeof(VSCR[0]));
		// MIX�֐��e�[�u�����A
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
