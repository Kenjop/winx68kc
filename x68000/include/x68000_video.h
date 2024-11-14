/* -----------------------------------------------------------------------------------
  "SHARP X68000" Video Driver
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_video_h_
#define _x68000_video_h_

#include "emu_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _ENDIAN_LITTLE
#define LE_ADDR_SWAP  1
#else
#define LE_ADDR_SWAP  0
#endif

#define X68_MAX_VSCR_WIDTH   768
#define X68_MAX_VSCR_HEIGHT  512

typedef struct {
	UINT16     tvram[0x80000/sizeof(UINT16)];
	UINT16     gvram[0x80000/sizeof(UINT16)];
	UINT16     bgram[0x10000/sizeof(UINT16)];
	UINT16     pal[0x400/sizeof(UINT16)];

	UINT16     vctrl0;
	UINT16     vctrl1;
	UINT16     vctrl2;
	UINT8      crtc[0x30];

	UINT32     vscr_w;    // virtual screen size
	UINT32     vscr_h;    // (boot option setting)

	UINT32     h_sz;      // current screen size
	UINT32     v_sz;      // (CRTC setting)

	SINT32     sp_ofs_x;
	SINT32     sp_ofs_y;

	UINT32     contrast;

	UINT32     mix_func_idx;
	UINT32     mix_func_offset;
	UINT32     mix_func_grmask;
	UINT32     gr_draw_func_idx;
	UINT32     gr_mix_func_idx;

	UINT32     tx_buf[1024*1024/8];  // TX expand buffer
	UINT32     tx_dirty[1024/32];    // TX line dirty flag

	BOOL       txpal_dirty;
	UINT32     txpal[16];
} X68000_VIDEO;

X68000_VIDEO* X68Video_Init(void);
void X68Video_Cleanup(X68000_VIDEO* vid);
void X68Video_Reset(X68000_VIDEO* vid);
void X68Video_UpdateMixFunc(X68000_VIDEO* vid);
void FASTCALL X68Video_LineUpdate(X68000_VIDEO* vid, UINT32 line);
void FASTCALL X68Video_Update(X68000_VIDEO* vid, INFO_SCRNBUF* scr);
void X68Video_LoadState(X68000_VIDEO* vid, STATE* state, UINT32 id);
void X68Video_SaveState(X68000_VIDEO* vid, STATE* state, UINT32 id);

#ifdef __cplusplus
}
#endif

#endif // of _x68000_video_h_
