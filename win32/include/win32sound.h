/* -----------------------------------------------------------------------------------
  DirectSound Streaming
                                                      (c) 2004-07 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _dsound_stream_h
#define _dsound_stream_h

typedef struct __DSOUND_HDL* DSHANDLE;

#ifdef __cplusplus
extern "C" {
#endif

DSHANDLE DSound_Create(HWND hWnd, UINT freq, UINT ch);
void DSound_Destroy(DSHANDLE handle);
void DSound_SetCB(DSHANDLE handle, void (CALLBACK *ptr)(void*, short*, unsigned int), void*);

void DSound_Pause(DSHANDLE handle);
void DSound_Restart(DSHANDLE handle);
void DSound_SetVolume(DSHANDLE handle, int n);
void DSound_Lock(DSHANDLE handle);
void DSound_Unlock(DSHANDLE handle);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //_dsound_stream_h
