/* -----------------------------------------------------------------------------------
  Screen buffer
                                                      (c) 2004-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

// �o�b�t�@�̃r�b�g����J���[�ϊ��Ǘ������Ă����w
// �����̕s�v�ȕ����S�J�b�g������A�قډ����c��Ȃ��Ȃ����B�����̃N���b�s���O�t���o�b�t�@�B

#include "osconfig.h"
#include "screen_buffer.h"


// --------------------------------------------------------------------------
//   ���J�֐�
// --------------------------------------------------------------------------
// ������
INFO_SCRNBUF* Scrn_Init(UINT32 w, UINT32 h)
{
	INFO_SCRNBUF* scr;

	if ( (!w) || (!h) ) return NULL;
	scr = (INFO_SCRNBUF*)_MALLOC(sizeof(INFO_SCRNBUF), "ScreenBuffer struct");
	do {
		if ( !scr ) break;
		memset(scr, 0, sizeof(INFO_SCRNBUF));
		scr->width  = scr->w = w;
		scr->height = scr->h = h;
		scr->bpp = 4;
		scr->bpl = scr->w*scr->bpp;
		scr->baseptr = (UINT8*)_MALLOC(scr->bpl*scr->h, "ScreenBuffer");
		if ( !scr->baseptr ) break;
		memset(scr->baseptr, 0, scr->bpl*scr->h);
		scr->ptr = scr->baseptr;

		LOG(("Screen Buffer initialized (%dx%d).", w, h));
		return scr;
	} while ( 0 );

	Scrn_Cleanup(scr);
	return NULL;
}


// �j��
void Scrn_Cleanup(INFO_SCRNBUF* scr)
{
	if ( scr ) {
		_MFREE(scr->baseptr);
		_MFREE(scr);
	}
}


BOOL Scrn_SetClipArea(INFO_SCRNBUF* scr, SINT32 x, SINT32 y, SINT32 w, SINT32 h)
{
	if ( (x<0)||(y<0)||(w<=0)||(h<=0)||((x+w)>(SINT32)scr->width)||((y+h)>(SINT32)scr->height) ) {
		LOG(("Scrn_SetClipArea : Invalid setting (%d,%d,%d,%d)", x, y, w, h));
		return FALSE;
	}
	scr->x = x;
	scr->y = y;
	scr->w = w;
	scr->h = h;
	scr->ptr = (UINT8*)scr->baseptr + x*scr->bpp + y*scr->bpl;
	return TRUE;
}
