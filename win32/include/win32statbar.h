/* -----------------------------------------------------------------------------------
  Win32ステータスバー処理
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef __win32_statbar_h
#define __win32_statbar_h

#include "osconfig.h"
#include "x68000_driver.h"


typedef enum {
	STATBAR_COLORSET_NORMAL = 0,
	STATBAR_COLORSET_FULLSCREEN,

	STATBAR_COLORSET_MAX
} STATBAR_COLORSET;


class CWin32StatBar
{
	public:
		CWin32StatBar();
		virtual ~CWin32StatBar();

		void Init(HWND hwnd);
		void Show(BOOL sw);
		void UpdatePos();
		void Invalidate();
		void CheckDriveStatus(EMUDRIVER* drv, BOOL has_hdd);
		void Draw(DRAWITEMSTRUCT* dis);

		void SelectColorset(STATBAR_COLORSET n);
		void SetImageName(UINT32 drive, const TCHAR* name);

		UINT32 GetHeight() { return ( m_bShow ) ? m_nHeight : 0; }

	private:
		void DrawDriveInfo(DRAWITEMSTRUCT* dis, UINT32 drive);
		void DrawHddInfo(DRAWITEMSTRUCT* dis);
		void DrawCrtInfo(DRAWITEMSTRUCT* dis);

		enum {
			STATBAR_COLOR_BK = 0,
			STATBAR_COLOR_FONT,
			STATBAR_COLOR_LED_OFF,
			STATBAR_COLOR_LED_GREEN,
			STATBAR_COLOR_LED_RED,
			STATBAR_COLOR_MAX
		};

		HWND             m_hMainWnd;
		HWND             m_hWnd;

		BOOL             m_bShow;

		UINT32           m_nHeight;
		STATBAR_COLORSET m_nColorSet;
		INFO_X68FDD_LED  m_stLedState;
		X68FDD_LED_STATE m_nHddLed;
		UINT32           m_nColorTable[STATBAR_COLORSET_MAX][CWin32StatBar::STATBAR_COLOR_MAX];

		TCHAR            m_sImageName[2][MAX_PATH];
		float            m_fHSync;
		float            m_fVSync;
};


#endif //__win32_statbar_h
