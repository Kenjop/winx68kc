/* -----------------------------------------------------------------------------------
  YAMAHA YM2151(OPM) Emulator Interface for fmgen
                                                      (c) 2007-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_opm_h_
#define _x68000_opm_h_

#include "emu_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void  (*X68OPMINTCB)(void* prm, BOOL line);
typedef struct __X68OPM_HDL* X68OPMHDL;

typedef void   (*_X68OPM_CT_W)(void* prm, UINT8 data);
typedef _X68OPM_CT_W  X68OPM_CT_W;

X68OPMHDL X68OPM_Init(TIMERHDL timer, STREAMHDL strm, UINT32 baseclk, float volume);
void X68OPM_Cleanup(X68OPMHDL handle);
void X68OPM_SetVolume(X68OPMHDL handle, float volume);
void X68OPM_SetIntCallback(X68OPMHDL handle, X68OPMINTCB cb, void* cbprm);
void X68OPM_Reset(X68OPMHDL handle);

MEM16W_HANDLER(X68OPM_Write);
MEM16R_HANDLER(X68OPM_Read);

void X68OPM_SetPort(X68OPMHDL handle, X68OPM_CT_W wr, void* prm);

void X68OPM_LoadState(X68OPMHDL hdl, STATE* state, UINT32 id);
void X68OPM_SaveState(X68OPMHDL hdl, STATE* state, UINT32 id);

#ifdef __cplusplus
}
#endif

#endif // of _x68000_opm_h_
