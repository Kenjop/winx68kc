/* -----------------------------------------------------------------------------------
  Win32ジョイスティック入力
                                                      (c) 2004-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef __win32_joystick_h
#define __win32_joystick_h

#include "osconfig.h"
#include "emu_driver.h"
#include "win32config.h"

class CWin32Joystick
{
public:
	CWin32Joystick();
	virtual ~CWin32Joystick();

	void Init(EMUDRIVER* drv, CWin32Config* cfg);
	void JoyKeyPress(WPARAM wParam);
	void JoyKeyRelease(WPARAM wParam);
	void JoyKeyClear();

	void CheckInput();

private:
	EMUDRIVER*     m_pDrv;
	CWin32Config*  m_pCfg;
	UINT32         m_nJoyKeyBtn;
	UINT32         m_nJoyKeyDir;
};

#endif //__win32_joystick_h
