/* -----------------------------------------------------------------------------------
  Event Timer functions (CPU, Timer and IRQ handler)
                                                      (c) 2004-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

// タイマ/イベントシステムから、マルチCPU対応辺りを削除して単純化したもの
// まだ残骸残ってるけど、まあ気が向いたら潰す程度で

#include "osconfig.h"
#include "event_timer.h"

// --------------------------------------------------------------------------
//   内部定義
// --------------------------------------------------------------------------
#define MAX_CPUNUM  8

// CPU実行用情報
typedef struct {
	CPUDEV*     cpu;              // CPU基礎情報構造体ポインタ
	CPUEXECCB   func;             // CPU実行ルーチンの関数ポインタ
	TUNIT       time_dif;         // 基準時間に対する個別CPU時間の誤差
	UINT32      id;               // ステートロード/セーブ用ID
} INFO_CPUT;

// タイマイベント情報
typedef struct _TIMERITEM {
	TIMERCB     func;             // 実行関数ポインタ
	void*       prm;              // 実行関数引渡しパラメータ（第1引数）
	UINT32      opt;              // 実行関数オプションパラメータ（第2引数）
	UINT32      opt2;             // 実行関数オプションパラメータ2
	TUNIT       period;           // 実行間隔
	TUNIT       current;          // 次の実行までの時間
	UINT32      flag;             // タイマ種別
	struct _TIMERITEM* prev;      // リストの前のアイテムへのポインタ
	struct _TIMERITEM* next;      // リストの次のアイテムへのポインタ
	BOOL        in_list;          // リスト内に登録されているかどうかのフラグ
	UINT32      id;               // ステートロード/セーブ用ID
	UINT32      idx;              // ステートロード/セーブ用インデックス
} TIMERITEM;

// タイマ情報構造体
typedef struct {
	// タイマアイテム類
	TIMERITEM*  top;              // タイマイベントリスト先頭
	TIMERITEM*  last;             // タイマイベントリスト末尾
	TUNIT       remain;           // 前回のタイマ処理での余った/足りなかった端数時間
	TUNIT       curtime;          // 現在のフレーム内での時間

	// サウンドストリーム関連
	STRMTIMERCB strmfunc;
	void*       strmprm;
	UINT32      strmfreq;
	TUNIT       strmfix;
	SINT32      strmremain;       // プッシュ生成するサンプル数上限

	// CPU関連
	INFO_CPUT   cpu;              // 登録CPU
	INFO_CPUT*  cpu_current;      // 現在実行中のCPU情報ポインタ（NULLでCPUタイムスライス中以外）
	TUNIT       slicetime;        // 現在のスライスの時間
} INFO_TIMER;


// --------------------------------------------------------------------------
//   内部関数
// --------------------------------------------------------------------------
FORCE_INLINE(void, InterruptCPUSlice, (INFO_TIMER* tm))
{
	// CPUスライス実行中に割り込みが入った際、スライスを中断する処理
	INFO_CPUT* cpu = tm->cpu_current;
	if ( cpu ) {
		cpu->cpu->slice_term(cpu->cpu);
	}
}

FORCE_INLINE(TUNIT, ExecCPU, (INFO_TIMER* tm, TUNIT times))
{
	INFO_CPUT* cpu = &tm->cpu;
	const TUNIT freq = (TUNIT)cpu->cpu->freq;
	SINT32 clk;

	tm->slicetime = times;  // タイマからの実行要求時間
	tm->cpu_current = cpu;

	// 実行クロック数に換算（CPUの進み／遅れ時間を考慮）
	clk = (SINT32)( (tm->slicetime-cpu->time_dif) * freq );
	if ( clk > 0 ) {
		// 指定時間実行
		SINT32 used = cpu->func(cpu->cpu, (UINT32)clk);
		// 実行クロック数からCPU内時間を進める
		cpu->time_dif += (TUNIT)used / freq;
		// 要求時間より短かった場合は、タイムスライスをそこに合わせる
		if ( ( cpu->time_dif < tm->slicetime ) && ( cpu->time_dif >= 0 ) ) {
			tm->slicetime = cpu->time_dif;
		}
	}

	tm->cpu_current = NULL;
	cpu->time_dif -= tm->slicetime;

	// 実行時間を返す
	return tm->slicetime;
}

// 双方向リスト的なもの（STL使え？ それはそう）
FORCE_INLINE(void, PushBackTimerItem, (INFO_TIMER* tm, TIMERITEM* item))
{
	if ( tm->last ) {
		// LASTがある＝1つ以上アイテムがある
		item->prev = tm->last;
		tm->last->next = item;
		item->next = NULL;
		tm->last = item;
	} else {
		// LASTがない場合TOPもない
		item->prev = NULL;
		item->next = NULL;
		tm->top = item;
		tm->last = item;
	}
	item->in_list = TRUE;
}

FORCE_INLINE(void, AddTimerItem, (INFO_TIMER* tm, TIMERITEM* item))
{
	if ( item->in_list ) return;  // 念のため

	if ( item->current == TIMERPERIOD_NEVER ) {
		// 未起動アイテムなら最後に追加
		PushBackTimerItem(tm, item);
	} else {
		// 自分より起動までの時間が遅いアイテムを探す
		TIMERITEM* p = tm->top;
		TIMERITEM* old = NULL;
		while ( p ) {
			if ( item->current < p->current ) {
				// 見つけたので挿入
				if ( old ) {
					// 1つ前がある
					item->next = p;
					p->prev = item;
					item->prev = old;
					old->next = item;
				} else {
					// 前がない＝リスト先頭
					item->next = tm->top;
					tm->top->prev = item;
					item->prev = NULL;
					tm->top = item;
				}
				// リストに入れる
				item->in_list = TRUE;
				// 自分より後ろがない場合はLAST更新
				if ( !item->next ) {
					tm->last = item;
				}
				break;
			}
			old = p;
			p = p->next;
		}
		// 見つからなかったので最後に追加
		if ( !item->in_list ) {
			PushBackTimerItem(tm, item);
		}
	}
}

FORCE_INLINE(void, AddTimerItemByIndex, (INFO_TIMER* tm, TIMERITEM* item))
{
	// インデックス順に登録する（ステート復帰用）
	TIMERITEM* p = tm->top;
	TIMERITEM* old = NULL;

	// 落とした状態から始める
	item->in_list = FALSE;

	while ( p ) {
		if ( item->idx < p->idx ) {
			// 自分よりインデックスの大きい相手を見つけたので挿入
			if ( old ) {
				// 1つ前がある
				item->next = p;
				p->prev = item;
				item->prev = old;
				old->next = item;
			} else {
				// 前がない＝リスト先頭
				item->next = tm->top;
				tm->top->prev = item;
				item->prev = NULL;
				tm->top = item;
			}
			// リストに入れる
			item->in_list = TRUE;
			// 自分より後ろがない場合はLAST更新
			if ( !item->next ) {
				tm->last = item;
			}
			break;
		}
		old = p;
		p = p->next;
	}
	// 見つからなかったので最後に追加
	if ( !item->in_list ) {
		PushBackTimerItem(tm, item);
	}
}

FORCE_INLINE(void, RemoveTimerItem, (INFO_TIMER* tm, TIMERITEM* item))
{
	if ( !item->in_list ) return;  // 念のため

	// TOPかLASTのアイテムなら、TOP/LASTを書き換える
	if ( tm->top  == item ) tm->top  = item->next;
	if ( tm->last == item ) tm->last = item->prev;

	// 前後のアイテムを接続
	if ( item->prev ) {
		item->prev->next = item->next;
	}
	if ( item->next ) {
		item->next->prev = item->prev;
	}

	// リストから外れる
	item->prev = NULL;
	item->next = NULL;
	item->in_list = FALSE;
}


// --------------------------------------------------------------------------
//   公開関数
// --------------------------------------------------------------------------
// 初期化
TIMERHDL Timer_Init(void)
{
	INFO_TIMER* tm;

	tm = (INFO_TIMER*)_MALLOC(sizeof(INFO_TIMER), "Timer struct");
	do {
		if ( !tm ) break;
		memset(tm, 0, sizeof(INFO_TIMER));
		tm->strmfix  = 0.0;

		return (TIMERHDL)tm;
	} while ( 0 );

	Timer_Cleanup((TIMERHDL)tm);
	return NULL;
}


// 破棄
void Timer_Cleanup(TIMERHDL t)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	if ( tm ) {
		// この時点でリストから外れてるアイテムはないはず（＝全部開放できるはず）
		TIMERITEM* item = tm->top;
		while ( item ) {
			TIMERITEM* o = item;
			item = item->next;
			_MFREE(o);
		}
		_MFREE(tm);
	}
}


// CPUをタイマに登録
BOOL Timer_AddCPU(TIMERHDL t, CPUDEV* prm, CPUEXECCB func, UINT32 id)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;

	if ( !tm ) return FALSE;

	if ( !func ) {
		LOG(("Timer_AddCPU : CPU実行関数が指定されていません"));
		return FALSE;
	}
	if ( !prm ) {
		LOG(("Timer_AddCPU : CPUハンドルが指定されていません"));
		return FALSE;
	}
	if ( !prm->freq ) {
		LOG(("Timer_AddCPU : CPUのクロック周波数が設定されていません"));
		return FALSE;
	}
	tm->cpu.cpu = prm;
	tm->cpu.func = func;
	tm->cpu.time_dif = 0.0;
	tm->cpu.id = id;

	return TRUE;
}


// サウンドストリームをタイマに登録
BOOL Timer_AddStream(TIMERHDL t, STRMTIMERCB func, void* prm, UINT32 freq)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;

	if ( !tm ) return FALSE;

	if ( !func ) {
		LOG(("Timer_AddStream : ストリーム実行関数が指定されていません"));
		return FALSE;
	}
	if ( !prm ) {
		LOG(("Timer_AddStream : ストリームハンドルが指定されていません"));
		return FALSE;
	}
	if ( !freq ) {
		LOG(("Timer_AddStream : ストリーム周波数が指定されていません"));
		return FALSE;
	}

	tm->strmfunc = func;
	tm->strmprm  = prm;
	tm->strmfreq = freq;

	return TRUE;
}


// タイマアイテムの作成
TIMER_ID Timer_CreateItem(TIMERHDL t, UINT32 flag, TUNIT period, TIMERCB func, void* prm, UINT32 opt, UINT32 id)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	TIMERITEM* item;

	if ( !tm ) return NULL;
	if ( !func ) {
		LOG(("Timer_CreateItem : 関数指定がありません"));
		return NULL;
	}

	// 新規追加
	item = (TIMERITEM*)_MALLOC(sizeof(TIMERITEM), "Timer item");
	if ( !item ) {
		LOG(("Timer_Create : メモリが確保できません"));
		return NULL;
	}
	memset(item, 0, sizeof(TIMERITEM));

	if ( period == TUNIT_ZERO ) {
		flag |= TIMER_ONESHOT;
	}

	item->func    = func;
	item->prm     = prm;
	item->flag    = flag;
	item->opt     = opt;
	item->id      = id;
	item->period  = ( item->flag & TIMER_ONESHOT ) ? TIMERPERIOD_NEVER : period;
	item->current = period;

	// リストに追加
	AddTimerItem(tm, item);

	return (TIMER_ID)item;
}


// 実行間隔の設定（とアイテム起動）
BOOL Timer_ChangePeriod(TIMERHDL t, TIMER_ID id, TUNIT period)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	TIMERITEM* item = (TIMERITEM*)id;

	if ( !tm ) return FALSE;
	if ( !item ) return FALSE;

	// ピリオド更新して起動
	// ワンショットの場合は次ピリオドをNEVERにしておく
	item->period  = ( item->flag & TIMER_ONESHOT ) ? TIMERPERIOD_NEVER : period;
	item->current = period;
	if ( item->current != TIMERPERIOD_NEVER ) {
		// 現在時間を基準に追加
		item->current += tm->curtime;
		// CPU実行中にタイマ起動された場合の補正
		if ( tm->cpu_current ) {
			SINT32 cnt = (SINT32)tm->cpu_current->cpu->slice_clk(tm->cpu_current ->cpu);
			TUNIT dif = (TUNIT)cnt / (TUNIT)tm->cpu_current->cpu->freq;
			item->current += dif;
		}
	}
	RemoveTimerItem(tm, item);
	AddTimerItem(tm, item);
	
	// 先頭に追加されたなら今のスライスの終了より起動が手前な可能性があるので、CPUスライス中断
	if ( !item->prev ) InterruptCPUSlice(tm);

	return TRUE;
}


// 開始時間、及び実行間隔の設定（とアイテム起動）
BOOL Timer_ChangeStartAndPeriod(TIMERHDL t, TIMER_ID id, TUNIT start, TUNIT period)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	TIMERITEM* item = (TIMERITEM*)id;

	if ( !tm ) return FALSE;
	if ( !item ) return FALSE;

	// ワンショットの場合は次ピリオドをNEVERにしておく（こっちを使うワンショットタイマはないはずだが）
	item->period  = ( item->flag & TIMER_ONESHOT ) ? TIMERPERIOD_NEVER : period;
	item->current = start;
	if ( item->current != TIMERPERIOD_NEVER ) {
		// 現在時間を基準に追加
		item->current += tm->curtime;
		// CPU実行中にタイマ起動された場合の補正
		if ( tm->cpu_current ) {
			SINT32 cnt = (SINT32)tm->cpu_current->cpu->slice_clk(tm->cpu_current ->cpu);
			TUNIT dif = (TUNIT)cnt / (TUNIT)tm->cpu_current->cpu->freq;
			item->current += dif;
		}
	}
	RemoveTimerItem(tm, item);
	AddTimerItem(tm, item);

	// 先頭に追加されたなら今のスライスの終了より起動が手前な可能性があるので、CPUスライス中断
	if ( !item->prev ) InterruptCPUSlice(tm);

	return TRUE;
}


// オプションパラメータの設定
BOOL Timer_ChangeOptPrm(TIMER_ID id, UINT32 opt, UINT32 opt2)
{
	TIMERITEM* item = (TIMERITEM*)id;
	if ( !item ) return FALSE;
	item->opt  = opt;
	item->opt2 = opt2;
	return TRUE;
}


// オプションパラメータ2の取得
UINT32 Timer_GetOptPrm2(TIMER_ID id)
{
	TIMERITEM* item = (TIMERITEM*)id;
	if ( !item ) return 0;
	return item->opt2;
}


// 現在のピリオド取得
TUNIT Timer_GetPeriod(TIMER_ID id)
{
	TIMERITEM* item = (TIMERITEM*)id;
	if ( !item ) return TIMERPERIOD_NEVER;
	return item->period;
}


// CPUスライス処理
void Timer_CPUSlice(TIMERHDL t)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	if ( tm ) {
		InterruptCPUSlice(tm);
	}
}


// 生成サウンドサンプル数上限のリセット
void Timer_SetSampleLimit(TIMERHDL t, UINT32 samples)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	if ( tm ) {
		// 140625 Kenjo, ForceUpdateでオーバー生成された場合は、次スライスのリミット上限を下げる
		if ( tm->strmremain<0 ) {
			tm->strmremain += samples;
		} else {
			tm->strmremain = samples;
		}
	}
}


// タイマ実行
void Timer_Exec(TIMERHDL t, TUNIT period)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	TUNIT clk = period;

	if ( !tm ) return;

	// 前回超過消化した時間（負数）
	clk += tm->remain;

	// このフレームの現在時間
	tm->curtime = TUNIT_ZERO;

	while ( clk > TUNIT_ZERO )
	{
		TUNIT idle = clk;

		// 次の実行時間までが短い順に並んでるので、最短イベントは先頭のアイテム
		if ( tm->top ) {
			TIMERITEM* item = tm->top;
			TUNIT next = item->current - tm->curtime;
			if ( next < idle ) idle = next;
			if ( idle < TUNIT_ZERO ) idle = TUNIT_ZERO;  // 一応巻き戻らないように
		}

		// アイドル時間分、CPUを回す
		if ( idle > TUNIT_ZERO ) {
			idle = ExecCPU(tm, idle);
		}

		// サウンドストリームプッシュ処理
		if ( tm->strmfunc ) {
			TUNIT tick = (((TUNIT)tm->strmfreq) * idle ) + tm->strmfix;
			SINT32 smpl = (SINT32)tick;
			tm->strmfix = tick-(TUNIT)smpl;  // 端数を保持
			if ( smpl > tm->strmremain ) {
				smpl = tm->strmremain;  // プッシュ生成するサンプル数にリミットを掛ける
			}
			if ( smpl > 0 ) {
				tm->strmremain -= smpl;
				tm->strmfunc(tm->strmprm, smpl);
			}
		}

		// CPUが回った分タイマを進める
		tm->curtime += idle;

		// 複数が時間到達したときは纏めて処理する
		while ( tm->top ) {
			// 実行時間0のアイテムを自己起動し続けると無限ループに落ちるので注意（ないはずだが）
			TIMERITEM* item = tm->top;
			// 時間に到達してたら紐づけられた関数呼び出し
			if ( tm->curtime >= item->current ) {
				// 呼び出し内で再度 ChangePeriod() で自身が起動され直す可能性があるので、まずリストから外す
				RemoveTimerItem(tm, item);
				// 次の実行時間まで加算しておく
				if ( item->period != TIMERPERIOD_NEVER ) {
					item->current += item->period;
				} else {
					item->current = TIMERPERIOD_NEVER;
				}
				// 関数呼び出し
				item->func(item->prm, item->opt);
				// 呼び出し内で起動されてなければ追加し直す
				if ( !item->in_list ) {
					AddTimerItem(tm, item);
				}
			} else {
				// 時間到達前のアイテムにぶつかったら終了
				break;
			}
		}

		// 消化した分の時間を引く
		clk -= idle;
	}

	// 全ての起動アイテムを今回のフレーム時間分進行
	{
		TIMERITEM* item = tm->top;
		while ( item ) {
			// NEVERのアイテムが出たら以降のアイテムは進行の必要なし
			if ( item->current == TIMERPERIOD_NEVER ) break;
			item->current -= tm->curtime;
			item = item->next;
		}
	}

	// フレーム時間を0に戻しておく
	tm->curtime = TUNIT_ZERO;

	// 今回超過消化した時間（負数）
	tm->remain = clk;
}


// ストリームの更新
void Timer_UpdateStream(TIMERHDL t)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	INFO_CPUT* cpu = tm->cpu_current;

	if ( !tm ) return;
	if ( ( cpu ) && ( tm->strmfunc ) ) {
		SINT32 cnt = (SINT32)cpu->cpu->slice_clk(cpu->cpu);
		if ( cnt>0 ) {
			TUNIT dif = (TUNIT)cnt / (TUNIT)cpu->cpu->freq;
			TUNIT tick = ((TUNIT)tm->strmfreq) * dif + tm->strmfix;
			SINT32 smpl = (SINT32)tick;
			// 同タイムスライスに複数回呼ばれる可能性があるので、「tick-(double)smpl」（端数処理）ではなく単純に減算（累積させる）
			tm->strmfix -= (TUNIT)smpl;
			if ( smpl>tm->strmremain ) {
				smpl = tm->strmremain;  // プッシュ生成するサンプル数にリミットを掛ける
			}
			if ( smpl>0 ) {
				tm->strmremain -= smpl;
				tm->strmfunc(tm->strmprm, smpl);
			}
		}
	}
}


// ステートロード/セーブ
static void LoadIrqState(CPUDEV* cpu, STATE* state, UINT32 id);
static void SaveIrqState(CPUDEV* cpu, STATE* state, UINT32 id);

void Timer_LoadState(TIMERHDL t, STATE* state, UINT32 id)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	if ( tm && state ) {
		TIMERITEM* p;
		INFO_CPUT* cpu = &tm->cpu;
		UINT32 idx;

		ReadState(state, id, MAKESTATEID('R','E','M','N'), &tm->remain,     sizeof(tm->remain));
		ReadState(state, id, MAKESTATEID('S','T','R','F'), &tm->strmfix,    sizeof(tm->strmfix));
		ReadState(state, id, MAKESTATEID('S','T','R','R'), &tm->strmremain, sizeof(tm->strmremain));

		// CPU情報ステートロード
		ReadState(state, id, MAKESTATEID('@','T','D',('0')), &cpu->time_dif, sizeof(cpu->time_dif));
		LoadIrqState(cpu->cpu, state, cpu->id);

		ReadState(state, id, MAKESTATEID('S','L','T','M'), &tm->slicetime, sizeof(tm->slicetime));

		// 全タイマアイテムをステートロードして復元
		p = tm->top;
		while ( p ) {
			ReadState(state, p->id, MAKESTATEID('$','I','D','X'), &p->idx,      sizeof(p->idx));
			ReadState(state, p->id, MAKESTATEID('$','O','P','1'), &p->opt,      sizeof(p->opt));
			ReadState(state, p->id, MAKESTATEID('$','O','P','2'), &p->opt2,     sizeof(p->opt2));
			ReadState(state, p->id, MAKESTATEID('$','P','R','D'), &p->period,   sizeof(p->period));
			ReadState(state, p->id, MAKESTATEID('$','C','U','R'), &p->current,  sizeof(p->current));
			ReadState(state, p->id, MAKESTATEID('$','F','L','G'), &p->flag,     sizeof(p->flag));
			p = p->next;
		}
		// idx=0から順に並べ直す
		idx = 0;
		while ( 1 ) {
			p = tm->top;
			while ( p ) {
				if ( p->idx == idx ) {
					TIMERITEM* item = p;
					RemoveTimerItem(tm, item);
					AddTimerItemByIndex(tm, item);
					break;
				}
				p = p->next;
			}
			if ( !p ) break;
			idx++;
		}
	}
}
void Timer_SaveState(TIMERHDL t, STATE* state, UINT32 id)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	if ( tm && state ) {
		TIMERITEM* p;
		INFO_CPUT* cpu = &tm->cpu;
		UINT32 idx = 0;

		WriteState(state, id, MAKESTATEID('R','E','M','N'), &tm->remain,     sizeof(tm->remain));
		WriteState(state, id, MAKESTATEID('S','T','R','F'), &tm->strmfix,    sizeof(tm->strmfix));
		WriteState(state, id, MAKESTATEID('S','T','R','R'), &tm->strmremain, sizeof(tm->strmremain));

		// CPU情報ステートセーブ
		WriteState(state, id, MAKESTATEID('@','T','D',('0')), &cpu->time_dif, sizeof(cpu->time_dif));
		SaveIrqState(cpu->cpu, state, cpu->id);

		WriteState(state, id, MAKESTATEID('S','L','T','M'), &tm->slicetime, sizeof(tm->slicetime));

		// リストから取り出しつつステートセーブ
		p = tm->top;
		while ( p ) {
			p->idx = idx++;
			WriteState(state, p->id, MAKESTATEID('$','I','D','X'), &p->idx,      sizeof(p->idx));
			WriteState(state, p->id, MAKESTATEID('$','O','P','1'), &p->opt,      sizeof(p->opt));
			WriteState(state, p->id, MAKESTATEID('$','O','P','2'), &p->opt2,     sizeof(p->opt2));
			WriteState(state, p->id, MAKESTATEID('$','P','R','D'), &p->period,   sizeof(p->period));
			WriteState(state, p->id, MAKESTATEID('$','C','U','R'), &p->current,  sizeof(p->current));
			WriteState(state, p->id, MAKESTATEID('$','F','L','G'), &p->flag,     sizeof(p->flag));
			p = p->next;
		}
	}
}



/* --------------------------------------------------------------------------
   割り込み
-------------------------------------------------------------------------- */

