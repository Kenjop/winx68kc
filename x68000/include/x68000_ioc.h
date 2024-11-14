/* -----------------------------------------------------------------------------------
  "SHARP X68000" IOC
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_ioc_h_
#define _x68000_ioc_h_

#include "emu_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	IOCIRQ_PRN = 0,
	IOCIRQ_FDD,
	IOCIRQ_FDC,
	IOCIRQ_HDD,
	IOCIRQ_MAX
} IOCIRQ;

typedef struct __X68IOCHDL* X68IOC;

X68IOC X68IOC_Init(CPUDEV* cpu);
void X68IOC_Cleanup(X68IOC hdl);
void X68IOC_Reset(X68IOC hdl);

MEM16W_HANDLER(X68IOC_Write);
MEM16R_HANDLER(X68IOC_Read);

void X68IOC_SetIrq(X68IOC hdl, IOCIRQ irq, BOOL sw);
int X68IOC_GetIntVector(X68IOC hdl);

void X68IOC_LoadState(X68IOC hdl, STATE* state, UINT32 id);
void X68IOC_SaveState(X68IOC hdl, STATE* state, UINT32 id);

#ifdef __cplusplus
}
#endif

#endif // of _x68000_ioc_h_
