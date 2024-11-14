/* -----------------------------------------------------------------------------------
  Emu Driver base functions
                                                      (c) 2004-07 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _emu_driver_h_
#define _emu_driver_h_

#include "osconfig.h"
#include "event_timer.h"
#include "screen_buffer.h"
#include "sound_stream.h"
#include "mem_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOUND_SAMPLE_LIMIT  (0.99)    // 1EXEC辺りの、サウンドサンプルのPUSH生成上限（％/100）

MEM16R_HANDLER(GameDrv_Read16ErrorHandler);
MEM16W_HANDLER(GameDrv_Write16ErrorHandler);

#define _MAINTIMER_         (drv->drv.timer)


#define DRIVER_INIT_ERROR(mes) { LOG(("EmuDriver : " mes)); break; }

#define DRIVER_INIT_START(name, strc) \
	strc *drv;                                                        \
	drv = (strc *)_MALLOC(sizeof(strc), "EmuDriver");                 \
	do {                                                              \
		if ( !drv ) break;                                            \
		memset(drv, 0, sizeof(strc));                                 \
		drv->drv.timer = Timer_Init();                                \
		if ( !drv->drv.timer ) {                                      \
			DRIVER_INIT_ERROR("Init Error : Timer");                  \
		}                                                             \
		drv->drv.sndfreq = sndfreq;                                   \
		drv->drv.sound = SndStream_Init(drv->drv.timer, sndfreq);     \
		if ( !drv->drv.sound ) {                                      \
			DRIVER_INIT_ERROR("Init Error : Sound Stream");           \
		}

#define END_DRIVER(a,prm) a##Driver_Cleanup((void*)prm)
#define DRIVER_INIT_END(name) \
		LOG(("EmuDriver : Initialized"));                             \
		return (EMUDRIVER*)drv;                                       \
	} while ( 0 );                                                    \
	END_DRIVER(name,drv);                                             \
	LOG(("EmuDriver : Initialize Failed"));                           \
	return NULL

#define DRIVER_CHECK_STRUCT(strc) \
	strc *drv = (strc *)__drv;                                        \
	if ( !drv ) return

#define DRIVER_CLEAN_END() \
	SndStream_Cleanup(drv->drv.sound);                                \
	Timer_Cleanup(drv->drv.timer);                                    \
	Scrn_Cleanup(drv->drv.scr);                                       \
	_MFREE(drv)

#define DRIVER_EXEC_END() \
	{                                                                                          \
		UINT32 smpl = (UINT32)(TUNIT2DBL(period)*SOUND_SAMPLE_LIMIT*(double)drv->drv.sndfreq); \
		Timer_SetSampleLimit(drv->drv.timer, smpl);                                            \
	}                                                                                          \
	drv->drv.scr->frame = 0;                                                                   \
	Timer_Exec(drv->drv.timer, period);

/*
 * 個別ドライバ スクリーンバッファ設定マクロ
 */
#define DRIVER_SETSCREEN(w,h) \
	drv->drv.scr = Scrn_Init(w, h);                                   \
	if ( !drv->drv.scr ) {                                            \
		DRIVER_INIT_ERROR("Init Error : Screen Buffer");              \
	}

#define DRIVER_SETSCREENCLIP(x,y,w,h) \
	if ( !Scrn_SetClipArea(drv->drv.scr, x, y, w, h) ) {              \
		DRIVER_INIT_ERROR("Init Error : Screen Clip Area");           \
	}


/*
 * 個別ドライバ タイマ設定マクロ
 */
#define DRIVER_ADDTIMER(flag,time,func,prm,opt,id) \
	{                                                                 \
		TIMER_ID __tid = Timer_CreateItem(drv->drv.timer, flag, time, func, prm, opt, id); \
		if ( !Timer_AddItem(drv->drv.timer, __tid) ) {                \
			DRIVER_INIT_ERROR("Init Error : Timer Registration");     \
			break;                                                    \
		}                                                             \
	}

#define DRIVER_ADDCPU(func,prm,id) \
	if ( !Timer_AddCPU(drv->drv.timer, prm, func, id) ) {             \
		DRIVER_INIT_ERROR("Init Error : Add CPU");                    \
		break;                                                        \
	}


/*
 * 個別ドライバ メモリ設定マクロ
 */
#define DRIVER_SET_MEM16_RHANDLER(hdl,bit,strc) \
	if ( !Mem16_SetReadHandler(hdl, bit, strc) ) {                    \
		DRIVER_INIT_ERROR("Init Error : Memory Read Handler");        \
		break;                                                        \
	}

#define DRIVER_SET_MEM16_WHANDLER(hdl,bit,strc) \
	if ( !Mem16_SetWriteHandler(hdl, bit, strc) ) {                   \
		DRIVER_INIT_ERROR("Init Error : Memory Write Handler");       \
		break;                                                        \
	}


/*
 * 個別ドライバ 基本構造体
 */
typedef struct {
	INFO_SCRNBUF* scr;
	TIMERHDL      timer;
	STREAMHDL     sound;
	UINT32        sndfreq;
} EMUDRIVER;


#ifdef __cplusplus
}
#endif

#endif // of _emu_driver_h_
