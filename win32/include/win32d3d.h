/* -----------------------------------------------------------------------------------
  スクリーン管理層（Win32 D3D11）
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef __d3d_draw_h
#define __d3d_draw_h

#include "osconfig.h"
#include "screen_buffer.h"
#include "x68000_driver.h"

typedef struct __D3DDRAW_HDL* D3DDRAWHDL;

typedef enum {
	D3DDRAW_ASPECT_3_2 = 0,
	D3DDRAW_ASPECT_4_3,
	D3DDRAW_ASPECT_FREE,
} D3DDRAW_ASPECT;

typedef enum {
	D3DDRAW_FILTER_POINT = 0,
	D3DDRAW_FILTER_LINEAR,
} D3DDRAW_FILTER;

#ifdef __cplusplus
extern "C" {
#endif

D3DDRAWHDL D3DDraw_Create(HWND hwnd, INFO_SCRNBUF* info);
void D3DDraw_Dispose(D3DDRAWHDL hdi);
void D3DDraw_Draw(D3DDRAWHDL hdi, BOOL wait, const ST_DISPAREA* area, BOOL enable);
void D3DDraw_Resize(D3DDRAWHDL hdi, UINT32 w, UINT32 h);
void D3DDraw_SetAspect(D3DDRAWHDL hdi, D3DDRAW_ASPECT n);
void D3DDraw_SetStatArea(D3DDRAWHDL hdi, UINT32 n);
void D3DDraw_UpdateWindowSize(D3DDRAWHDL hdi, UINT32 w, UINT32 h);
void D3DDraw_SetFilter(D3DDRAWHDL hdi, D3DDRAW_FILTER n);

#ifdef __cplusplus
}
#endif

#endif //__d3d_draw_h
