/* -----------------------------------------------------------------------------------
  Motorola M680x0 Interface for MUSASHI
                                                         (c) 2024 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	MUSASHI との接続 I/F
	MUSASHI はコールバックパラメータの類は持たないので、泥臭く static に配置しておく
*/

#include "osconfig.h"
#include "mem_manager.h"
#include "m68k.h"
#include "x68000_cpu.h"

typedef struct {
	CPUDEV         dev;  // 必須
	MEM16HDL       mem;
	X68CPU_CPUTYPE cputype;
	UINT32         id;
	UINT32         clks;
	M68K_IRQVECTCB irqcb_func;
	void*          irqcb_prm;
	M68K_RESETCB   resetcb_func;
	void*          resetcb_prm;
} ST_MUSASHI_IF;

static ST_MUSASHI_IF sM68k;


// --------------------------------------------------------------------------
//   MUSASHI用メモリI/F
// --------------------------------------------------------------------------
unsigned int  m68k_read_memory_8(unsigned int address)
{
	return Mem16_Read8BE8(sM68k.mem, address);
}

unsigned int  m68k_read_memory_16(unsigned int address)
{
	return Mem16_Read16BE8(sM68k.mem, address);
}

unsigned int  m68k_read_memory_32(unsigned int address)
{
	return Mem16_Read32BE8(sM68k.mem, address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
	Mem16_Write8BE8(sM68k.mem, address, value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
	Mem16_Write16BE8(sM68k.mem, address, value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
	Mem16_Write32BE8(sM68k.mem, address, value);
}

UINT32 m68k_irq_callback(int irq)
{
	ST_MUSASHI_IF* m68k = (ST_MUSASHI_IF*)&sM68k;
	return m68k->irqcb_func(m68k->irqcb_prm, irq);
}


// --------------------------------------------------------------------------
//   内部関数
// --------------------------------------------------------------------------
static UINT32 FASTCALL GetSliceClock(void* prm)
{
	// タイムスライス中の実行済みクロック数取得
	return m68k_cycles_run();
}

static void FASTCALL SliceTerm(void* prm)
{
	// 現在のタイムスライスを強制的に終わらせる
	m68k_end_timeslice();
}

static BOOL X68CPU_Int(void* prm, UINT32 line, UINT32 vect)
{
	m68k_set_virq( line & IRQLINE_MASK, line & IRQLINE_ON );
	return TRUE;
}

static int CALLBACK DummyIrqCb(void* prm, unsigned int irq)
{
	return -1;
}

static void CALLBACK DummyResetCb(void* prm)
{
}

static void musashi_reset_callback(void)
{
	ST_MUSASHI_IF* m68k = (ST_MUSASHI_IF*)&sM68k;
	m68k->resetcb_func(m68k->resetcb_prm);
}


// --------------------------------------------------------------------------
//   公開関数
// --------------------------------------------------------------------------
CPUDEV* X68CPU_Init(TIMERHDL timer, MEM16HDL mem, UINT32 clock, X68CPU_CPUTYPE cputype, UINT32 id)
{
	static const UINT32 CPUTYPE[] = {
		M68K_CPU_TYPE_68000, M68K_CPU_TYPE_68010, M68K_CPU_TYPE_68020, M68K_CPU_TYPE_68030
	};
	ST_MUSASHI_IF* m68k = (ST_MUSASHI_IF*)&sM68k;

	memset(m68k, 0, sizeof(ST_MUSASHI_IF));

	IRQ_Init(timer, (CPUDEV*)m68k, id);
	m68k->dev.irqfunc = &X68CPU_Int;
	m68k->mem = mem;
	m68k->dev.freq = clock;
	m68k->dev.slice_clk = &GetSliceClock;
	m68k->dev.slice_term = &SliceTerm;
	m68k->cputype = cputype;
	m68k->id = id;

	m68k_init();
	m68k_set_cpu_type(CPUTYPE[cputype]);

	X68CPU_SetIrqCallback((CPUDEV*)m68k, NULL, NULL);
	X68CPU_SetResetCallback((CPUDEV*)m68k, NULL, NULL);
	m68k_set_reset_instr_callback(&musashi_reset_callback);

	X68CPU_Reset((CPUDEV*)m68k);
	return (CPUDEV*)m68k;
}

void X68CPU_Cleanup(CPUDEV* handle)
{
}

void X68CPU_Reset(CPUDEV* handle)
{
	m68k_pulse_reset();
}

UINT32 FASTCALL X68CPU_Exec(CPUDEV* handle, UINT32 clk)
{
	UINT32 ret = m68k_execute(clk);
	sM68k.clks += ret;
	return ret;
}

void X68CPU_BusError(CPUDEV* handle, int address, BOOL is_read)
{
	m68k_pulse_bus_error(address);
}

void X68CPU_SetIrqCallback(CPUDEV* handle, M68K_IRQVECTCB cb, void* cbprm)
{
	ST_MUSASHI_IF* m68k = (ST_MUSASHI_IF*)&sM68k;
	m68k->irqcb_func = ( cb ) ? cb : &DummyIrqCb;
	m68k->irqcb_prm = cbprm;
}

void X68CPU_SetResetCallback(CPUDEV* handle, M68K_RESETCB cb, void* cbprm)
{
	ST_MUSASHI_IF* m68k = (ST_MUSASHI_IF*)&sM68k;
	m68k->resetcb_func = ( cb ) ? cb : &DummyResetCb;
	m68k->resetcb_prm = cbprm;
}

void X68CPU_ConsumeClock(CPUDEV* handle, UINT32 clk)
{
	// MUSASHIには該当するI/がないので勝手に追加した
	m68k_consume_cycles(clk);
}

UINT32 X68CPU_GetExecuteClocks(CPUDEV* handle)
{
	UINT32 ret = sM68k.clks;
	sM68k.clks = 0;
	return ret;
}

static UINT8 sContext[1024];
void X68CPU_LoadState(CPUDEV* handle, STATE* state, UINT32 id)
{
	ST_MUSASHI_IF* m68k = (ST_MUSASHI_IF*)&sM68k;
	if ( state ) {
		UINT32 sz = m68k_context_size();
		ReadState(state, id, MAKESTATEID('C','T','X','T'), sContext, sz);
		m68k_set_context_without_ptr((void*)sContext);  // テーブルポインタや関数ポインタを除いた部分を復帰させる
	}
}

void X68CPU_SaveState(CPUDEV* handle, STATE* state, UINT32 id)
{
	ST_MUSASHI_IF* m68k = (ST_MUSASHI_IF*)&sM68k;
	if ( state ) {
		UINT32 sz = m68k_context_size();
		m68k_get_context((void*)sContext);
		WriteState(state, id, MAKESTATEID('C','T','X','T'), sContext, sz);
	}
}
