/* -----------------------------------------------------------------------------------
  "SHARP X68000" IOC
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "x68000_ioc.h"


typedef struct {
	CPUDEV*   cpu;
	UINT32    vector;
	UINT32    stat;
} INFO_IOC;


// --------------------------------------------------------------------------
//   �����֐�
// --------------------------------------------------------------------------
static void CheckIrq(INFO_IOC* ioc)
{
	UINT32 flag = ioc->stat & (ioc->stat>>4);
	if ( flag ) {
		IRQ_Request(ioc->cpu, IRQLINE_LINE|1, 0);
	} else {
		IRQ_Clear(ioc->cpu, 1);
	}
}


// --------------------------------------------------------------------------
//   ���J�֐�
// --------------------------------------------------------------------------
X68IOC X68IOC_Init(CPUDEV* cpu)
{
	INFO_IOC* ioc = NULL;
	do {
		ioc = (INFO_IOC*)_MALLOC(sizeof(INFO_IOC), "IOC struct");
		if ( !ioc ) break;
		memset(ioc, 0, sizeof(INFO_IOC));
		ioc->cpu = cpu;
		X68IOC_Reset((X68IOC)ioc);
		LOG(("IOC : initialize OK"));
		return (X68IOC)ioc;
	} while ( 0 );

	X68IOC_Cleanup((X68IOC)ioc);
	return NULL;
}

void X68IOC_Cleanup(X68IOC hdl)
{
	INFO_IOC* ioc = (INFO_IOC*)hdl;
	if ( ioc ) {
		_MFREE(ioc);
	}
}

void X68IOC_Reset(X68IOC hdl)
{
	INFO_IOC* ioc = (INFO_IOC*)hdl;
	if ( ioc ) {
		ioc->stat = 0;
		IRQ_Clear(ioc->cpu, 1);
	}
}

MEM16W_HANDLER(X68IOC_Write)
{
	INFO_IOC* ioc = (INFO_IOC*)prm;
	switch ( adr & 0x00F )
	{
		case 0x001:  // 0xE9C001
			ioc->stat &= 0xF0;
			ioc->stat |= data & 0x0F;
			CheckIrq(ioc);
			break;
		case 0x003:  // 0xE9C003
			ioc->vector = data & 0xFC;
			break;
		default:
			break;
	}
}

MEM16R_HANDLER(X68IOC_Read)
{
	INFO_IOC* ioc = (INFO_IOC*)prm;
	UINT32 ret = 0xFF;
	switch ( adr & 0x00F )
	{
		case 0x001:  // 0xE9C001
			// IE �� STAT �Ńr�b�g���т��قȂ�
			ret = ioc->stat & 0x0F;
			if ( ioc->stat & (0x10<<IOCIRQ_PRN) ) ret |= 0x20;
			if ( ioc->stat & (0x10<<IOCIRQ_FDD) ) ret |= 0x40;
			if ( ioc->stat & (0x10<<IOCIRQ_FDC) ) ret |= 0x80;
			if ( ioc->stat & (0x10<<IOCIRQ_HDD) ) ret |= 0x10;
			break;
		default:
			break;
	}
	return ret;
}

void X68IOC_SetIrq(X68IOC hdl, IOCIRQ irq, BOOL sw)
{
	// STAT �͓����I�ɂ� IE �Ɠ������тŊǗ�����iRead���ɂ͒u�������ĕԂ��j
	INFO_IOC* ioc = (INFO_IOC*)hdl;
	UINT32 old = ioc->stat;
	ioc->stat = ( ioc->stat & ~(0x10<<irq) ) | ( ( (sw) ? 0x10 : 0x00 ) << irq );
	if ( old != ioc->stat ) {
		CheckIrq(ioc);
	}
}

int X68IOC_GetIntVector(X68IOC hdl)
{
	INFO_IOC* ioc = (INFO_IOC*)hdl;
	UINT32 flag = ioc->stat & (ioc->stat>>4);
	int ret = -1;

	// XXX �D�揇�ʕs��
	// �x�N�^���� IE �Ƃ� STAT �Ƃ����т��قȂ�̂Œ���
	do {
		// XXX IOC�̊��荞�݂͎�t���_�ŗ��Ƃ��Ă����i�l�Q�[�g�^�C�~���O���̂��߂�ǂ��j
		if ( flag & (1<<IOCIRQ_FDC) ) { ret = ioc->vector + 0; ioc->stat &= ~(0x10<<IOCIRQ_FDC); break; }
		if ( flag & (1<<IOCIRQ_FDD) ) { ret = ioc->vector + 1; ioc->stat &= ~(0x10<<IOCIRQ_FDD); break; }
		if ( flag & (1<<IOCIRQ_HDD) ) { ret = ioc->vector + 2; ioc->stat &= ~(0x10<<IOCIRQ_HDD); break; }
		if ( flag & (1<<IOCIRQ_PRN) ) { ret = ioc->vector + 3; ioc->stat &= ~(0x10<<IOCIRQ_PRN); break; }
		// �ǂ�ɂ��q�b�g���Ȃ��i�N����Ȃ��͂������j
		return ret;
	} while ( 0 );

	// �����ɗ�������m���Ɋ��荞�ݏ�Ԃ��ω����Ă���
	CheckIrq(ioc);
	return ret;
}


void X68IOC_LoadState(X68IOC hdl, STATE* state, UINT32 id)
{
	INFO_IOC* ioc = (INFO_IOC*)hdl;
	if ( ioc && state ) {
		ReadState(state, id, MAKESTATEID('V','E','C','T'), &ioc->vector, sizeof(ioc->vector));
		ReadState(state, id, MAKESTATEID('S','T','A','T'), &ioc->stat, sizeof(ioc->stat));
	}
}

void X68IOC_SaveState(X68IOC hdl, STATE* state, UINT32 id)
{
	INFO_IOC* ioc = (INFO_IOC*)hdl;
	if ( ioc && state ) {
		WriteState(state, id, MAKESTATEID('V','E','C','T'), &ioc->vector, sizeof(ioc->vector));
		WriteState(state, id, MAKESTATEID('S','T','A','T'), &ioc->stat, sizeof(ioc->stat));
	}
}
