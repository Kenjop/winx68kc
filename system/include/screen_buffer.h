/* -----------------------------------------------------------------------------------
  Screen buffer
                                                      (c) 2004-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _screen_buffer_h_
#define _screen_buffer_h_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	UINT32  width;
	UINT32  height;
	UINT32  x, y, w, h;  // Clipping area
	UINT32  bpp;
	UINT32  bpl;
	UINT8*  baseptr;
	UINT8*  ptr;
	UINT32  frame;
} INFO_SCRNBUF;

INFO_SCRNBUF* Scrn_Init(UINT32 w, UINT32 h);
void          Scrn_Cleanup(INFO_SCRNBUF* scr);
BOOL          Scrn_SetClipArea(INFO_SCRNBUF* scr, SINT32 x, SINT32 y, SINT32 w, SINT32 h);

#ifdef __cplusplus
}
#endif

#endif // of _screen_buffer_h_
