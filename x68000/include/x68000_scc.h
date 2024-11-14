/* -----------------------------------------------------------------------------------
  "SHARP X68000" SCC
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_scc_h_
#define _x68000_scc_h_

#include "emu_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __X68SCCHDL* X68SCC;
typedef void (*X68SCC_MOUSEREADCB)(void*,SINT8*,SINT8*,UINT8*);

X68SCC X68SCC_Init(CPUDEV* cpu, TIMERHDL t);
void X68SCC_Cleanup(X68SCC hdl);
void X68SCC_Reset(X68SCC hdl);

MEM16W_HANDLER(X68SCC_Write);
MEM16R_HANDLER(X68SCC_Read);
int X68SCC_GetIntVector(X68SCC hdl);
void X68SCC_SetMouseCallback(X68SCC hdl, X68SCC_MOUSEREADCB cb, void* cbprm);

void X68SCC_LoadState(X68SCC hdl, STATE* state, UINT32 id);
void X68SCC_SaveState(X68SCC hdl, STATE* state, UINT32 id);

#ifdef __cplusplus
}
#endif

#endif // of _x68000_scc_h_
