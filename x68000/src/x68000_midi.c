/* -----------------------------------------------------------------------------------
  "SHARP X68000" MIDI (CZ-6BM1)
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "x68000_midi.h"
#include "x68000_cpu.h"


#define TX_BUF_SIZE    16  // MIDIボード側のTXバッファはこのサイズのはず（YM3802の仕様書から）
#define MIDI_IRQ_LINE  4   // ボード上のスイッチで2か4が選べる

enum {
	MIDI_TIMER_GENERAL = 0,
	MIDI_TIMER_CLICKCNT,
	MIDI_TIMER_BUFFER,
	MIDI_TIMER_MAX
};

enum {
	MIDI_IRQ_REALTIME_MSG      = 0x01,
	MIDI_IRQ_CLICK_COUNTER     = 0x02,
	MIDI_IRQ_PLAYBACK_COUNTER  = 0x04,
	MIDI_IRQ_RECORDING_COUNTER = 0x08,
	MIDI_IRQ_OFFLINE_DETECT    = 0x10,
	MIDI_IRQ_FIFO_RX_READY     = 0x20,
	MIDI_IRQ_FIFO_TX_EMPTY     = 0x40,
	MIDI_IRQ_GENERAL_TIMER     = 0x80,
};


typedef struct {
	CPUDEV*     cpu;
	TIMERHDL    timer;
	TIMER_ID    tid[MIDI_TIMER_MAX];
	MIDIFUNCCB  midicb;
	void*       cbprm;
	BOOL        connected;  // ステート対象外（上位層の設定次第）

	UINT32      reg_high;
	UINT32      counter[2];

	UINT32      irq_mode;
	UINT32      irq_status;
	UINT32      irq_enable;
	UINT32      irq_basevect;
	UINT32      irq_curvect;

	UINT32      interpolator;

	UINT32      tx_ctrl;
	UINT8       tx_fifo[TX_BUF_SIZE];
	UINT32      tx_wpos;
	UINT32      tx_rpos;
} INFO_MIDI;


// --------------------------------------------------------------------------
//   内部関数
// --------------------------------------------------------------------------
static UINT32 MIDI_GetVector(INFO_MIDI* midi)
{
	UINT32 st = midi->irq_status & midi->irq_enable;
	UINT32 ret = 0x10;
	SINT32 i;
	for (i=7; i>=0; i--) {
		if ( st & (1<<i) ) {
			ret = i << 1;
			break;
		}
	}
	return midi->irq_basevect | ret;
}

static void MIDI_CheckIrq(INFO_MIDI* midi)
{
	if ( midi->irq_status & midi->irq_enable ) {
		midi->irq_curvect = MIDI_GetVector(midi);
		IRQ_Request(midi->cpu, IRQLINE_LINE | MIDI_IRQ_LINE, 0);
	} else {
		midi->irq_curvect = midi->irq_basevect | 0x10;
		IRQ_Clear(midi->cpu, MIDI_IRQ_LINE);
	}
}

static TIMER_HANDLER(MIDI_BufferOut)
{
	INFO_MIDI* midi = (INFO_MIDI*)prm;
	// データがあれば（ここ呼ばれた場合必ずあるはずだが）上位層へコールバックで転送
	if ( midi->tx_rpos != midi->tx_wpos ) {
		midi->midicb(midi->cbprm, MIDIFUNC_DATAOUT, midi->tx_fifo[midi->tx_rpos]);
		midi->tx_rpos = ( midi->tx_rpos + 1 ) & (TX_BUF_SIZE-1);
	}
	// データがなくなったらタイマ停止しとく
	if ( midi->tx_rpos == midi->tx_wpos ) {
		Timer_ChangePeriod(midi->timer, midi->tid[MIDI_TIMER_BUFFER], TIMERPERIOD_NEVER);
		// 送信FIFO EMPTY割り込み
		midi->irq_status |= MIDI_IRQ_FIFO_TX_EMPTY;
		MIDI_CheckIrq(midi);
	}
}

static TIMER_HANDLER(MIDI_TimerOver)
{
	INFO_MIDI* midi = (INFO_MIDI*)prm;
	midi->irq_status |= ( opt == MIDI_TIMER_CLICKCNT ) ? MIDI_IRQ_CLICK_COUNTER : MIDI_IRQ_GENERAL_TIMER;
	MIDI_CheckIrq(midi);
}

static void MIDI_UpdateClickCounterTimer(INFO_MIDI* midi)
{
	if ( ( midi->irq_enable & MIDI_IRQ_CLICK_COUNTER ) && !(midi->irq_mode &0x08 ) && ( midi->interpolator ) ) {
		TUNIT period = TIMERPERIOD_HZ(125000) * (TUNIT)midi->counter[1] * (TUNIT)midi->interpolator;
		Timer_ChangePeriod(midi->timer, midi->tid[MIDI_TIMER_CLICKCNT], period);
	} else {
		Timer_ChangePeriod(midi->timer, midi->tid[MIDI_TIMER_CLICKCNT], TIMERPERIOD_NEVER);
	}
}

static void MIDI_UpdateGeneralTimer(INFO_MIDI* midi)
{
	if (  midi->irq_enable & MIDI_IRQ_GENERAL_TIMER ) {
		TUNIT period = TIMERPERIOD_HZ(125000) * (TUNIT)midi->counter[0];
		Timer_ChangePeriod(midi->timer, midi->tid[MIDI_TIMER_GENERAL], period);
	} else {
		Timer_ChangePeriod(midi->timer, midi->tid[MIDI_TIMER_GENERAL], TIMERPERIOD_NEVER);
	}
}

static void CALLBACK MIDI_DummyCb(void* prm, MIDI_FUNCTIONS func, UINT8 data)
{
}


// --------------------------------------------------------------------------
//   公開関数
// --------------------------------------------------------------------------
X68MIDI X68MIDI_Init(CPUDEV* cpu, TIMERHDL t)
{
	INFO_MIDI* midi = NULL;
	do {
		midi = (INFO_MIDI*)_MALLOC(sizeof(INFO_MIDI), "MIDI struct");
		if ( !midi ) break;
		memset(midi, 0, sizeof(INFO_MIDI));
		midi->cpu = cpu;
		midi->timer = t;
		midi->midicb = &MIDI_DummyCb;
		midi->tid[MIDI_TIMER_GENERAL]  = Timer_CreateItem(midi->timer, TIMER_NORMAL, TIMERPERIOD_NEVER, &MIDI_TimerOver, (void*)midi, MIDI_TIMER_GENERAL,  MAKESTATEID('M','D','T','0'));
		midi->tid[MIDI_TIMER_CLICKCNT] = Timer_CreateItem(midi->timer, TIMER_NORMAL, TIMERPERIOD_NEVER, &MIDI_TimerOver, (void*)midi, MIDI_TIMER_CLICKCNT, MAKESTATEID('M','D','T','1'));
		midi->tid[MIDI_TIMER_BUFFER]   = Timer_CreateItem(midi->timer, TIMER_NORMAL, TIMERPERIOD_NEVER, &MIDI_BufferOut, (void*)midi, MIDI_TIMER_BUFFER,   MAKESTATEID('M','D','T','2'));
		X68MIDI_Reset((X68MIDI)midi);
		LOG(("MIDI : initialize OK"));
		return (X68MIDI)midi;
	} while ( 0 );

	X68MIDI_Cleanup((X68MIDI)midi);
	return NULL;
}

void X68MIDI_Cleanup(X68MIDI hdl)
{
	INFO_MIDI* midi = (INFO_MIDI*)hdl;
	if ( midi ) {
		_MFREE(midi);
	}
}

void X68MIDI_Reset(X68MIDI hdl)
{
	INFO_MIDI* midi = (INFO_MIDI*)hdl;
	if ( midi ) {
		midi->reg_high = 0;
		midi->counter[0] = 0;
		midi->counter[1] = 0;
		midi->irq_mode = 0;
		midi->irq_status = 0;
		midi->irq_enable = 0;
		midi->irq_basevect = 0;
		midi->irq_curvect = 0;
		midi->interpolator = 0;
		midi->tx_ctrl = 0;
		midi->tx_wpos = 0;
		midi->tx_rpos = 0;
		Timer_ChangePeriod(midi->timer, midi->tid[MIDI_TIMER_BUFFER ],  TIMERPERIOD_NEVER);
		Timer_ChangePeriod(midi->timer, midi->tid[MIDI_TIMER_CLICKCNT], TIMERPERIOD_NEVER);
		Timer_ChangePeriod(midi->timer, midi->tid[MIDI_TIMER_GENERAL],  TIMERPERIOD_NEVER);
		IRQ_Clear(midi->cpu, MIDI_IRQ_LINE);
		midi->midicb(midi->cbprm, MIDIFUNC_RESET, 0);
	}
}

void X68MIDI_SetCallback(X68MIDI hdl, MIDIFUNCCB func, void* cbprm)
{
	INFO_MIDI* midi = (INFO_MIDI*)hdl;
	if ( midi ) {
		if ( func ) {
			midi->midicb = func;
			midi->connected = TRUE;
		} else {
			midi->midicb = &MIDI_DummyCb;
			midi->connected = FALSE;
		}
		midi->cbprm = cbprm;
	}
}

MEM16W_HANDLER(X68MIDI_Write)
{
	INFO_MIDI* midi = (INFO_MIDI*)prm;

	// 範囲外かOFFの場合はバスエラー（範囲内偶数アドレスは意味を持たないがエラーにもならない）
	if ( ( adr< 0xEAFA00 ) || ( adr >= 0xEAFA10 ) || ( !midi->connected ) ) {
		X68CPU_BusError(midi->cpu, adr, FALSE);
		return;
	}

	if ( !( adr & 1 ) ) return;
	adr &= 0x0F;

	switch ( adr )
	{
	case 0x03:  // R01 [W] システムステータス
		midi->reg_high = data & 0x0F;
		if ( data & 0x80 ) {
			midi->midicb(midi->cbprm, MIDIFUNC_RESET, 0);
		}
		break;

	case 0x07:  // R03 [W] 割り込みクリア
		// 1を書き込んだビットが落ちる
		{
			UINT32 old = midi->irq_status;
			midi->irq_status &= ~data;
			if ( midi->irq_status != old ) MIDI_CheckIrq(midi);
		}
		break;

	case 0x09:  // R04, 14, ... 94
		switch ( midi->reg_high )
		{
		case 0:  // R04 [W] 割り込みベクタオフセット
			midi->irq_basevect = data & 0xE0;
			midi->irq_curvect = MIDI_GetVector(midi);
			break;
		case 8:  // R84 [W] General Timer(L)
			midi->counter[0] = ( midi->counter[0] & 0x3F00 ) | ( data << 0 );
			MIDI_UpdateGeneralTimer(midi);
			break;
		}
		break;

	case 0x0B:  // R05, 15, ... 95
		switch ( midi->reg_high )
		{
		case 0:  // R05 [W] 割り込みモードコントロール
			{
				UINT32 update = midi->irq_mode ^ data;
				midi->irq_mode = data & 0x0F;
				if ( update & 0x08 ) MIDI_UpdateClickCounterTimer(midi);
			}
			break;
		case 5:  // R55 [W] FIFO-Txコントロール
			midi->tx_ctrl = data;
			if ( data & 0x80 ) {
				UINT32 old = midi->irq_status;
				midi->tx_rpos = 0;
				midi->tx_wpos = 0;
				midi->irq_status |= MIDI_IRQ_FIFO_TX_EMPTY;
				if ( midi->irq_status != old ) MIDI_CheckIrq(midi);
			}
			// 必要に応じてタイマの起動・停止
			if ( ( midi->tx_rpos == midi->tx_wpos ) || !( midi->tx_ctrl & 0x01 ) ) {
				Timer_ChangePeriod(midi->timer, midi->tid[MIDI_TIMER_BUFFER], TIMERPERIOD_NEVER);
			} else if ( Timer_GetPeriod(midi->tid[MIDI_TIMER_BUFFER]) == TIMERPERIOD_NEVER ) {
				Timer_ChangePeriod(midi->timer, midi->tid[MIDI_TIMER_BUFFER], TIMERPERIOD_HZ(3125));
			}
			break;
		case 7:  // R75 [W] Interpolatorコントロール
			{
				UINT32 update = midi->interpolator ^ data;
				midi->interpolator = data & 0x0F;
				if ( update & 0x0F ) MIDI_UpdateClickCounterTimer(midi);
			}
			break;
		case 8:  // R85 [W] General Timer(H)
			midi->counter[0] = ( midi->counter[0] & 0x00FF ) | ( ( data & 0x3F ) << 8 );
			MIDI_UpdateGeneralTimer(midi);
			break;
		}
		break;

	case 0x0D:  // R06, 16, ... 96
		switch ( midi->reg_high )
		{
		case 0:  // R06 [W] 割り込みイネーブル
			{
				UINT32 update = midi->irq_enable ^ data;
				midi->irq_enable = data;
				if ( update ) MIDI_CheckIrq(midi);
				if ( update & MIDI_IRQ_CLICK_COUNTER ) MIDI_UpdateClickCounterTimer(midi);
				if ( update & MIDI_IRQ_GENERAL_TIMER ) MIDI_UpdateGeneralTimer(midi);
			}
//LOG(("MIDI : IRQ enable = $%02X", data));
			break;
		case 5:  // R56 [W] Out Data Byte
			{
				UINT32 next_wpos = ( midi->tx_wpos + 1 ) & (TX_BUF_SIZE-1);
				// 現在のバッファが空なら転送タイマ起動
				if ( midi->tx_rpos == midi->tx_wpos ) {
					Timer_ChangePeriod(midi->timer, midi->tid[MIDI_TIMER_BUFFER], TIMERPERIOD_HZ(3125));
					// 送信FIFO EMPTY割り込みクリア
					midi->irq_status &= ~MIDI_IRQ_FIFO_TX_EMPTY;
					MIDI_CheckIrq(midi);
				}
				// バッファに取り込む  FIFOフル時（TxRDYが立ってないとき）は書き込み無視される
				if ( next_wpos != midi->tx_rpos ) {
					midi->tx_fifo[midi->tx_wpos] = data;
					midi->tx_wpos = next_wpos;
				}
			}
			break;
		case 8:  // R86 [W] MIDI-clock Timer(L)
			midi->counter[1] = ( midi->counter[1] & 0x3F00 ) | ( data << 0 );
			MIDI_UpdateClickCounterTimer(midi);
			break;
		}
		break;

	case 0x0F:  // R07, 17, ... 97
		switch ( midi->reg_high )
		{
		case 8:  // R87 [W] MIDI-clock Timer(H)
			midi->counter[1] = ( midi->counter[1] & 0x00FF ) | ( ( data & 0x3F ) << 8 );
			MIDI_UpdateClickCounterTimer(midi);
			break;
		}
		break;

	default:
		break;
	}

}

MEM16R_HANDLER(X68MIDI_Read)
{
	INFO_MIDI* midi = (INFO_MIDI*)prm;
	UINT32 ret = 0x00;

	// 範囲外かOFFの場合はバスエラー（範囲内偶数アドレスは意味を持たないがエラーにもならない）
	if ( ( adr< 0xEAFA00 ) || ( adr >= 0xEAFA10 ) || ( !midi->connected ) ) {
		X68CPU_BusError(midi->cpu, adr, FALSE);
		return ret;
	}

	if ( !( adr & 1 ) ) return 0xFF;
	adr &= 0x0F;

	switch ( adr )
	{
	case 0x01:  // R00 [R] 割り込みベクタ
		ret = midi->irq_curvect;
		break;

	case 0x05:  // R02 [R] 割り込みステータス
		ret = midi->irq_status;
		break;

	case 0x09:			// R04, 14, ... 94
		switch ( midi->reg_high )
		{
		case 5:  // R54 [R] FIFO-Txステータス
			{
				UINT32 next_rpos = ( midi->tx_rpos + 1 ) & (TX_BUF_SIZE-1);
				if ( midi->tx_rpos == midi->tx_wpos ) {
					ret = 0xC0; // Tx empty/ready
				} else if ( next_rpos != midi->tx_wpos ) {
					ret = 0x40;	// Tx ready
				} else {
//					ret = 0x01;	// Tx busy/not ready（これBUSYは正しくない気がする）
				}
			}
			break;
		}
		break;

	default:
		break;
	}

	return ret;
}

int X68MIDI_GetIntVector(X68MIDI hdl)
{
	INFO_MIDI* midi = (INFO_MIDI*)hdl;
	return MIDI_GetVector(midi);
}

void X68MIDI_LoadState(X68MIDI hdl, STATE* state, UINT32 id)
{
	INFO_MIDI* midi = (INFO_MIDI*)hdl;
	if ( midi && state ) {
		// XXX 項目数多いので手抜き
		size_t sz = ((size_t)&(((INFO_MIDI*)0)->reg_high));  // offsetof(INFO_MIDI,reg_high)
		ReadState(state, id, MAKESTATEID('I','N','F','O'), &midi->reg_high, sizeof(INFO_MIDI)-(UINT32)sz);
	}
}

void X68MIDI_SaveState(X68MIDI hdl, STATE* state, UINT32 id)
{
	INFO_MIDI* midi = (INFO_MIDI*)hdl;
	if ( midi && state ) {
		// XXX 項目数多いので手抜き
		size_t sz = ((size_t)&(((INFO_MIDI*)0)->reg_high));  // offsetof(INFO_MIDI,reg_high)
		WriteState(state, id, MAKESTATEID('I','N','F','O'), &midi->reg_high, sizeof(INFO_MIDI)-(UINT32)sz);
	}
}
