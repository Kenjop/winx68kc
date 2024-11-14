/* -----------------------------------------------------------------------------------
  Win32ジョイスティック入力
                                                      (c) 2004-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "osconfig.h"
#include "win32joystick.h"
#include "x68000_driver.h"

#pragma comment(lib, "winmm.lib")

#define POV_ANGLE_RANGE  50  // 0/90/180/270度を中心に+/-何度の範囲を受け付けるか（単位は度）


CWin32Joystick::CWin32Joystick()
{
}

CWin32Joystick::~CWin32Joystick()
{
}

void CWin32Joystick::Init(EMUDRIVER* driver, CWin32Config* cfg)
{
	m_pDrv = driver;
	m_pCfg = cfg;
	m_nJoyKeyBtn = 0;
	m_nJoyKeyDir = 0;
}

void CWin32Joystick::JoyKeyPress(WPARAM wParam)
{
	switch ( wParam )
	{
	case 'Z':      m_nJoyKeyBtn |= 1 << 0;         break;
	case 'X':      m_nJoyKeyBtn |= 1 << 1;         break;
	case 'C':      m_nJoyKeyBtn |= 1 << 2;         break;  // XXX 現状未使用
	case 'V':      m_nJoyKeyBtn |= 1 << 3;         break;  // XXX 現状未使用
	case VK_LEFT:  m_nJoyKeyDir |= X68K_JOY_LEFT;  break;
	case VK_RIGHT: m_nJoyKeyDir |= X68K_JOY_RIGHT; break;
	case VK_UP:    m_nJoyKeyDir |= X68K_JOY_UP;    break;
	case VK_DOWN:  m_nJoyKeyDir |= X68K_JOY_DOWN;  break;
	default:
		break;
	}
}

void CWin32Joystick::JoyKeyRelease(WPARAM wParam)
{
	switch ( wParam )
	{
	case 'Z':      m_nJoyKeyBtn &= ~( 1 << 0 );         break;
	case 'X':      m_nJoyKeyBtn &= ~( 1 << 1 );         break;
	case 'C':      m_nJoyKeyBtn &= ~( 1 << 2 );         break;  // XXX 現状未使用
	case 'V':      m_nJoyKeyBtn &= ~( 1 << 3 );         break;  // XXX 現状未使用
	case VK_LEFT:  m_nJoyKeyDir &= ~( X68K_JOY_LEFT );  break;
	case VK_RIGHT: m_nJoyKeyDir &= ~( X68K_JOY_RIGHT ); break;
	case VK_UP:    m_nJoyKeyDir &= ~( X68K_JOY_UP );    break;
	case VK_DOWN:  m_nJoyKeyDir &= ~( X68K_JOY_DOWN );  break;
	default:
		break;
	}
}

void CWin32Joystick::JoyKeyClear()
{
	m_nJoyKeyBtn = 0;
	m_nJoyKeyDir = 0;
}

void CWin32Joystick::CheckInput()
{
	UINT32 result[2] = { 0xFF, 0xFF };
	UINT32 i;
	JOYINFOEX ji;
	for (i=0; i<2; i++) {
		UINT32 joyidx = m_pCfg->mDlgCfg.nJoystickIdx[i];
		if ( joyidx == 1 ) {
			// JoyKey
			UINT32 ret = m_nJoyKeyDir;
			// ボタン設定は0は「なし」、1～が有効
			if ( ( ( m_nJoyKeyBtn << 1 ) >> m_pCfg->mDlgCfg.nJoyKeyBtn[i][0] ) & 1 ) { ret |= X68K_JOY_BUTTON1; }
			if ( ( ( m_nJoyKeyBtn << 1 ) >> m_pCfg->mDlgCfg.nJoyKeyBtn[i][1] ) & 1 ) { ret |= X68K_JOY_BUTTON2; }
			result[i] = ret ^ 0xFF;
		} else if ( joyidx >= 2 ) {
			// ジョイスティック
			UINT32 id = JOYSTICKID1 + (joyidx-2);
			memset(&ji, 0, sizeof(JOYINFOEX));
			ji.dwSize = sizeof(JOYINFOEX);
			ji.dwFlags = JOY_RETURNALL;
			if ( joyGetPosEx(id, &ji)==JOYERR_NOERROR ) {
				UINT32 ret = 0;
				// ボタン設定は0は「なし」、1～が有効
				if ( ( ( ji.dwButtons << 1 ) >> m_pCfg->mDlgCfg.nJoystickBtn[i][0] ) & 1 ) { ret |= X68K_JOY_BUTTON1; }
				if ( ( ( ji.dwButtons << 1 ) >> m_pCfg->mDlgCfg.nJoystickBtn[i][1] ) & 1 ) { ret |= X68K_JOY_BUTTON2; }
				if ( ji.dwXpos < (65536  /4) ) ret |= X68K_JOY_LEFT;
				if ( ji.dwXpos > (65536*3/4) ) ret |= X68K_JOY_RIGHT;
				if ( ji.dwYpos < (65536  /4) ) ret |= X68K_JOY_UP;
				if ( ji.dwYpos > (65536*3/4) ) ret |= X68K_JOY_DOWN;
				if ( ji.dwPOV < 35900 ) {
					if ( ji.dwPOV < ((  0+POV_ANGLE_RANGE)*100) || ji.dwPOV > ((360-POV_ANGLE_RANGE)*100) ) ret |= X68K_JOY_UP;
					if ( ji.dwPOV > (( 90-POV_ANGLE_RANGE)*100) && ji.dwPOV < (( 90+POV_ANGLE_RANGE)*100) ) ret |= X68K_JOY_RIGHT;
					if ( ji.dwPOV > ((180-POV_ANGLE_RANGE)*100) && ji.dwPOV < ((180+POV_ANGLE_RANGE)*100) ) ret |= X68K_JOY_DOWN;
					if ( ji.dwPOV > ((270-POV_ANGLE_RANGE)*100) && ji.dwPOV < ((270+POV_ANGLE_RANGE)*100) ) ret |= X68K_JOY_LEFT;
				}
				result[i] = ret ^ 0xFF;
			}
		}
	}
	X68kDriver_JoyInput(m_pDrv, result[0], result[1]);
}
