/* -----------------------------------------------------------------------------------
  "SHARP X68000" RTC
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	�قڗB��@��ˑ����܂ރR�[�h�ilocaltime_s �����j
	�Œ���A�h���L����Stage15�i���v���j�̎��v�������ƌ������ɓ�������̂͊m�F�ρB
	���̑����l�����������Ă邩�ǂ����͖��m�F�B
*/

#include "x68000_rtc.h"
#include <stdio.h>
#include <time.h>


typedef struct {
	UINT32    bank;
	UINT8     regs[2][3];
} INFO_RTC;


// --------------------------------------------------------------------------
//   ���J�֐�
// --------------------------------------------------------------------------
X68RTC X68RTC_Init(void)
{
	INFO_RTC* rtc = NULL;
	do {
		rtc = (INFO_RTC*)_MALLOC(sizeof(INFO_RTC), "RTC struct");
		if ( !rtc ) break;
		memset(rtc, 0, sizeof(INFO_RTC));
		X68RTC_Reset((X68RTC)rtc);
		LOG(("RTC : initialize OK"));
		return (X68RTC)rtc;
	} while ( 0 );

	X68RTC_Cleanup((X68RTC)rtc);
	return NULL;
}

void X68RTC_Cleanup(X68RTC hdl)
{
	INFO_RTC* rtc = (INFO_RTC*)hdl;
	if ( rtc ) {
		_MFREE(rtc);
	}
}

void X68RTC_Reset(X68RTC hdl)
{
	INFO_RTC* rtc = (INFO_RTC*)hdl;
	if ( rtc ) {
	}
}

MEM16W_HANDLER(X68RTC_Write)
{
	INFO_RTC* rtc = (INFO_RTC*)prm;
	if ( adr & 1 ) {
		adr = ( adr & 0x1F ) >> 1;
		switch ( adr )
		{
		default:
		case 0x0D:
			rtc->bank = data & 1;
			rtc->regs[rtc->bank][0] = (UINT8)(data & 0x0D);
			break;
		case 0x0E:
			rtc->regs[rtc->bank][1] = (UINT8)(data & 0x0F);
			break;
		case 0x0F:
			rtc->regs[rtc->bank][2] = (UINT8)(data & 0x0F);
			break;
		}
	}
}

MEM16R_HANDLER(X68RTC_Read)
{
	INFO_RTC* rtc = (INFO_RTC*)prm;
	UINT32 ret = 0;
	if ( adr & 1 ) {
		time_t t = time(NULL);
		struct tm l;

		/*
			���� localtime_s / localtime_r �̂ǂ�����g���Ȃ����̏ꍇ�A���ł� localtime ���A
			�������͊���p�̃��[�J�����Ԏ擾�֐����g���Ă��������B
		*/
#ifdef WIN32
		localtime_s(&l, &t);
#else  // gcc
		localtime_r(&t, &l);  // localtime_s �Ƃ͈��������قȂ�̂Œ���
#endif
		
		// Bank1 �ł͖{���A���[�����Ȃǂ����邪�A�����ł͓��ɍl�����Ȃ�
		// XXX ���邤�N�J�E���^�Ƃ��v��H
		adr = ( adr & 0x1F ) >> 1;
		switch ( adr )
		{
		default:
		case 0x00: ret =  l.tm_sec    % 10; break;
		case 0x01: ret =  l.tm_sec    / 10; break;
		case 0x02: ret =  l.tm_min    % 10; break;
		case 0x03: ret =  l.tm_min    / 10; break;
		case 0x04: ret =  l.tm_hour   % 10; break;
		case 0x05: ret =  l.tm_hour   / 10; break;
		case 0x06: ret =  l.tm_wday;        break;
		case 0x07: ret =  l.tm_mday   % 10; break;
		case 0x08: ret =  l.tm_mday   / 10; break;
		case 0x09: ret = (l.tm_mon+1) % 10; break;                   // tm_mon �� 0�`11 �Ȃ̂ŁA1�`12 �ɕϊ�
		case 0x0A: ret = (l.tm_mon+1) / 10; break;
		case 0x0B: ret = ((l.tm_year+1900-1980) %100 ) % 10; break;  // tm_year �� 1900 �N����̔N���AHuman68k �� 1980 �N����̔N��
		case 0x0C: ret = ((l.tm_year+1900-1980) %100 ) / 10; break;
		case 0x0D: ret = rtc->regs[rtc->bank][0]; break;
		case 0x0E: ret = rtc->regs[rtc->bank][1]; break;
		case 0x0F: ret = rtc->regs[rtc->bank][2]; break;
		}
	}
	return ret;
}


void X68RTC_LoadState(X68RTC hdl, STATE* state, UINT32 id)
{
	INFO_RTC* rtc = (INFO_RTC*)hdl;
	if ( rtc && state ) {
		ReadState(state, id, MAKESTATEID('S','T','R','C'), rtc, sizeof(INFO_RTC));
	}
}

void X68RTC_SaveState(X68RTC hdl, STATE* state, UINT32 id)
{
	INFO_RTC* rtc = (INFO_RTC*)hdl;
	if ( rtc && state ) {
		WriteState(state, id, MAKESTATEID('S','T','R','C'), rtc, sizeof(INFO_RTC));
	}
}
