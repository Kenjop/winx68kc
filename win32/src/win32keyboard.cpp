/* -----------------------------------------------------------------------------------
  Windows -> X68000 ÉLÅ[ïœä∑
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "win32keyboard.h"
#include "emu_driver.h"
#include "x68000_driver.h"

CWin32Keyboard::CWin32Keyboard()
{
	m_pDrv = NULL;
	mKeyBuf.clear();
}

CWin32Keyboard::~CWin32Keyboard()
{
}

void CWin32Keyboard::Init(EMUDRIVER* driver, CWin32Config* cfg)
{
	m_pDrv = driver;
	m_pCfg = cfg;
}

void CWin32Keyboard::CheckInput()
{
	for (UINT32 i=0; i<mKeyBuf.size(); i++) {
		X68kDriver_KeyInput(m_pDrv, mKeyBuf[i]);
	}
	mKeyBuf.clear();
}

void CWin32Keyboard::Press(WPARAM wParam, LPARAM lParam)
{
	UINT32 code = ( wParam & 0xFF ) | ( ( lParam >> 16 ) & 0x100 );
	UINT32 x68key = m_pCfg->mDlgCfg.nKeyMap[code];
	if ( x68key ) {
		mKeyBuf.push_back(x68key);
	}
}

void CWin32Keyboard::Release(WPARAM wParam, LPARAM lParam)
{
	UINT32 code = ( wParam & 0xFF ) | ( ( lParam >> 16 ) & 0x100 );
	UINT32 x68key = m_pCfg->mDlgCfg.nKeyMap[code];
	if ( x68key ) {
		mKeyBuf.push_back(x68key|0x80);
	}
}

void CWin32Keyboard::Clear()
{
	mKeyBuf.clear();
	X68kDriver_KeyClear(m_pDrv);
}
