/* -----------------------------------------------------------------------------------
  Win32キーボード入力
                                                         (c) 2024 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef __win32_keyboard_h
#define __win32_keyboard_h

#include "osconfig.h"
#include "emu_driver.h"
#include "win32config.h"
#include <vector>

#if (__cplusplus>=201103L) || (_MSC_VER>=1600)  // C++11 or VS2010以上（VS2010/2013では通るのを確認済みのため）
#include <unordered_map>
#define STDMAP std::unordered_map
//#pragma message(" - Using std::unordered_map")
#else
#include <map>
#define STDMAP std::map
//#pragma message(" - Using std::map")
#endif

class CWin32Keyboard
{
public:
	CWin32Keyboard();
	virtual ~CWin32Keyboard();

	void Init(EMUDRIVER* driver, CWin32Config* cfg);

	void CheckInput();
	void Press(WPARAM wParam, LPARAM lParam);
	void Release(WPARAM wParam, LPARAM lParam);
	void Clear();

private:
	EMUDRIVER* m_pDrv;
	CWin32Config* m_pCfg;
	STDMAP<UINT32,UINT8> mMapToX68k;
	STDMAP<UINT8,UINT32> mMapToWin;
	std::vector<UINT8> mKeyBuf;
};

#endif //__win32_keyboard_h
