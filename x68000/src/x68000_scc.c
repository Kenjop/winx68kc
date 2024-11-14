/* -----------------------------------------------------------------------------------
  "SHARP X68000" SCC
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	マウス読み込みに必要な機能だけ実装
*/

#include "x68000_scc.h"

typedef struct {
	UINT8      reg[16];
	UINT32     regidx;
	UINT8      buf[4];
	UINT32     bufcnt;
} ST_SCC_CH;

typedef struct {
	CPUDEV*    cpu;
	TIMERHDL   t;
	TIMER_ID   tm_irq;
	 X68SCC_MOUSEREADCB mouse_cb;
	 void*     mouse_cbprm;
	ST_SCC_CH  ch[2];
	BOOL       in_irq;
} INFO_SCC;


// --------------------------------------------------------------------------
//   内部関数
// --------------------------------------------------------------------------
static void SCC_DummyMouseCb(void* prm, SINT8* px, SINT8* py, UINT8* pstat)
{
	*px = 0;
	*py = 0;
	*pstat = 0;
}

static TIMER_HANDLER(SCC_CheckInt)
{
	INFO_SCC* scc = (INFO_SCC*)prm;

	if ( !scc->in_irq )  // 割り込みクリアが来るまで次の割り込みは上げないようにしておく
	{
		ST_SCC_CH* ch = &scc->ch[1];
		BOOL irq = FALSE;

		if ( ( ch->bufcnt != 0 ) && ( ch->reg[9] & 0x08 ) ) {
			switch ( (ch->reg[1]>>3) & 3 )
			{
			case 1:  // 最初のキャラクタで割り込み
				if ( ch->bufcnt == 3 ) irq = TRUE;
				break;
			case 2:  // 全てのキャラクタで割り込み
				if ( ch->bufcnt != 0 ) irq = TRUE;
				break;
			default:
				break;
			}
		}

		if ( irq ) {
			scc->in_irq = TRUE;
			scc->ch[0].reg[3] |= 0x04;   // CH-B受信割り込みフラグセット（CH-AのRR3）
			IRQ_Request(scc->cpu, IRQLINE_LINE|5, 0);
		}
	}
}

static void SCC_WriteReg(INFO_SCC* scc, UINT32 chidx, UINT32 data)
{
	ST_SCC_CH* ch = &scc->ch[chidx];
	UINT32 regidx = ch->regidx;
	UINT32 old = ch->reg[regidx];

	ch->reg[regidx] = data;
	ch->regidx = 0;

	switch ( regidx )
	{
	case 0:  // WR0 全体制御
		ch->regidx = data & 7;
		// コマンド処理
		switch ( (data>>3) & 7 )
		{
		case 1:  // 001 上位レジスタ選択
			ch->regidx += 8;
			break;
		case 7:  // 111 最上位IUSリセット
			scc->in_irq = FALSE;
			scc->ch[0].reg[3] &= ~0x04;   // CH-B受信割り込みフラグクリア（CH-AのRR3）
			break;
		}
		break;

	case 1:  // WR1  割り込み設定
		break;

	case 2:  // WR2  ベクタ設定
		scc->ch[0].reg[2] = (UINT8)data;  // CH-A側は設定値固定（CH-B側は実際に発行されたベクタ）
		break;

	case 3:  // WR3  受信動作パラメータ設定
		break;

	case 5:  // WR5  送信動作パラメータ設定
		if ( ( ( old ^ data ) & data ) & 0x02 ) {  // /RTS H->L (enable)
			if ( ch->reg[3] & 0x01 ) {  // Rx Enable
				// マウスデータをバッファに取り込む
				if ( ch->bufcnt == 0 ) {  // けろぴーではバッファが空の時だけ取り込んでたので踏襲
					scc->mouse_cb(scc->mouse_cbprm, (SINT8*)&ch->buf[1], (SINT8*)&ch->buf[0], &ch->buf[2]);
					ch->bufcnt = 3;
				}
			}
		}
		break;

	case 9:  // WR9  割り込み制御・SCCリセット
		break;

	default:
		break;
	}
//if ( (data>>3) & 7 ) LOG(("      SCC : CH#%d Write Reg[%d]=$%02X", chidx, regidx, data));
}

static UINT32 SCC_ReadReg(INFO_SCC* scc, UINT32 chidx)
{
	ST_SCC_CH* ch = &scc->ch[chidx];
	UINT32 regidx = ch->regidx;
	UINT32 ret = 0;//ch->reg[regidx];
	ch->regidx = 0;

	switch ( regidx )
	{
	case 0:  // RR0
		if ( ch->bufcnt ) ret |= 0x01;
		ret |= 0x04;  // 送信バッファは常に空とする
		break;

	default:
		break;
	}

//LOG(("      SCC : CH#%d Read Reg[%d], $%02X", chidx, regidx, ret));
	return ret;
}


