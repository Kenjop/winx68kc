/* -----------------------------------------------------------------------------------
  "SHARP X68000" RTC
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	ほぼ唯一機種依存を含むコード（localtime_s 部分）
	最低限、ドラキュラStage15（時計塔）の時計がちゃんと現時刻に同期するのは確認済。
	その他数値が正しく取れてるかどうかは未確認。
*/

#include "x68000_rtc.h"
#include <stdio.h>
#include <time.h>


typedef struct {
	UINT32    bank;
	UINT8     regs[2][3];
} INFO_RTC;


// --------------------------------------------------------------------------
//   公開関数
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
			もし localtime_s / localtime_r のどちらも使えない環境の場合、旧版の localtime か、
			もしくは環境専用のローカル時間取得関数を使ってください。
		*/
#ifdef WIN32
		localtime_s(&l, &t);
#else  // gcc
		localtime_r(&t, &l);  // localtime_s とは引数順が異なるので注意
#endif
		
		// Bank1 では本来アラーム情報などが取れるが、ここでは特に考慮しない
		// XXX うるう年カウンタとか要る？
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
		case 0x09: ret = (l.tm_mon+1) % 10; break;                   // tm_mon は 0～11 なので、1～12 に変換
		case 0x0A: ret = (l.tm_mon+1) / 10; break;
		case 0x0B: ret = ((l.tm_year+1900-1980) %100 ) % 10; break;  // tm_year は 1900 年からの年数、Human68k は 1980 年からの年数
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