typedef struct {
	TUNIT    delay[IRQLINE_MAX+1];
	TIMERHDL timer;
	TIMER_ID timerid[IRQLINE_MAX+1];
} IRQINFO;


// --------------------------------------------------------------------------
//   コールバック
// --------------------------------------------------------------------------
static TIMER_HANDLER(IRQ_CallbackInt)
{
	CPUDEV* cpu = (CPUDEV*)prm;
	IRQINFO* info = (IRQINFO*)cpu->irqhdl;
	SINT32 line = opt&IRQLINE_MASK;
	if ( opt&IRQLINE_ON ) {
		cpu->irqfunc((void*)cpu, line|IRQLINE_ON, Timer_GetOptPrm2(info->timerid[line]));
		if ( (opt&IRQTYPE_MASK)==IRQLINE_PULSE ) cpu->irqfunc((void*)cpu, line|IRQLINE_OFF, 0);
	} else {
		if ( line>IRQLINE_MAX ) {
			cpu->irqfunc((void*)cpu, IRQLINE_ALL|IRQLINE_OFF, 0);
		} else {
			cpu->irqfunc((void*)cpu, line|IRQLINE_OFF, 0);
		}
	}
}


// --------------------------------------------------------------------------
//   公開関数
// --------------------------------------------------------------------------
BOOL IRQ_Init(TIMERHDL timer, CPUDEV* cpu, UINT32 id)
{
	UINT32 i;
	IRQINFO* info;

	if ( !cpu ) return FALSE;

	id &= 0xFF;
	if ( (id<'0')||(id>'9') ) {
		LOG(("***** CPU-ID error! ID should be MAKESTATEID(\'C\',\'P\',\'U\',\'x\') style (x=unique number)."));
		return FALSE;
	}

	if ( cpu->irqhdl ) _MFREE(cpu->irqhdl);

	info = (IRQINFO*)_MALLOC(sizeof(IRQINFO), "IRQ Info");
	if ( !info ) return FALSE;
	memset(info, 0, sizeof(IRQINFO));
	info->timer = timer;
	cpu->irqhdl = (IRQHDL)info;

	// 各TimerItemは「#IxA」～「#IxP」および「#IEx」のID（x＝CPU固有番号）を持つ
	for (i=0; i<=IRQLINE_MAX; i++) {
		info->timerid[i] = Timer_CreateItem(timer, TIMER_ONESHOT, TIMERPERIOD_NEVER, &IRQ_CallbackInt, (void*)cpu, i, MAKESTATEID('#','I',id,('A'+i)));
		if ( !info->timerid[i] ) return FALSE;
		info->delay[i] = TIMERPERIOD_NOW;
	}

	return TRUE;
}