// --------------------------------------------------------------------------
//   公開関数
// --------------------------------------------------------------------------
X68SCC X68SCC_Init(CPUDEV* cpu, TIMERHDL t)
{
	INFO_SCC* scc = NULL;
	do {
		scc = (INFO_SCC*)_MALLOC(sizeof(INFO_SCC), "SCC struct");
		if ( !scc ) break;
		memset(scc, 0, sizeof(INFO_SCC));
		scc->cpu = cpu;
		scc->t = t;
		scc->mouse_cb = &SCC_DummyMouseCb;
		// 4800bpsなので、割り込みは最大で600回/secになるはず（スタート/ストップビットも4800に含まれるなら400ちょい）
		scc->tm_irq = Timer_CreateItem(scc->t, TIMER_NORMAL, TIMERPERIOD_HZ(400), &SCC_CheckInt, (void*)scc, 0, MAKESTATEID('S','C','T','M'));
		X68SCC_Reset((X68SCC)scc);
		LOG(("SCC : initialize OK"));
		return (X68SCC)scc;
	} while ( 0 );

	X68SCC_Cleanup((X68SCC)scc);
	return NULL;
}

void X68SCC_Cleanup(X68SCC hdl)
{
	INFO_SCC* scc = (INFO_SCC*)hdl;
	if ( scc ) {
		_MFREE(scc);
	}
}

void X68SCC_Reset(X68SCC hdl)
{
	INFO_SCC* scc = (INFO_SCC*)hdl;
	if ( scc ) {
		memset(scc->ch, 0, sizeof(scc->ch));
		scc->in_irq = FALSE;
		IRQ_Clear(scc->cpu, 5);
	}
}

MEM16W_HANDLER(X68SCC_Write)
{
	INFO_SCC* scc = (INFO_SCC*)prm;

	switch ( adr & 7 )
	{
	case 1:  // CH-B command
		SCC_WriteReg(scc, 1, data);
		break;

	case 3:  // CH-B data
		break;

	case 5:  // CH-A command
		SCC_WriteReg(scc, 0, data);
		break;

	case 7:  // CH-A data
		break;

	default:
		break;
	}
}

MEM16R_HANDLER(X68SCC_Read)
{
	INFO_SCC* scc = (INFO_SCC*)prm;
	UINT32 ret = 0xFF;

	switch ( adr & 7 )
	{
	case 1:  // CH-B command
		ret = SCC_ReadReg(scc, 1);
		break;

	case 3:  // CH-B data
		if ( scc->ch[1].bufcnt>0 ) {
			scc->ch[1].bufcnt--;
			ret = scc->ch[1].buf[scc->ch[1].bufcnt];
		} else {
			ret = 0;
		}
		break;

	case 5:  // CH-A command
		ret = SCC_ReadReg(scc, 0);
		break;

	case 7:  // CH-A data
		ret = 0;
		break;

	default:
		break;
	}

	return ret;
}

int X68SCC_GetIntVector(X68SCC hdl)
{
	INFO_SCC* scc = (INFO_SCC*)hdl;
	ST_SCC_CH* ch = &scc->ch[1];
	int vector = -1;

	if ( scc->ch[0].reg[3] & 0x04 ) {
		// CH-B受信割り込みフラグが立っている
		if ( !( ch->reg[9] & 0x02 ) ) {
			// WR9 NV（No Vector）が立っていない
			vector = scc->ch[0].reg[2];  // CH-A側（設定値）を参照
			if ( ch->reg[9] & 0x01 ) {
				// WR9 VIS=1（割り込み要因でベクタが変化する）
				if ( ch->reg[9] & 0x10 ) {
					// Status High（割り込み要因でbit4-6が変化）
					vector = ( vector & ~(7<<4) ) | ( 0x02 << 4 );
				} else {
					// Status Low （割り込み要因でbit1-3が変化）
					vector = ( vector & ~(7<<1) ) | ( 0x02 << 1 );
				}
			}
			ch->reg[2] = (UINT8)vector;  // CH-BのRR2には実際に発行されたベクタが入る
		}
	}

	/*
		ドラキュラなど、受信割り込みハンドラが何もしない（rte オンリーな）場合、IRQ5
		を落とすタイミングがなくなり無限にIRQが発生し続ける形になってしまう。
		なので、何か美しくないが割り込み受け付けられた時点で落としてしまうことにする。
	*/
	IRQ_Clear(scc->cpu, 5);

	return vector;
}

void X68SCC_SetMouseCallback(X68SCC hdl, X68SCC_MOUSEREADCB cb, void* cbprm)
{
	INFO_SCC* scc = (INFO_SCC*)hdl;
	if ( scc ) {
		scc->mouse_cb = (cb) ? cb : &SCC_DummyMouseCb;
		scc->mouse_cbprm = cbprm;
	}
}

void X68SCC_LoadState(X68SCC hdl, STATE* state, UINT32 id)
{
	INFO_SCC* scc = (INFO_SCC*)hdl;
	if ( scc && state ) {
		ReadState(state, id, MAKESTATEID('S','C','C','C'), scc->ch, sizeof(scc->ch));
		ReadState(state, id, MAKESTATEID('I','I','R','Q'), &scc->in_irq, sizeof(scc->in_irq));
	}
}

void X68SCC_SaveState(X68SCC hdl, STATE* state, UINT32 id)
{
	INFO_SCC* scc = (INFO_SCC*)hdl;
	if ( scc && state ) {
		WriteState(state, id, MAKESTATEID('S','C','C','C'), scc->ch, sizeof(scc->ch));
		WriteState(state, id, MAKESTATEID('I','I','R','Q'), &scc->in_irq, sizeof(scc->in_irq));
	}
}
