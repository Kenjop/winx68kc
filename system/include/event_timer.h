/* -----------------------------------------------------------------------------------
  Event Timer functions (CPU, Timer and IRQ handler)
                                                      (c) 2004-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _event_timer_h_
#define _event_timer_h_

#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TIMER_NORMAL          0x00000000
#define TIMER_ONESHOT         0x00000001

#define TIMERPERIOD_NOW       TUNIT_ZERO
#define TIMERPERIOD_NEVER     TUNIT_NEVER
#define TIMERPERIOD_HZ(a)     ((a>0)?(DBL2TUNIT(1.0/(double)(a))):TUNIT_NEVER)
#define TIMERPERIOD_SEC(a)    DBL2TUNIT((double)(a))
#define TIMERPERIOD_MS(a)     DBL2TUNIT((double)(a)/1000.0)
#define TIMERPERIOD_US(a)     DBL2TUNIT((double)(a)/1000000.0)
#define TIMERPERIOD_NS(a)     DBL2TUNIT((double)(a)/1000000000.0)


#define _CPU_IRQ_NMI    0xFFFFFFFF

#define IRQLINE_DEFAULT 0
#define IRQLINE_MAX     8
#define IRQLINE_ALL     31
#define IRQLINE_NMI     IRQLINE_MAX

#define IRQLINE_NONE    0x00000000
#define IRQLINE_PULSE   0x00000040
#define IRQLINE_LINE    0x00000080

#define IRQLINE_ON      0x00000020
#define IRQLINE_OFF     0x00000000

#define IRQLINE_MASK    0x0000001F
#define IRQTYPE_MASK    0x000000C0
#define IRQVECT_MASK    0xFFFF0000
#define IRQVECT_SHIFT   16


#define TIMER_HANDLER(name)  void FASTCALL name(void* prm, UINT32 opt)
typedef void   (FASTCALL *_TIMERCB)(void* prm, UINT32 opt);
typedef _TIMERCB  TIMERCB;
#define STRMTIMER_HANDLER(name)  void FASTCALL name(void* prm, UINT32 count)
typedef void   (FASTCALL *_STRMTIMERCB)(void* prm, UINT32 count);
typedef _STRMTIMERCB  STRMTIMERCB;
typedef struct __TIMER_HDL* TIMERHDL;
typedef struct __TIMER_ID*  TIMER_ID;

typedef BOOL (*_IRQFUNC)(void* hdev, UINT32 line, UINT32 vect);
typedef _IRQFUNC IRQFUNC;
typedef struct __IRQ_HDL* IRQHDL;

typedef void (FASTCALL *SLICETERM)(void* prm);
typedef UINT32 (FASTCALL *SLICECLK)(void* prm);

typedef struct {
	// 初期化時にのみ設定される変数（ステート保存不要）
	UINT32     freq;
	IRQFUNC    irqfunc;
	IRQHDL     irqhdl;
	SLICETERM  slice_term;
	SLICECLK   slice_clk;
} CPUDEV;

typedef UINT32 (FASTCALL *_CPUEXECCB)(CPUDEV* handle, UINT32 clk);
typedef _CPUEXECCB CPUEXECCB;


TIMERHDL Timer_Init(void);
void     Timer_Cleanup(TIMERHDL t);
TIMER_ID Timer_CreateItem(TIMERHDL t, UINT32 flag, TUNIT period, TIMERCB func, void* prm, UINT32 opt, UINT32 id);
BOOL     Timer_AddCPU(TIMERHDL t, CPUDEV* prm, CPUEXECCB func, UINT32 id);
BOOL     Timer_AddStream(TIMERHDL t, STRMTIMERCB func, void* prm, UINT32 freq);
BOOL     Timer_ChangePeriod(TIMERHDL t, TIMER_ID id, TUNIT period);
BOOL     Timer_ChangeStartAndPeriod(TIMERHDL t, TIMER_ID id, TUNIT start, TUNIT period);
BOOL     Timer_ChangeOptPrm(TIMER_ID id, UINT32 opt, UINT32 opt2);
UINT32   Timer_GetOptPrm2(TIMER_ID id);
TUNIT    Timer_GetPeriod(TIMER_ID id);
void     Timer_CPUSlice(TIMERHDL t);
void     Timer_SetSampleLimit(TIMERHDL t, UINT32 samples);
void     Timer_Exec(TIMERHDL t, TUNIT period);
void     Timer_UpdateStream(TIMERHDL t);
void     Timer_LoadState(TIMERHDL t, STATE* state, UINT32 id);
void     Timer_SaveState(TIMERHDL t, STATE* state, UINT32 id);

BOOL     IRQ_Init(TIMERHDL timer, CPUDEV* cpu, UINT32 id);
void     IRQ_Cleanup(CPUDEV* cpu);
void     IRQ_Reset(CPUDEV* cpu);
void     IRQ_SetIrqDelay(CPUDEV* cpu, UINT32 line, TUNIT delay);
void     IRQ_Request(CPUDEV* cpu, UINT32 line, UINT32 vect);
void     IRQ_Clear(CPUDEV* cpu, UINT32 line);

#ifdef __cplusplus
}
#endif

#endif // of _event_timer_h_
