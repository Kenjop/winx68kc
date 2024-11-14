/* -----------------------------------------------------------------------------------
  "SHARP X68000" RTC (RICOH RP5C15)
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_rtc_h_
#define _x68000_rtc_h_

#include "emu_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __X68RTCHDL* X68RTC;

X68RTC X68RTC_Init(void);
void X68RTC_Cleanup(X68RTC hdl);
void X68RTC_Reset(X68RTC hdl);

MEM16W_HANDLER(X68RTC_Write);
MEM16R_HANDLER(X68RTC_Read);

void X68RTC_LoadState(X68RTC hdl, STATE* state, UINT32 id);
void X68RTC_SaveState(X68RTC hdl, STATE* state, UINT32 id);

#ifdef __cplusplus
}
#endif

#endif // of _x68000_rtc_h_
