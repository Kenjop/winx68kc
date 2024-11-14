/* -----------------------------------------------------------------------------------
  Sound stream manager
                                                      (c) 2004-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _sound_stream_h_
#define _sound_stream_h_

#include "event_timer.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STREAM_HANDLER(name)  void FASTCALL name(void* prm, SINT32* buf, UINT32 len)

typedef struct __STREAM_HDL* STREAMHDL;
typedef void  (FASTCALL *_STREAMCHCB)(void* prm, SINT32* buf, UINT32 len);
typedef _STREAMCHCB  STREAMCHCB;

STREAMHDL SndStream_Init(TIMERHDL timer, UINT32 freq);
void      SndStream_Cleanup(STREAMHDL hstrm);
void      SndStream_Clear(STREAMHDL hstrm);
BOOL      SndStream_AddChannel(STREAMHDL hstrm, STREAMCHCB cb, void* prm);
BOOL      SndStream_RemoveChannel(STREAMHDL hstrm, void* prm);
UINT32    SndStream_GetFreq(STREAMHDL hstrm);

void      SndStream_SetVolume(STREAMHDL hstrm, UINT32 vol);
UINT32    CALLBACK SndStream_GetPCM(STREAMHDL hstrm, SINT16* buf, UINT32 len);

void      SndStream_LoadState(STREAMHDL hstrm, STATE* state, UINT32 id);
void      SndStream_SaveState(STREAMHDL hstrm, STATE* state, UINT32 id);


// ƒtƒBƒ‹ƒ^
typedef struct __SND_FILTER_HDL* SNDFILTER;

SNDFILTER SndFilter_Create(STREAMHDL _strm);
void SndFilter_Destroy(SNDFILTER filter);

void SndFilter_SetPrmLowPass(SNDFILTER _f, float cutoff, float q);
void SndFilter_SetPrmHighPass(SNDFILTER _f, float cutoff, float q);
void SndFilter_SetPrmBandPass(SNDFILTER _f, float cutoff, float q);
void SndFilter_SetPrmLowShelf(SNDFILTER _f, float cutoff, float q, float gain);
void SndFilter_SetPrmHighShelf(SNDFILTER _f, float cutoff, float q, float gain);

void SndFilter_LoadState(SNDFILTER _f, STATE* state, UINT32 id);
void SndFilter_SaveState(SNDFILTER _f, STATE* state, UINT32 id);

void SndStream_AddFilter(STREAMHDL hstrm, SNDFILTER filter, void* dev);
void SndStream_RemoveFilter(STREAMHDL hstrm, SNDFILTER filter, void* dev);

#ifdef __cplusplus
}
#endif

#endif // of _sound_stream_h_
