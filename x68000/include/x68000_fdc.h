/* -----------------------------------------------------------------------------------
  "SHARP X68000" FDC
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_fdc_h_
#define _x68000_fdc_h_

#include "emu_driver.h"
#include "x68000_ioc.h"
#include "x68000_fdd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __X68FDCHDL* X68FDC;

X68FDC X68FDC_Init(X68IOC ioc, X68FDD fdd);
void X68FDC_Cleanup(X68FDC hdl);
void X68FDC_Reset(X68FDC hdl);

MEM16W_HANDLER(X68FDC_Write);
MEM16R_HANDLER(X68FDC_Read);

int X68FDC_IsDataReady(X68FDC hdl);
void X68FDC_SetForceReady(X68FDC hdl, BOOL sw);

void X68FDC_LoadState(X68FDC hdl, STATE* state, UINT32 id);
void X68FDC_SaveState(X68FDC hdl, STATE* state, UINT32 id);

#ifdef __cplusplus
}
#endif

#endif // of _x68000_fdc_h_
