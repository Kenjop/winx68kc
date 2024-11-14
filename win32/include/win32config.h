/* -----------------------------------------------------------------------------------
  Win32保存される設定類
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef __win32_settings_h
#define __win32_settings_h

#include "osconfig.h"
#include "win32d3d.h"
#include <map>
#include <vector>
#include <atlstr.h>

#define __SYS_APPTITLE__  _T("WinX68k Compact")
#define __SYS_APPNAME__   _T("WinX68kCompact")

using namespace std;

class CWin32Config
{
public:
	CWin32Config();
	~CWin32Config();

	void Init();
	void Load();
	void Save();

	void ShowSettingDialog(HINSTANCE hInst, HWND hWnd);

	// システムメニュー
	UINT32                nStateSlot;

	// 表示メニュー
	UINT32                nScreenSize;
	UINT32                nScreenAspect;
	BOOL                  bZoomIgnoreCrtc;
	BOOL                  bScreenInterp;
	BOOL                  bHardwareVsync;
	BOOL                  bFullScreen;

	// オプションメニュー
	UINT32                nCpuClock;

	typedef struct {
		// 設定ダイアログ - 動作設定
		UINT32            nRamSize;
		BOOL              bRamSizeUpdate;
		BOOL              bFastFdd;

		// 設定ダイアログ - キーボード
		UINT8             nKeyMap[0x200];

		// 設定ダイアログ - ジョイスティック
		UINT32            nJoystickIdx[2];
		UINT32            nJoystickBtn[2][2];
		UINT32            nJoyKeyBtn[2][2];

		// 設定ダイアログ - マウス
		SINT32            nMouseSpeed;

		// 設定ダイアログ - サウンド
		SINT32            nVolumeOPM;
		SINT32            nVolumeADPCM;
		UINT32            nFilterOPM;
		UINT32            nFilterADPCM;

		// 設定ダイアログ - MIDI
		SINT32            nMidiDeviceId;
		UINT32            nMidiModuleType;
		BOOL              bMidiSendReset;

		// 設定ダイアログ - SASI HDD
		TCHAR             sSasiFile[16][MAX_PATH];
		UINT32            nSasiSize[16];
		UINT32            nSasiNum;
		BOOL              bSasiSramUpd;

		// 設定ダイアログ - その他
		BOOL              bShowStatBar;
		BOOL              bShowFsStat;
		UINT32            nTitleBarInfo;
	} DLGCFG;
	DLGCFG                mDlgCfg;

private:
	void LoadItem(const TCHAR* name, TCHAR* str, const TCHAR* def);
	void LoadItem(const TCHAR* name, UINT32& num, const UINT32 def);
	void LoadItem(const TCHAR* name, SINT32& num, const SINT32 def);
	void SaveItem(const TCHAR* name, const TCHAR* str);
	void SaveItem(const TCHAR* name, UINT32 num);
	void SaveItem(const TCHAR* name, SINT32 num);

	void SetupSystemPage(HWND hDlg);
	void SetupKeyboardPage(HWND hDlg);
	void SetupJoystickPage(HWND hDlg);
	void SetupMousePage(HWND hDlg);
	void SetupSoundPage(HWND hDlg);
	void SetupMidiPage(HWND hDlg);
	void SetupSasiPage(HWND hDlg);
	void SetupEtcPage(HWND hDlg);

	void DefaultSystemPage(HWND hDlg);
	void DefaultKeyboardPage(HWND hDlg);
	void DefaultMousePage(HWND hDlg);
	void DefaultJoystickPage(HWND hDlg);
	void DefaultSoundPage(HWND hDlg);
	void DefaultMidiPage(HWND hDlg);
	void DefaultSasiPage(HWND hDlg);
	void DefaultEtcPage(HWND hDlg);

	void SetupVersionString();
	void SetupHelpMessage();
	void ShowHelpMessage(HWND hDlg, UINT32 itemid);

	void UpdateVolumeText(HWND hDlg, UINT32 item, SINT32 vol);
	void UpdateFilterSel(HWND hDlg, UINT32 opm, UINT32 adpcm);

	void UpdateJoystickCombo(HWND hDlg);

	void UpdateMouseSpeedStr(HWND hDlg);

	void UpdateMidiPage(HWND hDlg);

	void SetupSasiInfo();
	void SelectSasiFile(HWND hDlg, BOOL is_create);
	void RemoveSasiFile(HWND hDlg);

	void SetupDefaultKeyMap();
	void UpdateKeyMap();
	void LoadKeyMap();
	void SaveKeyMap();
	void ChangeKeyAssign(UINT32 id, UINT32 vk);

	static INT_PTR CALLBACK PropDialogProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
	static INT_PTR CALLBACK KeyConfDialogProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK KeyHookProc(int code, WPARAM wParam, LPARAM lParam);
	static INT_PTR CALLBACK SasiCreateDlgProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam);

	HINSTANCE             m_hInst;
	HWND                  m_hWnd;
	HWND                  m_hKeyList;
	TCHAR                 mIniFile[MAX_PATH];
	TCHAR                 mExeFile[MAX_PATH];
	CString               mVersionString;

	DLGCFG                mDlgCfgBk;

	typedef struct {
		CString           mDevName;
		vector<CString>   mBtnName;
	} JOYSTICK_ITEM;
	vector<JOYSTICK_ITEM> mJoysticks;

	map<UINT32,UINT32>    mMapKeyIdToTable;
	map<UINT32,UINT32>    mMap68KeyToWinKey;
	map<UINT32,UINT32>    mMapFixedKey;
	HHOOK                 m_hKeyConfHook;

	vector<CString>       mMidiDevices;
	vector<CString>       mMidiModules;
	SINT32                mMidiDeviceCount;

	SINT32                mSasiSelected;
	UINT32                mSasiCreateRemain;
	TCHAR                 mSasiCreateName[MAX_PATH];
	UINT_PTR              m_hSasiCreateTimer;
	HANDLE                m_hSasiCreateFile;

	typedef struct {
		UINT32 boxid;
		CString msg;
	} MSG_MAP_ITEM;
	map<UINT32,MSG_MAP_ITEM> mMsgMap;
	UINT32                   mLastMsgId;
	UINT32                   mLastMsgBox;
};

#endif //__win32_settings_h
