/* -----------------------------------------------------------------------------------
  "SHARP X68000" MFP
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_mfp_h_
#define _x68000_mfp_h_

#include "emu_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	GPIP_BIT_ALARM = 0,
	GPIP_BIT_EXPON,
	GPIP_BIT_POWSW,
	GPIP_BIT_FMIRQ,
	GPIP_BIT_VDISP,
	GPIP_BIT_DUMMY,
	GPIP_BIT_CIRQ,
	GPIP_BIT_HSYNC
} MFP_GPIP_BIT;

typedef struct __X68MFPHDL* X68MFP;

X68MFP X68MFP_Init(CPUDEV* cpu, TIMERHDL t);
void X68MFP_Cleanup(X68MFP hdl);
void X68MFP_Reset(X68MFP hdl);

MEM16W_HANDLER(X68MFP_Write);
MEM16R_HANDLER(X68MFP_Read);

void X68MFP_TimerExec(X68MFP hdl, float clk);
void X68MFP_SetGPIP(X68MFP hdl, MFP_GPIP_BIT bit, UINT32 n);
int X68MFP_GetIntVector(X68MFP hdl);
BOOL X68MFP_SetKeyData(X68MFP hdl, UINT8 key);

void X68MFP_LoadState(X68MFP hdl, STATE* state, UINT32 id);
void X68MFP_SaveState(X68MFP hdl, STATE* state, UINT32 id);

#ifdef __cplusplus
}
#endif

#endif // of _x68000_mfp_h_
