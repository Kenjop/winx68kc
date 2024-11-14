/* -----------------------------------------------------------------------------------
  Motorola M680x0 Interface for MUSASHI
                                                         (c) 2024 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_cpu_h_
#define _x68000_cpu_h_

#include "emu_driver.h"

typedef enum {
	X68CPU_68000 = 0,
	X68CPU_68010,
	X68CPU_68020,
	X68CPU_68030,
} X68CPU_CPUTYPE;

#ifdef __cplusplus
extern "C" {
#endif

typedef int  (CALLBACK *M68K_IRQVECTCB)(void*, unsigned int /*IRQ*/);
typedef void (CALLBACK *M68K_RESETCB)(void*);

CPUDEV* X68CPU_Init(TIMERHDL timer, MEM16HDL mem, UINT32 clk, X68CPU_CPUTYPE cputype, UINT32 id);
void X68CPU_Cleanup(CPUDEV* handle);
void X68CPU_Reset(CPUDEV* handle);
UINT32 FASTCALL X68CPU_Exec(CPUDEV* handle, UINT32 opt);
void X68CPU_LoadState(CPUDEV* handle, STATE* state, UINT32 id);
void X68CPU_SaveState(CPUDEV* handle, STATE* state, UINT32 id);

void X68CPU_BusError(CPUDEV* handle, int address, BOOL is_read);
void X68CPU_SetIrqCallback(CPUDEV* handle, M68K_IRQVECTCB cb, void* cbprm);
void X68CPU_SetResetCallback(CPUDEV* handle, M68K_RESETCB cb, void* cbprm);
void X68CPU_ConsumeClock(CPUDEV* handle, UINT32 clk);
UINT32 X68CPU_GetExecuteClocks(CPUDEV* handle);

#ifdef __cplusplus
}
#endif

#endif // of _x68000_cpu_h_
