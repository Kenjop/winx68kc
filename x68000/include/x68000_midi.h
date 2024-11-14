/* -----------------------------------------------------------------------------------
  "SHARP X68000" MIDI
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_midi_h_
#define _x68000_midi_h_

#include "emu_driver.h"
#include "x68000_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __X68MIDIHDL* X68MIDI;

X68MIDI X68MIDI_Init(CPUDEV* cpu, TIMERHDL t);
void X68MIDI_Cleanup(X68MIDI hdl);
void X68MIDI_Reset(X68MIDI hdl);
void X68MIDI_SetCallback(X68MIDI hdl, MIDIFUNCCB func, void* cbprm);

MEM16W_HANDLER(X68MIDI_Write);
MEM16R_HANDLER(X68MIDI_Read);

int X68MIDI_GetIntVector(X68MIDI hdl);

void X68MIDI_LoadState(X68MIDI hdl, STATE* state, UINT32 id);
void X68MIDI_SaveState(X68MIDI hdl, STATE* state, UINT32 id);

#ifdef __cplusplus
}
#endif

#endif // of _x68000_midi_h_
