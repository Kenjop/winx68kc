// --------------------------------------------------------------------------
//   TX0番カラーとの半透明ありの合成
// --------------------------------------------------------------------------

#define ENABLE_SPPRI 0
#define ENABLE_TRANS 0
#define ENABLE_TRTX0 1

// --------------------------- GR が最上位
#define GR_TOP

#define FUNC_NAME   MIX_0_xx
//      LINEBUF_1ST OFF
//      LINEBUF_2ND OFF
#include "x68000_mix_base.inc"

#define FUNC_NAME   MIX_0_xT_TOP
//      LINEBUF_1ST OFF
#define LINEBUF_2ND LINEBUF_TX
#include "x68000_mix_base.inc"

#define FUNC_NAME   MIX_0_Gx_TOP
#define LINEBUF_1ST LINEBUF_GR
//      LINEBUF_2ND OFF
#include "x68000_mix_base.inc"

#define FUNC_NAME   MIX_0_GT_TOP
#define LINEBUF_1ST LINEBUF_GR
#define LINEBUF_2ND LINEBUF_TX
#include "x68000_mix_base.inc"

#undef GR_TOP

// --------------------------- GR が中間
#define GR_MIDDLE

#define FUNC_NAME   MIX_0_xT_MID
//      LINEBUF_1ST OFF
#define LINEBUF_2ND LINEBUF_TX
#include "x68000_mix_base.inc"

#define FUNC_NAME   MIX_0_Gx_MID
#define LINEBUF_1ST LINEBUF_GR
//      LINEBUF_2ND OFF
#include "x68000_mix_base.inc"

#define FUNC_NAME   MIX_0_GT_MID
#define LINEBUF_1ST LINEBUF_GR
#define LINEBUF_2ND LINEBUF_TX
#include "x68000_mix_base.inc"

#undef GR_MIDDLE

// --------------------------- GR が最下位
#define GR_BOTTOM

#define FUNC_NAME   MIX_0_xG_BTM
//      LINEBUF_1ST OFF
#define LINEBUF_2ND LINEBUF_GR
#include "x68000_mix_base.inc"

#define FUNC_NAME   MIX_0_Tx_BTM
#define LINEBUF_1ST LINEBUF_TX
//      LINEBUF_2ND OFF
#include "x68000_mix_base.inc"

#define FUNC_NAME   MIX_0_TG_BTM
#define LINEBUF_1ST LINEBUF_TX
#define LINEBUF_2ND LINEBUF_GR
#include "x68000_mix_base.inc"

#undef GR_BOTTOM

// --------------------------- テーブル
static void (FASTCALL *MIX_0[16])(X68000_VIDEO*,const UINT32) = {
	MIX_0_xx, MIX_0_xT_TOP, MIX_0_Gx_TOP, MIX_0_GT_TOP,
	MIX_0_xx, MIX_0_xT_MID, MIX_0_Gx_MID, MIX_0_GT_MID,
	MIX_0_xx, MIX_0_Tx_BTM, MIX_0_xG_BTM, MIX_0_TG_BTM,
	MIX_0_xx, MIX_0_xx,     MIX_0_Gx_TOP, MIX_0_Gx_TOP  // GR以外OFF
};

#undef ENABLE_SPPRI
#undef ENABLE_TRANS
#undef ENABLE_TRTX0
