/* -----------------------------------------------------------------------------------
  Save / Load State
                                                      (c) 2004-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

// 旧コードとの互換性を持たせるための単なるブリッジと化した

#include "state.h"

BOOL ReadState(const STATE* state, UINT32 id, UINT32 sub_id, void* data, UINT32 size)
{
	return state->read_cb((const void*)state, id, sub_id, data, size);
}

BOOL WriteState(const STATE* state, UINT32 id, UINT32 sub_id, const void* data, UINT32 size)
{
	return state->write_cb((const void*)state, id, sub_id, data, size);
}