void IRQ_Cleanup(CPUDEV* cpu)
{
	if ( cpu ) {
		IRQINFO* info = (IRQINFO*)cpu->irqhdl;
		if ( info ) {
			_MFREE(info);
		}
	}
}


void IRQ_Reset(CPUDEV* cpu)
{
	if ( cpu ) {
		IRQINFO* info = (IRQINFO*)cpu->irqhdl;
		if ( info ) {
			UINT32 i;
			for (i=0; i<=IRQLINE_MAX; i++) {
				Timer_ChangePeriod(info->timer, info->timerid[i], TIMERPERIOD_NEVER);
			}
		}
	}
}


void IRQ_SetIrqDelay(CPUDEV* cpu, UINT32 line, TUNIT delay)
{
	if ( cpu ) {
		IRQINFO* info = (IRQINFO*)cpu->irqhdl;
		if ( info ) {
			if ( line <= IRQLINE_MAX ) {
				info->delay[line] = delay;
			}
		}
	}
}


void IRQ_Request(CPUDEV* cpu, UINT32 line, UINT32 vect)
{
	IRQINFO* info;
	if ( !cpu ) return;
	info = (IRQINFO*)cpu->irqhdl;
	if ( info ) {
		UINT32 type = line&IRQTYPE_MASK;
		line &= IRQLINE_MASK;
		if ( line>IRQLINE_MAX ) return;
		Timer_ChangeOptPrm(info->timerid[line], IRQLINE_ON|line|type, vect);
		if ( 0/*!info->delay[line]*/ ) {  // 上げる方はタイマを使う
			TIMERITEM *item = (TIMERITEM*)info->timerid[line];
			item->func(item->prm, item->opt);
		} else {
			Timer_ChangePeriod(info->timer, info->timerid[line], info->delay[line]);
		}
	}
}


