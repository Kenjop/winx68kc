/* -----------------------------------------------------------------------------------
  Win32ウィンドウ・メニュー関連処理
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef __win32_menu_h
#define __win32_menu_h

#include "osconfig.h"
#include "win32core.h"
#include "win32statbar.h"
#include "win32config.h"
#include <list>

using namespace std;

class CDiskEvent
{
public:
	CDiskEvent(UINT32 drive, const TCHAR* file)
	{
		m_nDrive = drive;
		_tcscpy(m_sFile, file);
	}
	virtual ~CDiskEvent()
	{
	}

	UINT32 m_nDrive;
	TCHAR m_sFile[MAX_PATH];
};

class CWin32Menu
{
public:
	CWin32Menu();
	virtual ~CWin32Menu();

	void Init(HINSTANCE hInst, HWND hwnd, HMENU hmenu, CWin32Config* cfg, CWin32Core* core);

	void SetupMenu();
	void MenuProc(WPARAM wParam, LPARAM lParam);

	void SetWindowSize(int width, int height);
	void SetFullScreen(BOOL sw);
	void ChangeScreenSize(UINT32 width);
	void ChangeScreenAspect(UINT32 aspect);

	void UpdateStatPos() { mStatBar.UpdatePos(); }
	void DrawStatBar(DRAWITEMSTRUCT* dis) { mStatBar.Draw(dis); };
	void UpdateStatInfo() { mStatBar.CheckDriveStatus(m_pCore->GetDriver(), (m_pCfg->mDlgCfg.nSasiNum>0)?TRUE:FALSE); }

	void MouseCapture(BOOL sw);
	void MouseEvent(WPARAM wParam, LPARAM lParam);
	void ToggleMouseCapture() { MouseCapture(!m_bMouseCapture); };

	void SetDisk(UINT32 drive, const TCHAR* image_file);
	void EjectDisk(UINT32 drive);
	void ExecDiskEvent();

private:
	CWin32StatBar mStatBar;

	CWin32Core*   m_pCore;
	CWin32Config* m_pCfg;

	HINSTANCE     m_hInst;
	HWND          m_hWnd;
	HMENU         m_hMenu;
	D3DDRAWHDL    m_hD3D;

	BOOL          m_bFullScreen;

	POINT         m_ptWinPos;
	POINT         m_ptWinSize;

	BOOL          m_bMouseCapture;
	POINT	      m_ptMousePos;
	POINT         m_ptMouseMove;
	UINT32        m_nMouseBtn;

	TCHAR         m_sImageFile[2][MAX_PATH];

	list<CDiskEvent> mDiskEvent;

	void UpdateStatBarState();
	void CheckMenu(UINT32 id, BOOL check);

	void LoadState(TCHAR* filename);
	void LoadState(int no);
	void SaveState(TCHAR* filename);
	void SaveState(int no);
	void OpenStateFile(BOOL is_write);

	void OpenDiskImage(UINT32 drive);
	void ToUpper(TCHAR* str);
	X68K_DISK_TYPE FindDiskType(const TCHAR* str);

	void DoSetDisk(UINT32 drive, const TCHAR* image_file);
	void DoEjectDisk(UINT32 drive);
	void SaveDiskImage(UINT32 drive, const UINT8* ptr, UINT32 sz);
	static void CALLBACK DiskEjectCallback(void* prm, UINT32 drive, const UINT8* ptr, UINT32 sz);
};

#endif //__win32_menu_h
