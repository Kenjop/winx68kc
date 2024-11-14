/* -----------------------------------------------------------------------------------
  "SHARP X68000" SASI (Shugart Associates System Interface) HDD
                                                      (c) 2000-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_sasi_h_
#define _x68000_sasi_h_

#include "emu_driver.h"
#include "x68000_ioc.h"
#include "x68000_fdd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __X68SASIHDL* X68SASI;

X68SASI X68SASI_Init(X68IOC ioc);
void X68SASI_Cleanup(X68SASI hdl);
void X68SASI_Reset(X68SASI hdl);

MEM16W_HANDLER(X68SASI_Write);
MEM16R_HANDLER(X68SASI_Read);

int X68SASI_IsDataReady(X68SASI hdl);
void X68SASI_SetCallback(X68SASI hdl, SASIFUNCCB func, void* cbprm);
X68FDD_LED_STATE X68SASI_GetLedState(X68SASI hdl);

void X68SASI_LoadState(X68SASI hdl, STATE* state, UINT32 id);
void X68SASI_SaveState(X68SASI hdl, STATE* state, UINT32 id);

#ifdef __cplusplus
}
#endif

#endif // of _x68000_sasi_h_
