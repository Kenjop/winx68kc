/* -----------------------------------------------------------------------------------
  "SHARP X68000" DMA
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_dma_h_
#define _x68000_dma_h_

#include "emu_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __X68DMAHDL* X68DMA;
typedef int (*X68DMA_READYCB)(void*);

X68DMA X68DMA_Init(CPUDEV* cpu, MEM16HDL mem);
void X68DMA_Cleanup(X68DMA hdl);
void X68DMA_Reset(X68DMA hdl);

MEM16W_HANDLER(X68DMA_Write);
MEM16R_HANDLER(X68DMA_Read);

BOOL X68DMA_Exec(X68DMA hdl, UINT32 chnum);
void X68DMA_BusErr(X68DMA hdl, UINT32 adr, BOOL is_read);
int X68DMA_GetIntVector(X68DMA hdl);
void X68DMA_SetReadyCb(X68DMA hdl, UINT32 chnum, X68DMA_READYCB cb, void* cbprm);

void X68DMA_LoadState(X68DMA hdl, STATE* state, UINT32 id);
void X68DMA_SaveState(X68DMA hdl, STATE* state, UINT32 id);

#ifdef __cplusplus
}
#endif

#endif // of _x68000_dma_h_
