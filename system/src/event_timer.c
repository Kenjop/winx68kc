/* -----------------------------------------------------------------------------------
  Event Timer functions (CPU, Timer and IRQ handler)
                                                      (c) 2004-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

// �^�C�}/�C�x���g�V�X�e������A�}���`CPU�Ή��ӂ���폜���ĒP������������
// �܂��c�[�c���Ă邯�ǁA�܂��C����������ׂ����x��

#include "osconfig.h"
#include "event_timer.h"

// --------------------------------------------------------------------------
//   ������`
// --------------------------------------------------------------------------
#define MAX_CPUNUM  8

// CPU���s�p���
typedef struct {
	CPUDEV*     cpu;              // CPU��b���\���̃|�C���^
	CPUEXECCB   func;             // CPU���s���[�`���̊֐��|�C���^
	TUNIT       time_dif;         // ����Ԃɑ΂����CPU���Ԃ̌덷
	UINT32      id;               // �X�e�[�g���[�h/�Z�[�u�pID
} INFO_CPUT;

// �^�C�}�C�x���g���
typedef struct _TIMERITEM {
	TIMERCB     func;             // ���s�֐��|�C���^
	void*       prm;              // ���s�֐����n���p�����[�^�i��1�����j
	UINT32      opt;              // ���s�֐��I�v�V�����p�����[�^�i��2�����j
	UINT32      opt2;             // ���s�֐��I�v�V�����p�����[�^2
	TUNIT       period;           // ���s�Ԋu
	TUNIT       current;          // ���̎��s�܂ł̎���
	UINT32      flag;             // �^�C�}���
	struct _TIMERITEM* prev;      // ���X�g�̑O�̃A�C�e���ւ̃|�C���^
	struct _TIMERITEM* next;      // ���X�g�̎��̃A�C�e���ւ̃|�C���^
	BOOL        in_list;          // ���X�g���ɓo�^����Ă��邩�ǂ����̃t���O
	UINT32      id;               // �X�e�[�g���[�h/�Z�[�u�pID
	UINT32      idx;              // �X�e�[�g���[�h/�Z�[�u�p�C���f�b�N�X
} TIMERITEM;

// �^�C�}���\����
typedef struct {
	// �^�C�}�A�C�e����
	TIMERITEM*  top;              // �^�C�}�C�x���g���X�g�擪
	TIMERITEM*  last;             // �^�C�}�C�x���g���X�g����
	TUNIT       remain;           // �O��̃^�C�}�����ł̗]����/����Ȃ������[������
	TUNIT       curtime;          // ���݂̃t���[�����ł̎���

	// �T�E���h�X�g���[���֘A
	STRMTIMERCB strmfunc;
	void*       strmprm;
	UINT32      strmfreq;
	TUNIT       strmfix;
	SINT32      strmremain;       // �v�b�V����������T���v�������

	// CPU�֘A
	INFO_CPUT   cpu;              // �o�^CPU
	INFO_CPUT*  cpu_current;      // ���ݎ��s����CPU���|�C���^�iNULL��CPU�^�C���X���C�X���ȊO�j
	TUNIT       slicetime;        // ���݂̃X���C�X�̎���
} INFO_TIMER;


// --------------------------------------------------------------------------
//   �����֐�
// --------------------------------------------------------------------------
FORCE_INLINE(void, InterruptCPUSlice, (INFO_TIMER* tm))
{
	// CPU�X���C�X���s���Ɋ��荞�݂��������ہA�X���C�X�𒆒f���鏈��
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

	tm->slicetime = times;  // �^�C�}����̎��s�v������
	tm->cpu_current = cpu;

	// ���s�N���b�N���Ɋ��Z�iCPU�̐i�݁^�x�ꎞ�Ԃ��l���j
	clk = (SINT32)( (tm->slicetime-cpu->time_dif) * freq );
	if ( clk > 0 ) {
		// �w�莞�Ԏ��s
		SINT32 used = cpu->func(cpu->cpu, (UINT32)clk);
		// ���s�N���b�N������CPU�����Ԃ�i�߂�
		cpu->time_dif += (TUNIT)used / freq;
		// �v�����Ԃ��Z�������ꍇ�́A�^�C���X���C�X�������ɍ��킹��
		if ( ( cpu->time_dif < tm->slicetime ) && ( cpu->time_dif >= 0 ) ) {
			tm->slicetime = cpu->time_dif;
		}
	}

	tm->cpu_current = NULL;
	cpu->time_dif -= tm->slicetime;

	// ���s���Ԃ�Ԃ�
	return tm->slicetime;
}

// �o�������X�g�I�Ȃ��́iSTL�g���H ����͂����j
FORCE_INLINE(void, PushBackTimerItem, (INFO_TIMER* tm, TIMERITEM* item))
{
	if ( tm->last ) {
		// LAST�����遁1�ȏ�A�C�e��������
		item->prev = tm->last;
		tm->last->next = item;
		item->next = NULL;
		tm->last = item;
	} else {
		// LAST���Ȃ��ꍇTOP���Ȃ�
		item->prev = NULL;
		item->next = NULL;
		tm->top = item;
		tm->last = item;
	}
	item->in_list = TRUE;
}

FORCE_INLINE(void, AddTimerItem, (INFO_TIMER* tm, TIMERITEM* item))
{
	if ( item->in_list ) return;  // �O�̂���

	if ( item->current == TIMERPERIOD_NEVER ) {
		// ���N���A�C�e���Ȃ�Ō�ɒǉ�
		PushBackTimerItem(tm, item);
	} else {
		// �������N���܂ł̎��Ԃ��x���A�C�e����T��
		TIMERITEM* p = tm->top;
		TIMERITEM* old = NULL;
		while ( p ) {
			if ( item->current < p->current ) {
				// �������̂ő}��
				if ( old ) {
					// 1�O������
					item->next = p;
					p->prev = item;
					item->prev = old;
					old->next = item;
				} else {
					// �O���Ȃ������X�g�擪
					item->next = tm->top;
					tm->top->prev = item;
					item->prev = NULL;
					tm->top = item;
				}
				// ���X�g�ɓ����
				item->in_list = TRUE;
				// ��������낪�Ȃ��ꍇ��LAST�X�V
				if ( !item->next ) {
					tm->last = item;
				}
				break;
			}
			old = p;
			p = p->next;
		}
		// ������Ȃ������̂ōŌ�ɒǉ�
		if ( !item->in_list ) {
			PushBackTimerItem(tm, item);
		}
	}
}

FORCE_INLINE(void, AddTimerItemByIndex, (INFO_TIMER* tm, TIMERITEM* item))
{
	// �C���f�b�N�X���ɓo�^����i�X�e�[�g���A�p�j
	TIMERITEM* p = tm->top;
	TIMERITEM* old = NULL;

	// ���Ƃ�����Ԃ���n�߂�
	item->in_list = FALSE;

	while ( p ) {
		if ( item->idx < p->idx ) {
			// �������C���f�b�N�X�̑傫��������������̂ő}��
			if ( old ) {
				// 1�O������
				item->next = p;
				p->prev = item;
				item->prev = old;
				old->next = item;
			} else {
				// �O���Ȃ������X�g�擪
				item->next = tm->top;
				tm->top->prev = item;
				item->prev = NULL;
				tm->top = item;
			}
			// ���X�g�ɓ����
			item->in_list = TRUE;
			// ��������낪�Ȃ��ꍇ��LAST�X�V
			if ( !item->next ) {
				tm->last = item;
			}
			break;
		}
		old = p;
		p = p->next;
	}
	// ������Ȃ������̂ōŌ�ɒǉ�
	if ( !item->in_list ) {
		PushBackTimerItem(tm, item);
	}
}

FORCE_INLINE(void, RemoveTimerItem, (INFO_TIMER* tm, TIMERITEM* item))
{
	if ( !item->in_list ) return;  // �O�̂���

	// TOP��LAST�̃A�C�e���Ȃ�ATOP/LAST������������
	if ( tm->top  == item ) tm->top  = item->next;
	if ( tm->last == item ) tm->last = item->prev;

	// �O��̃A�C�e����ڑ�
	if ( item->prev ) {
		item->prev->next = item->next;
	}
	if ( item->next ) {
		item->next->prev = item->prev;
	}

	// ���X�g����O���
	item->prev = NULL;
	item->next = NULL;
	item->in_list = FALSE;
}


// --------------------------------------------------------------------------
//   ���J�֐�
// --------------------------------------------------------------------------
// ������
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


// �j��
void Timer_Cleanup(TIMERHDL t)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	if ( tm ) {
		// ���̎��_�Ń��X�g����O��Ă�A�C�e���͂Ȃ��͂��i���S���J���ł���͂��j
		TIMERITEM* item = tm->top;
		while ( item ) {
			TIMERITEM* o = item;
			item = item->next;
			_MFREE(o);
		}
		_MFREE(tm);
	}
}


// CPU���^�C�}�ɓo�^
BOOL Timer_AddCPU(TIMERHDL t, CPUDEV* prm, CPUEXECCB func, UINT32 id)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;

	if ( !tm ) return FALSE;

	if ( !func ) {
		LOG(("Timer_AddCPU : CPU���s�֐����w�肳��Ă��܂���"));
		return FALSE;
	}
	if ( !prm ) {
		LOG(("Timer_AddCPU : CPU�n���h�����w�肳��Ă��܂���"));
		return FALSE;
	}
	if ( !prm->freq ) {
		LOG(("Timer_AddCPU : CPU�̃N���b�N���g�����ݒ肳��Ă��܂���"));
		return FALSE;
	}
	tm->cpu.cpu = prm;
	tm->cpu.func = func;
	tm->cpu.time_dif = 0.0;
	tm->cpu.id = id;

	return TRUE;
}


// �T�E���h�X�g���[�����^�C�}�ɓo�^
BOOL Timer_AddStream(TIMERHDL t, STRMTIMERCB func, void* prm, UINT32 freq)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;

	if ( !tm ) return FALSE;

	if ( !func ) {
		LOG(("Timer_AddStream : �X�g���[�����s�֐����w�肳��Ă��܂���"));
		return FALSE;
	}
	if ( !prm ) {
		LOG(("Timer_AddStream : �X�g���[���n���h�����w�肳��Ă��܂���"));
		return FALSE;
	}
	if ( !freq ) {
		LOG(("Timer_AddStream : �X�g���[�����g�����w�肳��Ă��܂���"));
		return FALSE;
	}

	tm->strmfunc = func;
	tm->strmprm  = prm;
	tm->strmfreq = freq;

	return TRUE;
}


// �^�C�}�A�C�e���̍쐬
TIMER_ID Timer_CreateItem(TIMERHDL t, UINT32 flag, TUNIT period, TIMERCB func, void* prm, UINT32 opt, UINT32 id)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	TIMERITEM* item;

	if ( !tm ) return NULL;
	if ( !func ) {
		LOG(("Timer_CreateItem : �֐��w�肪����܂���"));
		return NULL;
	}

	// �V�K�ǉ�
	item = (TIMERITEM*)_MALLOC(sizeof(TIMERITEM), "Timer item");
	if ( !item ) {
		LOG(("Timer_Create : ���������m�ۂł��܂���"));
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

	// ���X�g�ɒǉ�
	AddTimerItem(tm, item);

	return (TIMER_ID)item;
}


// ���s�Ԋu�̐ݒ�i�ƃA�C�e���N���j
BOOL Timer_ChangePeriod(TIMERHDL t, TIMER_ID id, TUNIT period)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	TIMERITEM* item = (TIMERITEM*)id;

	if ( !tm ) return FALSE;
	if ( !item ) return FALSE;

	// �s���I�h�X�V���ċN��
	// �����V���b�g�̏ꍇ�͎��s���I�h��NEVER�ɂ��Ă���
	item->period  = ( item->flag & TIMER_ONESHOT ) ? TIMERPERIOD_NEVER : period;
	item->current = period;
	if ( item->current != TIMERPERIOD_NEVER ) {
		// ���ݎ��Ԃ���ɒǉ�
		item->current += tm->curtime;
		// CPU���s���Ƀ^�C�}�N�����ꂽ�ꍇ�̕␳
		if ( tm->cpu_current ) {
			SINT32 cnt = (SINT32)tm->cpu_current->cpu->slice_clk(tm->cpu_current ->cpu);
			TUNIT dif = (TUNIT)cnt / (TUNIT)tm->cpu_current->cpu->freq;
			item->current += dif;
		}
	}
	RemoveTimerItem(tm, item);
	AddTimerItem(tm, item);
	
	// �擪�ɒǉ����ꂽ�Ȃ獡�̃X���C�X�̏I�����N������O�ȉ\��������̂ŁACPU�X���C�X���f
	if ( !item->prev ) InterruptCPUSlice(tm);

	return TRUE;
}


// �J�n���ԁA�y�ю��s�Ԋu�̐ݒ�i�ƃA�C�e���N���j
BOOL Timer_ChangeStartAndPeriod(TIMERHDL t, TIMER_ID id, TUNIT start, TUNIT period)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	TIMERITEM* item = (TIMERITEM*)id;

	if ( !tm ) return FALSE;
	if ( !item ) return FALSE;

	// �����V���b�g�̏ꍇ�͎��s���I�h��NEVER�ɂ��Ă����i���������g�������V���b�g�^�C�}�͂Ȃ��͂������j
	item->period  = ( item->flag & TIMER_ONESHOT ) ? TIMERPERIOD_NEVER : period;
	item->current = start;
	if ( item->current != TIMERPERIOD_NEVER ) {
		// ���ݎ��Ԃ���ɒǉ�
		item->current += tm->curtime;
		// CPU���s���Ƀ^�C�}�N�����ꂽ�ꍇ�̕␳
		if ( tm->cpu_current ) {
			SINT32 cnt = (SINT32)tm->cpu_current->cpu->slice_clk(tm->cpu_current ->cpu);
			TUNIT dif = (TUNIT)cnt / (TUNIT)tm->cpu_current->cpu->freq;
			item->current += dif;
		}
	}
	RemoveTimerItem(tm, item);
	AddTimerItem(tm, item);

	// �擪�ɒǉ����ꂽ�Ȃ獡�̃X���C�X�̏I�����N������O�ȉ\��������̂ŁACPU�X���C�X���f
	if ( !item->prev ) InterruptCPUSlice(tm);

	return TRUE;
}


// �I�v�V�����p�����[�^�̐ݒ�
BOOL Timer_ChangeOptPrm(TIMER_ID id, UINT32 opt, UINT32 opt2)
{
	TIMERITEM* item = (TIMERITEM*)id;
	if ( !item ) return FALSE;
	item->opt  = opt;
	item->opt2 = opt2;
	return TRUE;
}


// �I�v�V�����p�����[�^2�̎擾
UINT32 Timer_GetOptPrm2(TIMER_ID id)
{
	TIMERITEM* item = (TIMERITEM*)id;
	if ( !item ) return 0;
	return item->opt2;
}


// ���݂̃s���I�h�擾
TUNIT Timer_GetPeriod(TIMER_ID id)
{
	TIMERITEM* item = (TIMERITEM*)id;
	if ( !item ) return TIMERPERIOD_NEVER;
	return item->period;
}


// CPU�X���C�X����
void Timer_CPUSlice(TIMERHDL t)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	if ( tm ) {
		InterruptCPUSlice(tm);
	}
}


// �����T�E���h�T���v��������̃��Z�b�g
void Timer_SetSampleLimit(TIMERHDL t, UINT32 samples)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	if ( tm ) {
		// 140625 Kenjo, ForceUpdate�ŃI�[�o�[�������ꂽ�ꍇ�́A���X���C�X�̃��~�b�g�����������
		if ( tm->strmremain<0 ) {
			tm->strmremain += samples;
		} else {
			tm->strmremain = samples;
		}
	}
}


// �^�C�}���s
void Timer_Exec(TIMERHDL t, TUNIT period)
{
	INFO_TIMER* tm = (INFO_TIMER*)t;
	TUNIT clk = period;

	if ( !tm ) return;

	// �O�񒴉ߏ����������ԁi�����j
	clk += tm->remain;

	// ���̃t���[���̌��ݎ���
	tm->curtime = TUNIT_ZERO;

	while ( clk > TUNIT_ZERO )
	{
		TUNIT idle = clk;

		// ���̎��s���Ԃ܂ł��Z�����ɕ���ł�̂ŁA�ŒZ�C�x���g�͐擪�̃A�C�e��
		if ( tm->top ) {
			TIMERITEM* item = tm->top;
			TUNIT next = item->current - tm->curtime;
			if ( next < idle ) idle = next;
			if ( idle < TUNIT_ZERO ) idle = TUNIT_ZERO;  // �ꉞ�����߂�Ȃ��悤��
		}

		// �A�C�h�����ԕ��ACPU����
		if ( idle > TUNIT_ZERO ) {
			idle = ExecCPU(tm, idle);
		}

		// �T�E���h�X�g���[���v�b�V������
		if ( tm->strmfunc ) {
			TUNIT tick = (((TUNIT)tm->strmfreq) * idle ) + tm->strmfix;
			SINT32 smpl = (SINT32)tick;
			tm->strmfix = tick-(TUNIT)smpl;  // �[����ێ�
			if ( smpl > tm->strmremain ) {
				smpl = tm->strmremain;  // �v�b�V����������T���v�����Ƀ��~�b�g���|����
			}
			if ( smpl > 0 ) {
				tm->strmremain -= smpl;
				tm->strmfunc(tm->strmprm, smpl);
			}
		}

		// CPU����������^�C�}��i�߂�
		tm->curtime += idle;

		// ���������ԓ��B�����Ƃ��͓Z�߂ď�������
		while ( tm->top ) {
			// ���s����0�̃A�C�e�������ȋN����������Ɩ������[�v�ɗ�����̂Œ��Ӂi�Ȃ��͂������j
			TIMERITEM* item = tm->top;
			// ���Ԃɓ��B���Ă���R�Â���ꂽ�֐��Ăяo��
			if ( tm->curtime >= item->current ) {
				// �Ăяo�����ōēx ChangePeriod() �Ŏ��g���N�����꒼���\��������̂ŁA�܂����X�g����O��
				RemoveTimerItem(tm, item);
				// ���̎��s���Ԃ܂ŉ��Z���Ă���
				if ( item->period != TIMERPERIOD_NEVER ) {
					item->current += item->period;
				} else {
					item->current = TIMERPERIOD_NEVER;
				}
				// �֐��Ăяo��
				item->func(item->prm, item->opt);
				// �Ăяo�����ŋN������ĂȂ���Βǉ�������
				if ( !item->in_list ) {
					AddTimerItem(tm, item);
				}
			} else {
				// ���ԓ��B�O�̃A�C�e���ɂԂ�������I��
				break;
			}
		}

		// �����������̎��Ԃ�����
		clk -= idle;
	}

	// �S�Ă̋N���A�C�e��������̃t���[�����ԕ��i�s
	{
		TIMERITEM* item = tm->top;
		while ( item ) {
			// NEVER�̃A�C�e�����o����ȍ~�̃A�C�e���͐i�s�̕K�v�Ȃ�
			if ( item->current == TIMERPERIOD_NEVER ) break;
			item->current -= tm->curtime;
			item = item->next;
		}
	}

	// �t���[�����Ԃ�0�ɖ߂��Ă���
	tm->curtime = TUNIT_ZERO;

	// ���񒴉ߏ����������ԁi�����j
	tm->remain = clk;
}


// �X�g���[���̍X�V
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
			// ���^�C���X���C�X�ɕ�����Ă΂��\��������̂ŁA�utick-(double)smpl�v�i�[�������j�ł͂Ȃ��P���Ɍ��Z�i�ݐς�����j
			tm->strmfix -= (TUNIT)smpl;
			if ( smpl>tm->strmremain ) {
				smpl = tm->strmremain;  // �v�b�V����������T���v�����Ƀ��~�b�g���|����
			}
			if ( smpl>0 ) {
				tm->strmremain -= smpl;
				tm->strmfunc(tm->strmprm, smpl);
			}
		}
	}
}


// �X�e�[�g���[�h/�Z�[�u
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

		// CPU���X�e�[�g���[�h
		ReadState(state, id, MAKESTATEID('@','T','D',('0')), &cpu->time_dif, sizeof(cpu->time_dif));
		LoadIrqState(cpu->cpu, state, cpu->id);

		ReadState(state, id, MAKESTATEID('S','L','T','M'), &tm->slicetime, sizeof(tm->slicetime));

		// �S�^�C�}�A�C�e�����X�e�[�g���[�h���ĕ���
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
		// idx=0���珇�ɕ��ג���
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

		// CPU���X�e�[�g�Z�[�u
		WriteState(state, id, MAKESTATEID('@','T','D',('0')), &cpu->time_dif, sizeof(cpu->time_dif));
		SaveIrqState(cpu->cpu, state, cpu->id);

		WriteState(state, id, MAKESTATEID('S','L','T','M'), &tm->slicetime, sizeof(tm->slicetime));

		// ���X�g������o���X�e�[�g�Z�[�u
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
   ���荞��
-------------------------------------------------------------------------- */

typedef struct {
	TUNIT    delay[IRQLINE_MAX+1];
	TIMERHDL timer;
	TIMER_ID timerid[IRQLINE_MAX+1];
} IRQINFO;


// --------------------------------------------------------------------------
//   �R�[���o�b�N
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
//   ���J�֐�
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

	// �eTimerItem�́u#IxA�v�`�u#IxP�v����сu#IEx�v��ID�ix��CPU�ŗL�ԍ��j������
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
		if ( 0/*!info->delay[line]*/ ) {  // �グ����̓^�C�}���g��
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
		if ( 1 ) {  // ���Ƃ����͑�
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