void IRQ_Clear(CPUDEV* cpu, UINT32 line)
{
	IRQINFO* info;
	if ( !cpu ) return;
	info = (IRQINFO*)cpu->irqhdl;
	if ( info ) {
		line &= IRQLINE_MASK;
		if ( line>IRQLINE_MAX ) return;
		Timer_ChangeOptPrm(info->timerid[line], IRQLINE_OFF|line, 0);
		if ( 1 ) {  // 落とす方は即
			TIMERITEM *item = (TIMERITEM*)info->timerid[line];
			item->func(item->prm, item->opt);
		} else {
			Timer_ChangePeriod(info->timer, info->timerid[line], info->delay[line]);
		}
	}
}


static void LoadIrqState(CPUDEV* cpu, STATE* state, UINT32 id)
{
	IRQINFO* info;
	if ( !cpu ) return;
	info = (IRQINFO*)cpu->irqhdl;
	if ( info && state ) {
		ReadState(state, id, MAKESTATEID('@','I','D','L'), info->delay, sizeof(info->delay));
	}
}


static void SaveIrqState(CPUDEV* cpu, STATE* state, UINT32 id)
{
	IRQINFO* info;
	if ( !cpu ) return;
	info = (IRQINFO*)cpu->irqhdl;
	if ( info && state ) {
		WriteState(state, id, MAKESTATEID('@','I','D','L'), info->delay, sizeof(info->delay));
	}
}
