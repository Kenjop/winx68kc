/* -----------------------------------------------------------------------------------
  OKI MSM6258 ADPCM Emulator
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_adpcm_h_
#define _x68000_adpcm_h_

#include "emu_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __X68ADPCM_HDL* X68ADPCM;
typedef BOOL (*_X68ADPCM_SAMPLE_REQ_CB)(void* prm);
typedef _X68ADPCM_SAMPLE_REQ_CB X68ADPCM_SAMPLE_REQ_CB;

X68ADPCM X68ADPCM_Init(TIMERHDL timer, STREAMHDL strm, UINT32 baseclk, float volume);
void X68ADPCM_Cleanup(X68ADPCM handle);
void X68ADPCM_Reset(X68ADPCM handle);

MEM16R_HANDLER(X68ADPCM_Read);
MEM16W_HANDLER(X68ADPCM_Write);

BOOL X68ADPCM_IsDataReady(X68ADPCM handle);

void X68ADPCM_SetCallback(X68ADPCM handle, X68ADPCM_SAMPLE_REQ_CB cb, void* cbprm);
void X68ADPCM_SetBaseClock(X68ADPCM handle, UINT32 baseclk);
void X68ADPCM_SetPrescaler(X68ADPCM handle, UINT32 pres);

void X68ADPCM_SetChannelVolume(X68ADPCM handle, float vol_l, float vol_r);
void X68ADPCM_SetMasterVolume(X68ADPCM handle, float volume);

void X68ADPCM_LoadState(X68ADPCM handle, STATE* state, UINT32 id);
void X68ADPCM_SaveState(X68ADPCM handle, STATE* state, UINT32 id);

#ifdef __cplusplus
}
#endif

#endif // of _x68000_adpcm_h_
