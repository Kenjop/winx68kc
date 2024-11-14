/* -----------------------------------------------------------------------------------
  Save / Load State
                                                      (c) 2004-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _state_h_
#define _state_h_

#include "osconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef BOOL (CALLBACK *STATEITEM_R)(const void* state, UINT32 id, UINT32 sub_id, void* data, UINT32 size);
typedef BOOL (CALLBACK *STATEITEM_W)(const void* state, UINT32 id, UINT32 sub_id, const void* data, UINT32 size);

typedef struct {
	void* prm;
	void* handle;
	STATEITEM_R read_cb;
	STATEITEM_W write_cb;
} STATE;

#define MAKESTATEID(a,b,c,d)	( ((a)<<24) | ((b)<<16) | ((c)<<8) | ((d)<<0) )

BOOL ReadState(const STATE* info, UINT32 id, UINT32 sub_id, void* data, UINT32 size);
BOOL WriteState(const STATE* info, UINT32 id, UINT32 sub_id, const void* data, UINT32 size);

#ifdef __cplusplus
}
#endif

#endif // of _state_h_
