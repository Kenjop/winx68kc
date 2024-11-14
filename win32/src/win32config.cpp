/* -----------------------------------------------------------------------------------
  Win32保存される設定類
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "win32config.h"
#include "x68000_driver.h"
#include "resource.h"
#include <mmeapi.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "winmm.lib")


// --------------------------------------------------------------------------
//   バージョン文字列作成
// --------------------------------------------------------------------------
static const TCHAR ADDITIONAL_VERSION_STRING[] =
	_T("This program contains the following codes :\n")
	_T("\n")
	_T("MUSASHI\n")
	_T("  A portable Motorola M680x0 processor emulation engine\n")
	_T("  Version 4.10\n")
	_T("  Copyright 1998-2002 Karl Stenerud.\n")
	_T("\n")
	_T("fmgen\n")
	_T("  FM Sound Generator with OPN/OPM interface\n")
	_T("  Version 008\n")
	_T("  Copyright (C) by cisc 1998, 2003.\n")
	_T("\n")
	_T("MIDI support\n")
	_T("  based on the implementation (for WinX68k/Keropi) by Yui\n")
	;

void CWin32Config::SetupVersionString()
{
	UINT8* pBuf = NULL;

	mVersionString = _T("");

	do {
		UINT32 sz = GetFileVersionInfoSize(mExeFile, NULL);
		if ( !sz ) break;
		pBuf = new UINT8[sz];
		if ( !pBuf ) break;
		BOOL ret = GetFileVersionInfo(mExeFile, NULL, sz, (void*)pBuf);
		if ( !ret ) break;

		VS_FIXEDFILEINFO* pInfo;
		UINT nLen;
		VerQueryValue(pBuf, _T("\\"), (void**)&pInfo, &nLen);
		CString sVersion;
		sVersion.Format(_T("Version %02d.%02d.%02d"), HIWORD(pInfo->dwFileVersionMS), LOWORD(pInfo->dwFileVersionMS), HIWORD(pInfo->dwFileVersionLS), LOWORD(pInfo->dwFileVersionLS));

		WORD* pLang;
		VerQueryValue(pBuf, _T("\\VarFileInfo\\Translation"), (void**)&pLang, &nLen);

		CString subBlock;
		TCHAR* pStr;

		subBlock.Format(_T("\\StringFileInfo\\%04X%04X\\ProductName"), pLang[0], pLang[1]);
		VerQueryValue(pBuf, (LPTSTR)(LPCTSTR)subBlock, (void**)&pStr, &nLen);
		if ( nLen > 0 ) {
			mVersionString += pStr; mVersionString += _T("\n");
		} else {
			mVersionString += _T("WinX68k Compact\n");
		}

		subBlock.Format(_T("\\StringFileInfo\\%04X%04X\\FileDescription"), pLang[0], pLang[1]);
		VerQueryValue(pBuf, (LPTSTR)(LPCTSTR)subBlock, (void**)&pStr, &nLen);
		if ( nLen > 0 ) {
			mVersionString += pStr; mVersionString += _T("\n");
		} else {
			mVersionString += _T("SHARP X68000 emulator\n");
		}

		mVersionString += sVersion;
		mVersionString += _T("\n");

		subBlock.Format(_T("\\StringFileInfo\\%04X%04X\\LegalCopyright"), pLang[0], pLang[1]);
		VerQueryValue(pBuf, (LPTSTR)(LPCTSTR)subBlock, (void**)&pStr, &nLen);
		if ( nLen > 0 ) {
			mVersionString += pStr; mVersionString += _T("\n");
		} else {
			mVersionString += _T("Copyright (C) 2000-24 Kenjo (Kengo Takagi)\n");
		}

		delete[] pBuf;
		return;
	} while ( 0 );

	mVersionString = _T("WinX68k Compact\nSHARP X68000 emulator\nVersion UNKNOWN\nCopyright (C) 2000-24 Kenjo (Kengo Takagi)\n");

	delete[] pBuf;
}


// --------------------------------------------------------------------------
//   設定保存・読込
// --------------------------------------------------------------------------
#define INI_SECTION_NAME  _T("SETTING")

CWin32Config::CWin32Config()
{
	TCHAR buf[MAX_PATH];
	TCHAR* filepart;
	TCHAR* extpart;
	GetModuleFileName(NULL, buf, MAX_PATH);
	GetFullPathName(buf, MAX_PATH, mIniFile, &filepart);
	_tcscpy(mExeFile, mIniFile);
	extpart = _tcsrchr(mIniFile, '\\');
	if ( extpart ) *extpart = 0;
	_tcscat(mIniFile, _T("\\winx68kc.ini"));

	m_hInst = NULL;
	m_hWnd = NULL;
	m_hKeyList = NULL;
}

CWin32Config::~CWin32Config()
{
}

void CWin32Config::Init()
{
	SetupVersionString();
	SetupDefaultKeyMap();
	SetupHelpMessage();
}

void CWin32Config::Load()
{
	LoadItem(_T("SCREEN_SIZE"),     nScreenSize,    0);
	LoadItem(_T("SCREEN_ASPECT"),   nScreenAspect,  D3DDRAW_ASPECT_4_3);
	LoadItem(_T("ZOOM_IGNORE_CRTC"),bZoomIgnoreCrtc,FALSE);
	LoadItem(_T("SCREEN_INTERP"),   bScreenInterp,  TRUE);
	LoadItem(_T("HARDWARE_VSYNC"),  bHardwareVsync, FALSE);
	LoadItem(_T("FULL_SCREEN"),     bFullScreen,    FALSE);

	LoadItem(_T("CPU_CLOCK_IDX"),   nCpuClock,      X68K_CLK_16MHZ);

	LoadItem(_T("RAM_SIZE"),        mDlgCfg.nRamSize,       2);
	LoadItem(_T("RAM_SIZE_UPDATE"), mDlgCfg.bRamSizeUpdate, TRUE);
	LoadItem(_T("FAST_FDD"),        mDlgCfg.bFastFdd,       FALSE);

	LoadItem(_T("VOLUME_OPM"),      mDlgCfg.nVolumeOPM,     0);
	LoadItem(_T("VOLUME_ADPCM"),    mDlgCfg.nVolumeADPCM,   0);
	LoadItem(_T("FILTER_OPM"),      mDlgCfg.nFilterOPM,     1);
	LoadItem(_T("FILTER_ADPCM"),    mDlgCfg.nFilterADPCM,   1);

	LoadItem(_T("JOYSTICK1_DEV"),   mDlgCfg.nJoystickIdx[0],    1);
	LoadItem(_T("JOYSTICK2_DEV"),   mDlgCfg.nJoystickIdx[1],    0);
	LoadItem(_T("JOYSTICK1_BTN1"),  mDlgCfg.nJoystickBtn[0][0], 1);
	LoadItem(_T("JOYSTICK1_BTN2"),  mDlgCfg.nJoystickBtn[0][1], 2);
	LoadItem(_T("JOYSTICK2_BTN1"),  mDlgCfg.nJoystickBtn[1][0], 1);
	LoadItem(_T("JOYSTICK2_BTN2"),  mDlgCfg.nJoystickBtn[1][1], 2);
	LoadItem(_T("JOYKEY1_BTN1"),    mDlgCfg.nJoyKeyBtn[0][0],   1);
	LoadItem(_T("JOYKEY1_BTN2"),    mDlgCfg.nJoyKeyBtn[0][1],   2);
	LoadItem(_T("JOYKEY2_BTN1"),    mDlgCfg.nJoyKeyBtn[1][0],   1);
	LoadItem(_T("JOYKEY2_BTN2"),    mDlgCfg.nJoyKeyBtn[1][1],   2);

	LoadItem(_T("MOUSE_SPEED"),     mDlgCfg.nMouseSpeed, 75);

	LoadItem(_T("MIDI_DEVICE_ID"),  mDlgCfg.nMidiDeviceId,   -1);
	LoadItem(_T("MIDI_MODULE_TYPE"),mDlgCfg.nMidiModuleType, 1);
	LoadItem(_T("MIDI_SEND_RESET"), mDlgCfg.bMidiSendReset,  TRUE);

	LoadItem(_T("SHOW_STATBAR"),    mDlgCfg.bShowStatBar,   TRUE);
	LoadItem(_T("SHOW_FS_STAT"),    mDlgCfg.bShowFsStat,    TRUE);
	LoadItem(_T("TITLE_BAR_INFO"),  mDlgCfg.nTitleBarInfo,  1);

	for (UINT32 i=0; i<16; i++) {
		CString name;
		name.Format(_T("SASI%d"), i);
		LoadItem(name.GetBuffer(), mDlgCfg.sSasiFile[i], _T(""));
	}
	LoadItem(_T("SASI_SRAM_UPD"),  mDlgCfg.bSasiSramUpd, TRUE);

	LoadKeyMap();
	SetupSasiInfo();

	mDlgCfg.nRamSize = NUMLIMIT(mDlgCfg.nRamSize, 2, 12) & ~1;
}

void CWin32Config::Save()
{
	SaveItem(_T("SCREEN_SIZE"),     nScreenSize);
	SaveItem(_T("SCREEN_ASPECT"),   nScreenAspect);
	SaveItem(_T("ZOOM_IGNORE_CRTC"),bZoomIgnoreCrtc);
	SaveItem(_T("SCREEN_INTERP"),   bScreenInterp);
	SaveItem(_T("HARDWARE_VSYNC"),  bHardwareVsync);
	SaveItem(_T("FULL_SCREEN"),     bFullScreen);

	SaveItem(_T("CPU_CLOCK_IDX"),   nCpuClock);

	SaveItem(_T("RAM_SIZE"),        mDlgCfg.nRamSize);
	SaveItem(_T("RAM_SIZE_UPDATE"), mDlgCfg.bRamSizeUpdate);
	SaveItem(_T("FAST_FDD"),        mDlgCfg.bFastFdd);

	SaveItem(_T("VOLUME_OPM"),      mDlgCfg.nVolumeOPM);
	SaveItem(_T("VOLUME_ADPCM"),    mDlgCfg.nVolumeADPCM);
	SaveItem(_T("FILTER_OPM"),      mDlgCfg.nFilterOPM);
	SaveItem(_T("FILTER_ADPCM"),    mDlgCfg.nFilterADPCM);

	SaveItem(_T("JOYSTICK1_DEV"),   mDlgCfg.nJoystickIdx[0]);
	SaveItem(_T("JOYSTICK2_DEV"),   mDlgCfg.nJoystickIdx[1]);
	SaveItem(_T("JOYSTICK1_BTN1"),  mDlgCfg.nJoystickBtn[0][0]);
	SaveItem(_T("JOYSTICK1_BTN2"),  mDlgCfg.nJoystickBtn[0][1]);
	SaveItem(_T("JOYSTICK2_BTN1"),  mDlgCfg.nJoystickBtn[1][0]);
	SaveItem(_T("JOYSTICK2_BTN2"),  mDlgCfg.nJoystickBtn[1][1]);
	SaveItem(_T("JOYKEY1_BTN1"),    mDlgCfg.nJoyKeyBtn[0][0]);
	SaveItem(_T("JOYKEY1_BTN2"),    mDlgCfg.nJoyKeyBtn[0][1]);
	SaveItem(_T("JOYKEY2_BTN1"),    mDlgCfg.nJoyKeyBtn[1][0]);
	SaveItem(_T("JOYKEY2_BTN2"),    mDlgCfg.nJoyKeyBtn[1][1]);

	SaveItem(_T("MOUSE_SPEED"),     mDlgCfg.nMouseSpeed);

	SaveItem(_T("MIDI_DEVICE_ID"),  mDlgCfg.nMidiDeviceId);
	SaveItem(_T("MIDI_MODULE_TYPE"),mDlgCfg.nMidiModuleType);
	SaveItem(_T("MIDI_SEND_RESET"), mDlgCfg.bMidiSendReset);

	SaveItem(_T("SHOW_STATBAR"),    mDlgCfg.bShowStatBar);
	SaveItem(_T("SHOW_FS_STAT"),    mDlgCfg.bShowFsStat);
	SaveItem(_T("TITLE_BAR_INFO"),  mDlgCfg.nTitleBarInfo);

	for (UINT32 i=0; i<16; i++) {
		CString name;
		name.Format(_T("SASI%d"), i);
		SaveItem(name.GetBuffer(), mDlgCfg.sSasiFile[i]);
	}
	SaveItem(_T("SASI_SRAM_UPD"),  mDlgCfg.bSasiSramUpd);

	SaveKeyMap();
}


void CWin32Config::LoadItem(const TCHAR* name, TCHAR* str, const TCHAR* def)
{
	GetPrivateProfileString(INI_SECTION_NAME, name, def, str, MAX_PATH, mIniFile);
}

void CWin32Config::LoadItem(const TCHAR* name, UINT32& num, const UINT32 def)
{
	num = GetPrivateProfileInt(INI_SECTION_NAME, name, def, mIniFile);
}

void CWin32Config::LoadItem(const TCHAR* name, SINT32& num, const SINT32 def)
{
	num = GetPrivateProfileInt(INI_SECTION_NAME, name, def, mIniFile);
}

void CWin32Config::SaveItem(const TCHAR* name, const TCHAR* str)
{
	WritePrivateProfileString(INI_SECTION_NAME, name, str, mIniFile);
}

void CWin32Config::SaveItem(const TCHAR* name, UINT32 num)
{
	CString s;
	s.Format(_T("%d"), num);
	WritePrivateProfileString(INI_SECTION_NAME, name, s.GetBuffer(), mIniFile);
}

void CWin32Config::SaveItem(const TCHAR* name, SINT32 num)
{
	CString s;
	s.Format(_T("%d"), num);
	WritePrivateProfileString(INI_SECTION_NAME, name, s.GetBuffer(), mIniFile);
}


// --------------------------------------------------------------------------
//   設定ダイアログ関連
// --------------------------------------------------------------------------

// -----------------------------
// システム設定
// -----------------------------
static const TCHAR* RAM_SIZE_STR[] = {
	_T("---"), _T("2Mb"), _T("4Mb"), _T("6Mb"), _T("8Mb"), _T("10Mb"), _T("12Mb")
};

void CWin32Config::SetupSystemPage(HWND hDlg)
{
	SendDlgItemMessage(hDlg, IDC_COMBO_RAM_SIZE, CB_RESETCONTENT, 0, 0);
	for (UINT32 i=2; i<=12; i+=2) {
		SendDlgItemMessage(hDlg, IDC_COMBO_RAM_SIZE, CB_ADDSTRING, 0, (LPARAM)RAM_SIZE_STR[i/2]);
	}
	SendDlgItemMessage(hDlg, IDC_COMBO_RAM_SIZE, CB_SELECTSTRING, 0, (LPARAM)RAM_SIZE_STR[mDlgCfg.nRamSize/2]);
	CheckDlgButton(hDlg, IDC_CHECK_RAM_SIZE_UPDATE, ( mDlgCfg.bRamSizeUpdate ) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECK_FAST_FDD, ( mDlgCfg.bFastFdd ) ? BST_CHECKED : BST_UNCHECKED);
}

void CWin32Config::DefaultSystemPage(HWND hDlg)
{
	mDlgCfg.nRamSize = 2;
	mDlgCfg.bRamSizeUpdate = TRUE;
	mDlgCfg.bFastFdd = FALSE;
	SetupSystemPage(hDlg);
}


// -----------------------------
// サウンド設定
// -----------------------------
#define VOLUME_MAX_DB10  100
#define VOLUME_MIN_DB10  -240

void CWin32Config::SetupSoundPage(HWND hDlg)
{
	SendDlgItemMessage(hDlg, IDC_SLIDER_VOLUME_OPM,   TBM_SETRANGE, TRUE, MAKELONG(VOLUME_MIN_DB10, VOLUME_MAX_DB10));
	SendDlgItemMessage(hDlg, IDC_SLIDER_VOLUME_OPM,   TBM_SETPOS, TRUE, mDlgCfg.nVolumeOPM);
	UpdateVolumeText(hDlg, IDC_STATIC_VOLUME_OPM, mDlgCfg.nVolumeOPM);
	SendDlgItemMessage(hDlg, IDC_SLIDER_VOLUME_ADPCM, TBM_SETRANGE, TRUE, MAKELONG(VOLUME_MIN_DB10, VOLUME_MAX_DB10));
	SendDlgItemMessage(hDlg, IDC_SLIDER_VOLUME_ADPCM, TBM_SETPOS, TRUE, mDlgCfg.nVolumeADPCM);
	UpdateVolumeText(hDlg, IDC_STATIC_VOLUME_ADPCM, mDlgCfg.nVolumeADPCM);
	UpdateFilterSel(hDlg, mDlgCfg.nFilterOPM, mDlgCfg.nFilterADPCM);
}

void CWin32Config::DefaultSoundPage(HWND hDlg)
{
	mDlgCfg.nVolumeOPM = 0;
	mDlgCfg.nVolumeADPCM = 0;
	mDlgCfg.nFilterOPM = 1;
	mDlgCfg.nFilterADPCM = 1;
	SendDlgItemMessage(hDlg, IDC_SLIDER_VOLUME_OPM,   TBM_SETPOS, TRUE, mDlgCfg.nVolumeOPM);
	UpdateVolumeText(hDlg, IDC_STATIC_VOLUME_OPM, mDlgCfg.nVolumeOPM);
	SendDlgItemMessage(hDlg, IDC_SLIDER_VOLUME_ADPCM, TBM_SETPOS, TRUE, mDlgCfg.nVolumeADPCM);
	UpdateVolumeText(hDlg, IDC_STATIC_VOLUME_ADPCM, mDlgCfg.nVolumeADPCM);
	UpdateFilterSel(hDlg, mDlgCfg.nFilterOPM, mDlgCfg.nFilterADPCM);
}

void CWin32Config::UpdateVolumeText(HWND hDlg, UINT32 item, SINT32 vol)
{
	CString s;
	s.Format(_T("%.01f db"), (float)vol/10.0f);
	SetDlgItemText(hDlg, item, s.GetBuffer());
}

void CWin32Config::UpdateFilterSel(HWND hDlg, UINT32 opm, UINT32 adpcm)
{
	CheckDlgButton(hDlg, IDC_RADIO_OPM_FILTER0,  (opm==0)   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_RADIO_OPM_FILTER1,  (opm==1)   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_RADIO_ADPCM_FILTER0,(adpcm==0) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_RADIO_ADPCM_FILTER1,(adpcm==1) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_RADIO_ADPCM_FILTER2,(adpcm==2) ? BST_CHECKED : BST_UNCHECKED);
}


// -----------------------------
// ジョイスティック設定
// -----------------------------
void CWin32Config::SetupJoystickPage(HWND hDlg)
{
	JOYSTICK_ITEM item;

	mJoysticks.clear();

	item.mDevName = _T("割り当てなし");
	item.mBtnName.clear();
	mJoysticks.push_back(item);

	item.mDevName = _T("JoyKey");
	item.mBtnName.clear();
	item.mBtnName.push_back(_T("なし"));
	item.mBtnName.push_back(_T("Z キー"));
	item.mBtnName.push_back(_T("X キー"));
	mJoysticks.push_back(item);

	UINT32 num = joyGetNumDevs();
	for (UINT32 i=0; i<num; i++) {
		JOYCAPS caps;
		if ( joyGetDevCaps(i, &caps, sizeof(caps)) == JOYERR_NOERROR ) {
			item.mDevName = caps.szPname;
			item.mBtnName.clear();
			item.mBtnName.push_back(_T("なし"));
			for (UINT32 btn=0; btn<caps.wNumButtons; btn++) {
				// ジョイスティックAPIだとボタン名とかは取れない DInputにしろって？ それはそう
				CString s;
				s.Format(_T("ボタン %d"), btn+1);
				item.mBtnName.push_back(s);
			}
			mJoysticks.push_back(item);
		}
	}
	UpdateJoystickCombo(hDlg);
}

void CWin32Config::DefaultJoystickPage(HWND hDlg)
{
	mDlgCfg.nJoystickIdx[0]    = 1;  // #1のデフォルトはJoyKeyにしとく
	mDlgCfg.nJoystickIdx[1]    = 0;
	mDlgCfg.nJoystickBtn[0][0] = 1;
	mDlgCfg.nJoystickBtn[0][1] = 2;
	mDlgCfg.nJoystickBtn[1][0] = 1;
	mDlgCfg.nJoystickBtn[1][1] = 2;
	mDlgCfg.nJoyKeyBtn[0][0]   = 1;
	mDlgCfg.nJoyKeyBtn[0][1]   = 2;
	mDlgCfg.nJoyKeyBtn[1][0]   = 1;
	mDlgCfg.nJoyKeyBtn[1][1]   = 2;
	UpdateJoystickCombo(hDlg);
}

void CWin32Config::UpdateJoystickCombo(HWND hDlg)
{
	SendDlgItemMessage(hDlg, IDC_COMBO_JOY1_DEVICE, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessage(hDlg, IDC_COMBO_JOY2_DEVICE, CB_RESETCONTENT, 0, 0);

	for (UINT32 i=0; i<mJoysticks.size(); i++) {
		SendDlgItemMessage(hDlg, IDC_COMBO_JOY1_DEVICE, CB_ADDSTRING, 0, (LPARAM)mJoysticks[i].mDevName.GetString());
		SendDlgItemMessage(hDlg, IDC_COMBO_JOY2_DEVICE, CB_ADDSTRING, 0, (LPARAM)mJoysticks[i].mDevName.GetString());
	}

	UINT32 dev[2];
	dev[0] = mDlgCfg.nJoystickIdx[0];
	dev[1] = mDlgCfg.nJoystickIdx[1];
	if ( dev[0] >= mJoysticks.size() ) dev[0] = 0;
	if ( dev[1] >= mJoysticks.size() ) dev[1] = 0;
	SendDlgItemMessage(hDlg, IDC_COMBO_JOY1_DEVICE, CB_SELECTSTRING, 0, (LPARAM)mJoysticks[dev[0]].mDevName.GetString());
	SendDlgItemMessage(hDlg, IDC_COMBO_JOY2_DEVICE, CB_SELECTSTRING, 0, (LPARAM)mJoysticks[dev[1]].mDevName.GetString());

	for (UINT32 idx=0; idx<2; idx++) {
		UINT32 id = ( idx == 0 ) ? IDC_COMBO_JOY1_BUTTON_A : IDC_COMBO_JOY2_BUTTON_A;

		SendDlgItemMessage(hDlg, id+0, CB_RESETCONTENT, 0, 0);
		SendDlgItemMessage(hDlg, id+1, CB_RESETCONTENT, 0, 0);

		if ( dev[idx]==0 ) {
			EnableWindow(GetDlgItem(hDlg, id+0), FALSE);
			EnableWindow(GetDlgItem(hDlg, id+1), FALSE);
		} else {
			EnableWindow(GetDlgItem(hDlg, id+0), TRUE);
			EnableWindow(GetDlgItem(hDlg, id+1), TRUE);

			for (UINT32 i=0; i<mJoysticks[dev[idx]].mBtnName.size(); i++) {
				SendDlgItemMessage(hDlg, id+0, CB_ADDSTRING, 0, (LPARAM)mJoysticks[dev[idx]].mBtnName[i].GetString());
				SendDlgItemMessage(hDlg, id+1, CB_ADDSTRING, 0, (LPARAM)mJoysticks[dev[idx]].mBtnName[i].GetString());
			}

			UINT32 btn[2];
			if ( dev[idx]==1 ) {
				btn[0] = mDlgCfg.nJoyKeyBtn[idx][0];
				btn[1] = mDlgCfg.nJoyKeyBtn[idx][1];
			} else {
				btn[0] = mDlgCfg.nJoystickBtn[idx][0];
				btn[1] = mDlgCfg.nJoystickBtn[idx][1];
			}
			if ( btn[0] >= mJoysticks[dev[0]].mBtnName.size() ) btn[0] = 0;
			if ( btn[1] >= mJoysticks[dev[0]].mBtnName.size() ) btn[1] = 0;
			SendDlgItemMessage(hDlg, id+0, CB_SELECTSTRING, 0, (LPARAM)mJoysticks[dev[idx]].mBtnName[btn[0]].GetString());
			SendDlgItemMessage(hDlg, id+1, CB_SELECTSTRING, 0, (LPARAM)mJoysticks[dev[idx]].mBtnName[btn[1]].GetString());
		}
	}
}


// -----------------------------
// キーボード設定
// -----------------------------
typedef struct {
	UINT32  id;
	UINT32  x68code;
	const TCHAR* name;
	UINT32  vk;
} CONFIGURABLE_KEY_ITEM;

static const CONFIGURABLE_KEY_ITEM CONF_KEYS[] = {
	// 設定可能キー群
	{ IDC_KEY_BREAK,    X68K_KEY_BREAK,    _T("BREAK"),           0                      },  // Z : VK_ESCAPE
	{ IDC_KEY_COPY,     X68K_KEY_COPY,     _T("COPY"),            VK_APPS        | 0x100 },  // Z : VK_APPS | 0x100
	{ IDC_KEY_ESC,      X68K_KEY_ESC,      _T("ESC"),             VK_ESCAPE              },  // Z : 0xF3/0xF4 交互
	{ IDC_KEY_TAB,      X68K_KEY_TAB,      _T("TAB"),             VK_TAB                 },
	{ IDC_KEY_CTRL,     X68K_KEY_CTRL,     _T("CTR_T("),            VK_CONTROL             },
	{ IDC_KEY_SHIFTL,   X68K_KEY_SHIFT,    _T("SHIFT"),           VK_SHIFT               },
	{ IDC_KEY_SHIFTR,   X68K_KEY_SHIFT,    _T("SHIFT"),           VK_SHIFT               },
	{ IDC_KEY_HIRAGANA, X68K_KEY_HIRAGANA, _T("ひらがな"),        VK_LWIN        | 0x100 },  // Z : VK_LWIN | 0x100
	{ IDC_KEY_XF1,      X68K_KEY_XF1,      _T("XF1"),             VK_MENU                },  // Z : VK_MENU (ALT)
	{ IDC_KEY_XF2,      X68K_KEY_XF2,      _T("XF2"),             VK_NONCONVERT          },  // Z : VK_NONCONVERT
	{ IDC_KEY_XF3,      X68K_KEY_XF3,      _T("XF3"),             VK_CONVERT             },  // Z : VK_CONVERT
	{ IDC_KEY_XF4,      X68K_KEY_XF4,      _T("XF4"),             0xF2                   },  // Z : 0xF2
	{ IDC_KEY_XF5,      X68K_KEY_XF5,      _T("XF5"),             VK_MENU        | 0x100 },  // Z : VK_MENU | 0x100 (右ALT)
	{ IDC_KEY_ZENKAKU,  X68K_KEY_ZENKAKU,  _T("全角"),            VK_RWIN        | 0x100 },  // Z : VK_RWIN | 0x100
	{ IDC_KEY_BS,       X68K_KEY_BS,       _T("BS"),              VK_BACK                },
	{ IDC_KEY_KANA,     X68K_KEY_KANA,     _T("かな"),            VK_SNAPSHOT            },  // Z : VK_SNAPSHOT
	{ IDC_KEY_ROMAJI,   X68K_KEY_ROMAJI,   _T("ローマ字"),        VK_SCROLL              },  // Z : VK_SCROLL (ScrollLock)
	{ IDC_KEY_CODEIN,   X68K_KEY_CODEIN,   _T("コード入力"),      VK_PAUSE               },  // Z : VK_PAUSE
	{ IDC_KEY_CAPS,     X68K_KEY_CAPS,     _T("CAPS"),            VK_CAPITAL             },  // Z : 0xF0
	{ IDC_KEY_KIGOU,    X68K_KEY_KIGOU,    _T("記号入力"),        VK_VOLUME_MUTE | 0x100 },  // Z : VK_VOLUME_MUTE | 0x100
	{ IDC_KEY_TOUROKU,  X68K_KEY_TOUROKU,  _T("登録"),            VK_VOLUME_DOWN | 0x100 },  // Z : VK_VOLUME_DOWN | 0x100
	{ IDC_KEY_HELP,     X68K_KEY_HELP,     _T("HELP"),            VK_VOLUME_UP   | 0x100 },  // Z : VK_VOLUME_UP   | 0x100
	{ IDC_KEY_HOME,     X68K_KEY_HOME,     _T("HOME"),            VK_HOME        | 0x100 },
	{ IDC_KEY_INS,      X68K_KEY_INS,      _T("INS"),             VK_INSERT      | 0x100 },
	{ IDC_KEY_DEL,      X68K_KEY_DEL,      _T("DE_T("),             VK_DELETE      | 0x100 },
	{ IDC_10KEY_CLR,    X68K_KEY_CLR,      _T("CLR"),             VK_NUMLOCK     | 0x100 },  // Z : VK_NUMLOCK     | 0x100
	{ IDC_KEY_ROLLUP,   X68K_KEY_ROLLUP,   _T("ROLLUP"),          VK_PRIOR       | 0x100 },
	{ IDC_KEY_ROLLDOWN, X68K_KEY_ROLLDOWN, _T("ROLLDOWN"),        VK_NEXT        | 0x100 },
	{ IDC_KEY_UNDO,     X68K_KEY_UNDO,     _T("UNDO"),            VK_END         | 0x100 },
	{ IDC_TENKEY_SLASH, X68K_KEY_NUMDIV,   _T("テンキー /"),      VK_DIVIDE      | 0x100 },
	{ IDC_TENKEY_MULTI, X68K_KEY_NUMMUL,   _T("テンキー *"),      VK_MULTIPLY            },
	{ IDC_TENKEY_MINUS, X68K_KEY_NUMMINUS, _T("テンキー -"),      VK_SUBTRACT            },
	{ IDC_TENKEY_7,     X68K_KEY_NUM7,     _T("テンキー 7"),      VK_NUMPAD7             },
	{ IDC_TENKEY_8,     X68K_KEY_NUM8,     _T("テンキー 8"),      VK_NUMPAD8             },
	{ IDC_TENKEY_9,     X68K_KEY_NUM9,     _T("テンキー 9"),      VK_NUMPAD9             },
	{ IDC_TENKEY_PLUS,  X68K_KEY_NUMPLUS,  _T("テンキー +"),      VK_ADD                 },
	{ IDC_TENKEY_4,     X68K_KEY_NUM4,     _T("テンキー 4"),      VK_NUMPAD4             },
	{ IDC_TENKEY_5,     X68K_KEY_NUM5,     _T("テンキー 5"),      VK_NUMPAD5             },
	{ IDC_TENKEY_6,     X68K_KEY_NUM6,     _T("テンキー 6"),      VK_NUMPAD6             },
	{ IDC_TENKEY_EQUAL, X68K_KEY_NUMEQUAL, _T("テンキー ="),      VK_CLEAR               },  // Z : VK_CLEAR
	{ IDC_TENKEY_1,     X68K_KEY_NUM1,     _T("テンキー 1"),      VK_NUMPAD1             },
	{ IDC_TENKEY_2,     X68K_KEY_NUM2,     _T("テンキー 2"),      VK_NUMPAD2             },
	{ IDC_TENKEY_3,     X68K_KEY_NUM3,     _T("テンキー 3"),      VK_NUMPAD3             },
	{ IDC_TENKEY_0,     X68K_KEY_NUM0,     _T("テンキー 0"),      VK_NUMPAD0             },
	{ IDC_TENKEY_COMMA, X68K_KEY_NUMCOMMA, _T("テンキー ,"),      0xC2                   },  // Z : 0xC2
	{ IDC_TENKEY_PERIOD,X68K_KEY_NUMPERIOD,_T("テンキー ."),      VK_DECIMAL             },
	{ IDC_TENKEY_ENTER, X68K_KEY_NUMENTER, _T("テンキー ENTER"),  VK_RETURN      | 0x100 },
	{ IDC_KEY_LEFT,     X68K_KEY_LEFT,     _T("カーソルキー ←"), VK_LEFT        | 0x100 },
	{ IDC_KEY_UP,       X68K_KEY_UP,       _T("カーソルキー ↑"), VK_UP          | 0x100 },
	{ IDC_KEY_DOWN,     X68K_KEY_DOWN,     _T("カーソルキー ↓"), VK_DOWN        | 0x100 },
	{ IDC_KEY_RIGHT,    X68K_KEY_RIGHT,    _T("カーソルキー →"), VK_RIGHT       | 0x100 },
	{ IDC_KEY_OPT1,     X68K_KEY_OPT1,     _T("OPT1"),            VK_F11                 },  // Z : VK_F11
	{ IDC_KEY_OPT2,     X68K_KEY_OPT2,     _T("OPT2"),            VK_F12                 },  // Z : VK_F12
	{ IDC_KEY_UNDERBAR, X68K_KEY_BACKSLASH,_T("_ ろ"),            VK_OEM_102             },
	{ IDC_KEY_YEN,      X68K_KEY_YEN,      _T("\\ | _"),          VK_OEM_5               },
	// 以下は固定
	{ 0,                X68K_KEY_ENTER,    _T("ENTER"),           VK_RETURN              },
	{ 0,                X68K_KEY_SPACE,    _T("スペースバー"),    VK_SPACE               },
	{ 0,                X68K_KEY_F1,       _T("F1"),              VK_F1                  },
	{ 0,                X68K_KEY_F2,       _T("F2"),              VK_F2                  },
	{ 0,                X68K_KEY_F3,       _T("F3"),              VK_F3                  },
	{ 0,                X68K_KEY_F4,       _T("F4"),              VK_F4                  },
	{ 0,                X68K_KEY_F5,       _T("F5"),              VK_F5                  },
	{ 0,                X68K_KEY_F6,       _T("F6"),              VK_F6                  },
	{ 0,                X68K_KEY_F7,       _T("F7"),              VK_F7                  },
	{ 0,                X68K_KEY_F8,       _T("F8"),              VK_F8                  },
	{ 0,                X68K_KEY_F9,       _T("F9"),              VK_F9                  },
	{ 0,                X68K_KEY_F10,      _T("F10"),             VK_F10                 },
	{ 0,                X68K_KEY_0,        _T("0"),               '0'                    }, // '0'
	{ 0,                X68K_KEY_1,        _T("1"),               '1'                    }, // '1'
	{ 0,                X68K_KEY_2,        _T("2"),               '2'                    }, // '2'
	{ 0,                X68K_KEY_3,        _T("3"),               '3'                    }, // '3'
	{ 0,                X68K_KEY_4,        _T("4"),               '4'                    }, // '4'
	{ 0,                X68K_KEY_5,        _T("5"),               '5'                    }, // '5'
	{ 0,                X68K_KEY_6,        _T("6"),               '6'                    }, // '6'
	{ 0,                X68K_KEY_7,        _T("7"),               '7'                    }, // '7'
	{ 0,                X68K_KEY_8,        _T("8"),               '8'                    }, // '8'
	{ 0,                X68K_KEY_9,        _T("9"),               '9'                    }, // '9'
	{ 0,                X68K_KEY_A,        _T("A"),               'A'                    }, // 'A'
	{ 0,                X68K_KEY_B,        _T("B"),               'B'                    }, // 'B'
	{ 0,                X68K_KEY_C,        _T("C"),               'C'                    }, // 'C'
	{ 0,                X68K_KEY_D,        _T("D"),               'D'                    }, // 'D'
	{ 0,                X68K_KEY_E,        _T("E"),               'E'                    }, // 'E'
	{ 0,                X68K_KEY_F,        _T("F"),               'F'                    }, // 'F'
	{ 0,                X68K_KEY_G,        _T("G"),               'G'                    }, // 'G'
	{ 0,                X68K_KEY_H,        _T("H"),               'H'                    }, // 'H'
	{ 0,                X68K_KEY_I,        _T("I"),               'I'                    }, // 'I'
	{ 0,                X68K_KEY_J,        _T("J"),               'J'                    }, // 'J'
	{ 0,                X68K_KEY_K,        _T("K"),               'K'                    }, // 'K'
	{ 0,                X68K_KEY_L,        _T("_T("),               'L'                    }, // 'L'
	{ 0,                X68K_KEY_M,        _T("M"),               'M'                    }, // 'M'
	{ 0,                X68K_KEY_N,        _T("N"),               'N'                    }, // 'N'
	{ 0,                X68K_KEY_O,        _T("O"),               'O'                    }, // 'O'
	{ 0,                X68K_KEY_P,        _T("P"),               'P'                    }, // 'P'
	{ 0,                X68K_KEY_Q,        _T("Q"),               'Q'                    }, // 'Q'
	{ 0,                X68K_KEY_R,        _T("R"),               'R'                    }, // 'R'
	{ 0,                X68K_KEY_S,        _T("S"),               'S'                    }, // 'S'
	{ 0,                X68K_KEY_T,        _T("T"),               'T'                    }, // 'T'
	{ 0,                X68K_KEY_U,        _T("U"),               'U'                    }, // 'U'
	{ 0,                X68K_KEY_V,        _T("V"),               'V'                    }, // 'V'
	{ 0,                X68K_KEY_W,        _T("W"),               'W'                    }, // 'W'
	{ 0,                X68K_KEY_X,        _T("X"),               'X'                    }, // 'X'
	{ 0,                X68K_KEY_Y,        _T("Y"),               'Y'                    }, // 'Y'
	{ 0,                X68K_KEY_Z,        _T("Z"),               'Z'                    }, // 'Z'
	{ 0,                X68K_KEY_MINUS,    _T("-"),               VK_OEM_MINUS           }, // -
	{ 0,                X68K_KEY_EXP,      _T("^"),               VK_OEM_7               }, //  ^
	{ 0,                X68K_KEY_AT,       _T("@"),               VK_OEM_3               }, //  @
	{ 0,                X68K_KEY_OPENSB,   _T("["),               VK_OEM_4,              }, //  [
	{ 0,                X68K_KEY_SEMICOLON,_T(";"),               VK_OEM_PLUS            }, //  ;
	{ 0,                X68K_KEY_COLON,    _T(":"),               VK_OEM_1               }, //  :
	{ 0,                X68K_KEY_CLOSESB,  _T("]"),               VK_OEM_6               }, //  ]
	{ 0,                X68K_KEY_COMMA,    _T("),"),               VK_OEM_COMMA           }, //  ,
	{ 0,                X68K_KEY_PERIOD,   _T("."),               VK_OEM_PERIOD          }, //  .
	{ 0,                X68K_KEY_SLASH,    _T("/"),               VK_OEM_2               }, //  /
};

void CWin32Config::SetupKeyboardPage(HWND hDlg)
{
}

void CWin32Config::DefaultKeyboardPage(HWND hDlg)
{
	SetupDefaultKeyMap();
}

void CWin32Config::SetupDefaultKeyMap()
{
	mMapKeyIdToTable.clear();
	mMapFixedKey.clear();
	memset(mDlgCfg.nKeyMap, 0, sizeof(mDlgCfg.nKeyMap));

	for (UINT32 i=0; i<(sizeof(CONF_KEYS)/sizeof(CONF_KEYS[0])); i++) {
		UINT32 id = CONF_KEYS[i].id;
		UINT32 vk = CONF_KEYS[i].vk;
		if ( id ) {
			mMapKeyIdToTable[id] = i;
		}
		if ( vk ) {
			mDlgCfg.nKeyMap[vk] = (UINT8)CONF_KEYS[i].x68code;
		}
		if ( !id && vk ) {
			mMapFixedKey[vk] = i;
		}
	}
	UpdateKeyMap();
}

void CWin32Config::UpdateKeyMap()
{
	mMap68KeyToWinKey.clear();
	for (UINT32 vk=0; vk<0x200; vk++) {
		UINT32 x68code = mDlgCfg.nKeyMap[vk];
		if ( x68code ) {
			mMap68KeyToWinKey[x68code] = vk;
		}
	}
}

void CWin32Config::LoadKeyMap()
{
	memset(mDlgCfg.nKeyMap, 0, sizeof(mDlgCfg.nKeyMap));

	for (UINT32 i=0; i<(sizeof(CONF_KEYS)/sizeof(CONF_KEYS[0])); i++) {
		UINT32 id = CONF_KEYS[i].id;
		if ( id ) {
			UINT32 x68code = CONF_KEYS[i].x68code;
			UINT32 vk = 0;
			CString s;
			s.Format(_T("KEY_%02X"), x68code);
			LoadItem(s.GetBuffer(), vk, CONF_KEYS[i].vk);
			if ( vk ) {
				mDlgCfg.nKeyMap[vk] = (UINT8)x68code;
			}
		} else {
			// 固定部はそのまま登録
			UINT32 x68code = CONF_KEYS[i].x68code;
			UINT32 vk = CONF_KEYS[i].vk;
			mDlgCfg.nKeyMap[vk] = (UINT8)x68code;
		}
	}
	UpdateKeyMap();
}

void CWin32Config::SaveKeyMap()
{
	for (UINT32 i=0; i<(sizeof(CONF_KEYS)/sizeof(CONF_KEYS[0])); i++) {
		UINT32 id = CONF_KEYS[i].id;
		if ( id ) {
			UINT32 x68code = CONF_KEYS[i].x68code;
			UINT32 vk = mMap68KeyToWinKey[x68code];
			CString s;
			s.Format(_T("KEY_%02X"), x68code);
			SaveItem(s.GetBuffer(), vk);
		}
	}
}

void CWin32Config::ChangeKeyAssign(UINT32 id, UINT32 vk)
{
	UINT32 idx = mMapKeyIdToTable[id];
	UINT32 x68code = CONF_KEYS[idx].x68code;
	// まず現在のVKの割り当てを外す
	UINT32 oldvk = mMap68KeyToWinKey[x68code];
	if ( oldvk ) {
		mDlgCfg.nKeyMap[oldvk] = 0;
	}
	if ( vk ) {
		// 新しいVKに割り当てる
		UINT32 old68 = mDlgCfg.nKeyMap[vk];
		if ( old68 ) {
			// このキーが既に他に割り当て済みの場合、割り当て解除
			// XXX ここはメッセージ欲しいかもしれない
			mMap68KeyToWinKey[old68] = 0;
		}
		mMap68KeyToWinKey[x68code] = vk;
		mDlgCfg.nKeyMap[vk] = (UINT8)x68code;
	} else {
		// 現在の割り当てを解除
		mMap68KeyToWinKey[x68code] = 0;
	}
	// カーソルが動いてなくてもメッセージ更新されるようにする
	mLastMsgId = 0;
}

LRESULT CALLBACK CWin32Config::KeyHookProc(int code, WPARAM wParam, LPARAM lParam)
{
	// ダイアログはそのままでは WM_KEYDOWN を拾えない（ダイアログアイテム側に吸われる）ので、
	// メッセージフックしてアイテムの親＝ダイアログに WM_KEYDOWN を流す
	if ( code > -1 ) {
		if ( !(lParam & 0xC0000000) ) {  // on KEYDOWN
			HWND wnd = GetParent(GetFocus());
			SendMessage(wnd, WM_KEYDOWN, wParam, lParam);
			return TRUE;
        }
	}
    return CallNextHookEx(NULL, code, wParam, lParam);
}

INT_PTR CALLBACK CWin32Config::KeyConfDialogProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch ( Msg )
	{
	case WM_INITDIALOG:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(lParam);
		SetWindowLongPtr(hDlg, GWL_USERDATA, (LONG_PTR)lParam);
		// キーメッセージをフックする
		_this->m_hKeyConfHook = SetWindowsHookEx(WH_KEYBOARD, &KeyHookProc, NULL, GetCurrentThreadId());
		// 初期メッセージ設定
		SetDlgItemText(hDlg, IDC_STATIC_KEYCONF, _T("割り当てたいキーを押してください。\n\n「割り当て解除」ボタンを押すと、割り当てなしの状態となります。"));
	}
		break;

	case WM_CLOSE:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(GetWindowLongPtr(hDlg, GWL_USERDATA));
		EndDialog(hDlg, -1);
	}
		break;

	case WM_COMMAND:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(GetWindowLongPtr(hDlg, GWL_USERDATA));
		switch ( LOWORD(wParam) )
		{
		case IDCANCEL:
			EndDialog(hDlg, -1);  // -1 はキャンセル
	        break;
		case IDC_BUTTON_NO_ASSIGN:
			EndDialog(hDlg, 0);   // 0 は解除
	        break;
		}
	}
        break;

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(GetWindowLongPtr(hDlg, GWL_USERDATA));
		UINT32 result = (wParam&0xFF) | ((lParam>>16)&0x100);
		// 固定キーを設定できないようにする
		UINT32 idx = _this->mMapFixedKey[result];
		if ( !idx ) {
			EndDialog(hDlg, result);
		} else {
			CString msg;
			msg.Format(_T("「%s」キーは固定された割り当てのキーです。\n他のキーを使用してください。"), CONF_KEYS[idx].name);
			SetDlgItemText(hDlg, IDC_STATIC_KEYCONF, msg.GetBuffer());
			
		}
	}
        break;

    case WM_DESTROY:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(GetWindowLongPtr(hDlg, GWL_USERDATA));
		// キーメッセージフックを解除
		UnhookWindowsHookEx(_this->m_hKeyConfHook);
	}
        break;

	default:
        break;
	}
	return FALSE;
}


// -----------------------------
// マウス設定
// -----------------------------
void CWin32Config::SetupMousePage(HWND hDlg)
{
	SendDlgItemMessage(hDlg, IDC_SLIDER_MOUSE_SPEED, TBM_SETRANGE, TRUE, MAKELONG(1, 200));
	SendDlgItemMessage(hDlg, IDC_SLIDER_MOUSE_SPEED, TBM_SETPOS, TRUE, mDlgCfg.nMouseSpeed);
	UpdateMouseSpeedStr(hDlg);
}

void CWin32Config::DefaultMousePage(HWND hDlg)
{
	mDlgCfg.nMouseSpeed = 75;
	SetupMousePage(hDlg);
}

void CWin32Config::UpdateMouseSpeedStr(HWND hDlg)
{
	CString s;
	s.Format(_T("%d%%"), mDlgCfg.nMouseSpeed);
	SetDlgItemText(hDlg, IDC_STATIC_MOUSE_SPEED, s.GetBuffer());
}


// -----------------------------
// MIDI設定
// -----------------------------
void CWin32Config::SetupMidiPage(HWND hDlg)
{
	SINT32 i;

	SendDlgItemMessage(hDlg, IDC_COMBO_MIDI_DEVICE, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessage(hDlg, IDC_COMBO_MIDI_MODULE, CB_RESETCONTENT, 0, 0);

	mMidiModules = { _T("GM音源"), _T("GS音源"), _T("LA音源"), _T("XG音源") };

	mMidiDevices.clear();
	mMidiDevices.push_back(_T("MIDIを使用しない"));
	mMidiDeviceCount = midiOutGetNumDevs();
	for (i=0; i<mMidiDeviceCount; i++) {
		MIDIOUTCAPS moc;
		if ( midiOutGetDevCaps(i, &moc,sizeof(moc)) == MMSYSERR_NOERROR ) {
			mMidiDevices.push_back(moc.szPname);
		}
	}
	for (i=0; i<mMidiDevices.size(); i++) {
		SendDlgItemMessage(hDlg, IDC_COMBO_MIDI_DEVICE, CB_ADDSTRING, 0, (LPARAM)mMidiDevices[i].GetBuffer());
	}

	for (i=0; i<mMidiModules.size(); i++) {
		SendDlgItemMessage(hDlg, IDC_COMBO_MIDI_MODULE, CB_ADDSTRING, 0, (LPARAM)mMidiModules[i].GetBuffer());
	}

	if ( mDlgCfg.nMidiDeviceId >= mMidiDeviceCount ) mDlgCfg.nMidiDeviceId = -1;
	SendDlgItemMessage(hDlg, IDC_COMBO_MIDI_DEVICE, CB_SELECTSTRING, 0, (LPARAM)mMidiDevices[mDlgCfg.nMidiDeviceId+1].GetBuffer());
	SendDlgItemMessage(hDlg, IDC_COMBO_MIDI_MODULE, CB_SELECTSTRING, 0, (LPARAM)mMidiModules[mDlgCfg.nMidiModuleType].GetBuffer());

	CheckDlgButton(hDlg, IDC_CHECK_MIDI_SEND_RESET,  ( mDlgCfg.bMidiSendReset  ) ? BST_CHECKED : BST_UNCHECKED);

	UpdateMidiPage(hDlg);
}

void CWin32Config::DefaultMidiPage(HWND hDlg)
{
	mDlgCfg.nMidiDeviceId = -1;
	mDlgCfg.nMidiModuleType = 1;
	mDlgCfg.bMidiSendReset = 0;
	SendDlgItemMessage(hDlg, IDC_COMBO_MIDI_DEVICE, CB_SELECTSTRING, 0, (LPARAM)mMidiDevices[mDlgCfg.nMidiDeviceId+1].GetBuffer());
	SendDlgItemMessage(hDlg, IDC_COMBO_MIDI_MODULE, CB_SELECTSTRING, 0, (LPARAM)mMidiModules[mDlgCfg.nMidiModuleType].GetBuffer());
	UpdateMidiPage(hDlg);
}

void CWin32Config::UpdateMidiPage(HWND hDlg)
{
	BOOL sw = ( mDlgCfg.nMidiDeviceId < 0 ) ? FALSE : TRUE;
	EnableWindow(GetDlgItem(hDlg, IDC_STATIC_MIDI_MODULE), sw);
	EnableWindow(GetDlgItem(hDlg, IDC_COMBO_MIDI_MODULE), sw);
	EnableWindow(GetDlgItem(hDlg, IDC_CHECK_MIDI_SEND_RESET), sw);
}


// -----------------------------
// SASI HDD設定
// -----------------------------
static const UINT32 SASI_IMAGE_SIZE[] = { 0x09F5400, 0x13C9800, 0x2793000 };

void CWin32Config::SetupSasiPage(HWND hDlg)
{
	HWND hList = GetDlgItem(hDlg, IDC_LIST_SASI);

	// ヘッダ白いの見辛いんだけど、ハンドルは取れるが色変えられん（カスタムドローも効かないし、オーナードローするしかないっぽい）
	// HWND hHeader = ListView_GetHeader(hList);

	// 拡張スタイル設定 一列選択・罫線付き
	DWORD dwStyle = ListView_GetExtendedListViewStyle(hList);
	dwStyle |= LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES;
	ListView_SetExtendedListViewStyle(hList, dwStyle);

	static const UINT32 HEADER_CX_SIZE[] = { 22, 40, 240 };
	static TCHAR* HEARER_STR[] = { _T("#"), _T("容量"), _T("ファイル") };

	UINT32 i;
    LV_COLUMN lvcol;

    lvcol.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    lvcol.fmt = LVCFMT_LEFT;
	for (i=0; i<3; i++) {
		lvcol.cx = HEADER_CX_SIZE[i];
		lvcol.pszText = HEARER_STR[i];
		lvcol.iSubItem = i;
		ListView_DeleteColumn(hList, i);
		ListView_InsertColumn(hList, i, &lvcol);
	}

	// 一旦全アイテム削除
	ListView_DeleteAllItems(hList);

    LV_ITEM item;
	memset(&item, 0, sizeof(item));
	for (i=0; i<16; i++) {
		if ( !mDlgCfg.sSasiFile[i][0] ) break;

		CString s;

		item.mask = LVIF_TEXT;
		item.iItem = i;

		// 装置番号
		s.Format(_T("%d"), i);
		item.pszText = s.GetBuffer();
		item.iSubItem = 0;
		ListView_InsertItem(hList, &item);

		// 容量
		UINT32 sz = 0;
		if ( mDlgCfg.nSasiSize[i] >= SASI_IMAGE_SIZE[2] ) sz = 40;
		else if ( mDlgCfg.nSasiSize[i] >= SASI_IMAGE_SIZE[1] ) sz = 20;
		else if ( mDlgCfg.nSasiSize[i] >= SASI_IMAGE_SIZE[0] ) sz = 10;
		if ( sz ) {
			s.Format(_T("%dMb"), sz);
		} else {
			s.Format(_T("？"));
		}
		item.pszText = s.GetBuffer();
		item.iSubItem = 1;
		ListView_SetItem(hList, &item);

		// ファイル名
		item.pszText = mDlgCfg.sSasiFile[i];
		item.iSubItem = 2;
		ListView_SetItem(hList, &item);
	}

	BOOL sw = ( mDlgCfg.nSasiNum >= 16 ) ? FALSE : TRUE;
	EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SASI_ADD), sw);
	EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SASI_CREATE), sw);
	EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SASI_REMOVE), FALSE);

	CheckDlgButton(hDlg, IDC_CHECK_SASI_UPDATE,  ( mDlgCfg.bSasiSramUpd  ) ? BST_CHECKED : BST_UNCHECKED);

	mSasiSelected = -1;
}

void CWin32Config::DefaultSasiPage(HWND hDlg)
{
	UINT32 i;
	for (i=0; i<16; i++) mDlgCfg.sSasiFile[i][0] = 0;
	mDlgCfg.bSasiSramUpd = TRUE;
	SetupSasiInfo();
	SetupSasiPage(hDlg);
}

void CWin32Config::SetupSasiInfo()
{
	UINT32 i;
	mDlgCfg.nSasiNum = 0;
	for (i=0; i<16; i++) {
		mDlgCfg.nSasiSize[i] = 0;
		if ( mDlgCfg.sSasiFile[i][0] ) {
			HANDLE hfile = CreateFile(mDlgCfg.sSasiFile[i], GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if ( hfile != INVALID_HANDLE_VALUE ) {
				UINT32 sz = GetFileSize(hfile, NULL);
				if ( sz != INVALID_FILE_SIZE ) {
					mDlgCfg.nSasiSize[i] = sz;
				}
				CloseHandle(hfile);
				mDlgCfg.nSasiNum++;
			}
		}
	}
}

void CWin32Config::SelectSasiFile(HWND hDlg, BOOL is_create)
{
	OPENFILENAME ofn;
	TCHAR filename[MAX_PATH];

	if ( mDlgCfg.nSasiNum >= 16 ) return;  // 起こらないはずだが

	memset(&ofn, 0, sizeof(ofn));
	filename[0] = 0;

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hWnd;
	ofn.lpstrFilter = _T("SASI HDD Images (*.hdf)\0*.hdf\0")
					  _T("All Files (*.*)\0*.*\0\0");
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_SHAREAWARE | OFN_EXPLORER;
	ofn.Flags |= ( is_create ) ? 0 : OFN_FILEMUSTEXIST;
	ofn.lpstrDefExt = _T("hdf");
	ofn.lpstrTitle = _T("SASI HDDイメージファイルの選択");

	BOOL is_open;
	if ( is_create ) {
		_tcscpy(filename, _T("新規イメージ"));
		is_open = GetSaveFileName(&ofn);
	} else {
		is_open = GetOpenFileName(&ofn);
	}

	if ( is_open ) {
		BOOL is_ok = TRUE;
		if ( is_create ) {
			memcpy(mSasiCreateName, filename, MAX_PATH);
			is_ok = (BOOL)DialogBoxParam(m_hInst, MAKEINTRESOURCE(IDD_DIALOG_SASI_CREATE), hDlg, &SasiCreateDlgProc, (LPARAM)this);
		}
		if ( is_ok ) {
			memcpy(mDlgCfg.sSasiFile[mDlgCfg.nSasiNum], filename, MAX_PATH);
			SetupSasiInfo();
			SetupSasiPage(hDlg);
		}
	}
}

void CWin32Config::RemoveSasiFile(HWND hDlg)
{
	if ( mSasiSelected < 0 ) return;  // ないはずだが

	// 選択されたアイテムの次以降をひとつ前にずらす
	UINT32 i;
	for (i=mSasiSelected+1; i<16; i++) {
		memcpy(mDlgCfg.sSasiFile[i-1], mDlgCfg.sSasiFile[i], sizeof(mDlgCfg.sSasiFile[0]));
	}
	// 最後のアイテムをクリア
	mDlgCfg.sSasiFile[15][0] = 0;
	// 表示更新
	SetupSasiInfo();
	SetupSasiPage(hDlg);
}

INT_PTR CALLBACK CWin32Config::SasiCreateDlgProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	static UINT8 DUMMY_FILE_IMAGE[1024*1024];

	switch ( Msg )
	{
	case WM_INITDIALOG:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(lParam);
		SetWindowLongPtr(hDlg, GWL_USERDATA, (LONG_PTR)lParam);
		// 初期メッセージ設定
		SetDlgItemText(hDlg, IDC_STATIC_SASI_CREATE, _T("作成するHDDイメージのサイズを選んでください。"));
		// プログレスバー非表示
		ShowWindow(GetDlgItem(hDlg, IDC_PROGRESS_SASI_CREATE), SW_HIDE);
		// ラジオボタン選択
		CheckDlgButton(hDlg, IDC_RADIO_SASI_10MB, BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_RADIO_SASI_20MB, BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_RADIO_SASI_40MB, BST_CHECKED);
		_this->mSasiCreateRemain = 0;
		_this->m_hSasiCreateTimer = 0;
		_this->m_hSasiCreateFile = INVALID_HANDLE_VALUE;
		if ( PathFileExists(_this->mSasiCreateName) ) {
			MessageBox(hDlg, _T("指定されたファイル名のファイルは既に存在しています。\nHDDイメージを作成することができません。"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
			EndDialog(hDlg, FALSE);
			break;
		}
	}
		break;

	case WM_CLOSE:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(GetWindowLongPtr(hDlg, GWL_USERDATA));
		EndDialog(hDlg, FALSE);
	}
		break;

	case WM_COMMAND:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(GetWindowLongPtr(hDlg, GWL_USERDATA));
		switch ( LOWORD(wParam) )
		{
		case IDCANCEL:
			EndDialog(hDlg, FALSE);
	        break;
		case IDOK:
			// サイズ決定
			{
				UINT32 sz = SASI_IMAGE_SIZE[0];
				if ( Button_GetCheck(GetDlgItem(hDlg, IDC_RADIO_SASI_20MB)) == BST_CHECKED ) {
					sz = SASI_IMAGE_SIZE[1];
				}
				if ( Button_GetCheck(GetDlgItem(hDlg, IDC_RADIO_SASI_40MB)) == BST_CHECKED ) {
					sz = SASI_IMAGE_SIZE[2];
				}
				_this->mSasiCreateRemain = sz;
				memset(DUMMY_FILE_IMAGE, 0, sizeof(DUMMY_FILE_IMAGE));
			}
			// メッセージ設定
			SetDlgItemText(hDlg, IDC_STATIC_SASI_CREATE, _T("作成中です。"));
			// ラジオボタン無効化
			EnableWindow(GetDlgItem(hDlg, IDC_RADIO_SASI_10MB), FALSE);
			EnableWindow(GetDlgItem(hDlg, IDC_RADIO_SASI_20MB), FALSE);
			EnableWindow(GetDlgItem(hDlg, IDC_RADIO_SASI_40MB), FALSE);
			// OKボタン無効化
			EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
			// プログレスバー表示
			ShowWindow(GetDlgItem(hDlg, IDC_PROGRESS_SASI_CREATE), SW_SHOW);
			SendMessage(GetDlgItem(hDlg, IDC_PROGRESS_SASI_CREATE), PBM_SETRANGE, (WPARAM)0, MAKELPARAM(0, _this->mSasiCreateRemain / sizeof(DUMMY_FILE_IMAGE)));
			SendMessage(GetDlgItem(hDlg, IDC_PROGRESS_SASI_CREATE), PBM_SETSTEP, 1, 0);
			SendMessage(GetDlgItem(hDlg, IDC_PROGRESS_SASI_CREATE), PBM_SETPOS, (WPARAM)0, 0);
			// ファイルオープン
			_this->m_hSasiCreateFile = CreateFile(_this->mSasiCreateName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if ( _this->m_hSasiCreateFile == INVALID_HANDLE_VALUE ) {
				MessageBox(hDlg, _T("ファイルが作成できません。\nHDDイメージの作成に失敗しました。"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
				EndDialog(hDlg, FALSE);
				break;
			}
			// タイマ起動
			_this->m_hSasiCreateTimer = SetTimer(hDlg, 1, 20, NULL);
			if ( !_this->m_hSasiCreateTimer ) {
				MessageBox(hDlg, _T("イメージ作成タイマが起動できません。\nHDDイメージの作成に失敗しました。"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
				EndDialog(hDlg, FALSE);
				break;
			}
//			EndDialog(hDlg, TRUE);  // ここは終わっちゃダメ
	        break;
		}
	}
        break;

	case WM_TIMER:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(GetWindowLongPtr(hDlg, GWL_USERDATA));
		DWORD sz = sizeof(DUMMY_FILE_IMAGE);
		if ( sz > _this->mSasiCreateRemain ) sz = _this->mSasiCreateRemain;
		if ( _this->m_hSasiCreateFile != INVALID_HANDLE_VALUE ) {
			DWORD bytes;
			BOOL ret = WriteFile(_this->m_hSasiCreateFile, DUMMY_FILE_IMAGE, sz, &bytes, NULL);
			if ( !ret || ( bytes != sz ) ) {
				// 先にタイマ殺さないと、MessageBox() 中にもずっとタイマメッセージが飛んでくる
				KillTimer(hDlg, _this->m_hSasiCreateTimer);
				_this->m_hSasiCreateTimer = 0;
				MessageBox(hDlg, _T("ファイルへの書き込みができません。\nHDDイメージの作成に失敗しました。"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
				EndDialog(hDlg, FALSE);
				break;
			}
		}
		SendMessage(GetDlgItem(hDlg, IDC_PROGRESS_SASI_CREATE), PBM_STEPIT, 0, 0);
		_this->mSasiCreateRemain -= sz;
		if ( _this->mSasiCreateRemain == 0 ) {
			EndDialog(hDlg, TRUE);
		}
	}
		break;

    case WM_DESTROY:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(GetWindowLongPtr(hDlg, GWL_USERDATA));
		KillTimer(hDlg, _this->m_hSasiCreateTimer);
		CloseHandle(_this->m_hSasiCreateFile);
	}

	default:
        break;
	}
	return FALSE;
}


// -----------------------------
// その他設定
// -----------------------------
void CWin32Config::SetupEtcPage(HWND hDlg)
{
	CheckDlgButton(hDlg, IDC_CHECK_STATUS_BAR, ( mDlgCfg.bShowStatBar ) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_CHECK_FS_STATUS,  ( mDlgCfg.bShowFsStat  ) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_RADIO_TITLEBAR_0, (mDlgCfg.nTitleBarInfo==0) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_RADIO_TITLEBAR_1, (mDlgCfg.nTitleBarInfo==1) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_RADIO_TITLEBAR_2, (mDlgCfg.nTitleBarInfo==2) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_RADIO_TITLEBAR_3, (mDlgCfg.nTitleBarInfo==3) ? BST_CHECKED : BST_UNCHECKED);
}

void CWin32Config::DefaultEtcPage(HWND hDlg)
{
	mDlgCfg.bShowStatBar = TRUE;
	mDlgCfg.bShowFsStat = TRUE;
	mDlgCfg.nTitleBarInfo = 1;
	SetupEtcPage(hDlg);
}


// -----------------------------
// ヘルプテキスト関連
// -----------------------------
void CWin32Config::SetupHelpMessage()
{
	struct ST_MSGITEM {
		int itemid;
		int boxid;
		const TCHAR* msg;
	} MSGLIST[] = {
		{ IDC_COMBO_RAM_SIZE,          IDC_STATIC_SYSTEM_INFO,     _T("搭載メインメモリの容量を設定します。\n最大の12Mb設定は、起動時にIPLがエラーメッセージを出す場合があるため推奨しません。") },
		{ IDC_CHECK_RAM_SIZE_UPDATE,   IDC_STATIC_SYSTEM_INFO,     _T("SRAM内のメインメモリ容量設定を自動的に更新します。\nチェックを外すと、switch.x等で手動で設定する必要があります。") },
		{ IDC_CHECK_FAST_FDD,          IDC_STATIC_SYSTEM_INFO,     _T("チェックすると、フロッピーディスクへのアクセスを高速化します。\n高速化した場合、処理負荷の増加や一部ソフトウェアで不具合が発生する可能性があります。") },
		{ IDC_BUTTON_SYSTEM_DEFAULT,   IDC_STATIC_SYSTEM_INFO,     _T("このページの設定をデフォルト値に戻します。") },

		{ IDC_SLIDER_VOLUME_OPM,       IDC_STATIC_SOUND_INFO,      _T("OPMの音量を設定します。") },
		{ IDC_SLIDER_VOLUME_ADPCM,     IDC_STATIC_SOUND_INFO,      _T("ADPCMの音量を設定します。") },
		{ IDC_RADIO_OPM_FILTER0,       IDC_STATIC_SOUND_INFO,      _T("OPM出力のローパスフィルタを使用しません。") },
		{ IDC_RADIO_OPM_FILTER1,       IDC_STATIC_SOUND_INFO,      _T("OPM出力にローパスフィルタを適用します。") },
		{ IDC_RADIO_ADPCM_FILTER0,     IDC_STATIC_SOUND_INFO,      _T("ADPCM出力のローパスフィルタを使用しません。") },
		{ IDC_RADIO_ADPCM_FILTER1,     IDC_STATIC_SOUND_INFO,      _T("ADPCM出力に、実機に近い（強めの）ローパスフィルタを適用します。") },
		{ IDC_RADIO_ADPCM_FILTER2,     IDC_STATIC_SOUND_INFO,      _T("ADPCM出力に、高音質化改造を施した実機に近い（弱めの）ローパスフィルタを適用します。") },
		{ IDC_BUTTON_SOUND_DEFAULT,    IDC_STATIC_SOUND_INFO,      _T("このページの設定をデフォルト値に戻します。") },

		{ IDC_COMBO_JOY1_DEVICE,       IDC_STATIC_JOYSTICK_INFO,   _T("ジョイスティック#1として使用するデバイスを選択します。") },
		{ IDC_COMBO_JOY2_DEVICE,       IDC_STATIC_JOYSTICK_INFO,   _T("ジョイスティック#2として使用するデバイスを選択します。") },
		{ IDC_COMBO_JOY1_BUTTON_A,     IDC_STATIC_JOYSTICK_INFO,   _T("ジョイスティック#1のボタンAの割り当てを選択します。") },
		{ IDC_COMBO_JOY1_BUTTON_B,     IDC_STATIC_JOYSTICK_INFO,   _T("ジョイスティック#1のボタンBの割り当てを選択します。") },
		{ IDC_COMBO_JOY2_BUTTON_A,     IDC_STATIC_JOYSTICK_INFO,   _T("ジョイスティック#2のボタンAの割り当てを選択します。") },
		{ IDC_COMBO_JOY2_BUTTON_B,     IDC_STATIC_JOYSTICK_INFO,   _T("ジョイスティック#2のボタンBの割り当てを選択します。") },
		{ IDC_BUTTON_JOYSTICK_DEFAULT, IDC_STATIC_JOYSTICK_INFO,   _T("このページの設定をデフォルト値に戻します。") },

		{ IDC_SLIDER_MOUSE_SPEED,      IDC_STATIC_MOUSE_INFO,      _T("Windowsでのマウス移動量に対する、X68000のマウスの移動量の比率を設定します。") },
		{ IDC_BUTTON_MOUSE_DEFAULT,    IDC_STATIC_MOUSE_INFO,      _T("このページの設定をデフォルト値に戻します。") },

		{ IDC_COMBO_MIDI_DEVICE,       IDC_STATIC_MIDI_INFO,       _T("MIDIの出力先デバイスを選択します。") },
		{ IDC_COMBO_MIDI_MODULE,       IDC_STATIC_MIDI_INFO,       _T("MIDIに対して、どの方式の初期化コマンド送信するかを選択します。") },
		{ IDC_CHECK_MIDI_SEND_RESET,   IDC_STATIC_MIDI_INFO,       _T("チェックすると、エミュレータのソフトリセット時にもMIDIに対して初期化コマンドを送信します。") },
		{ IDC_BUTTON_MIDI_DEFAULT,     IDC_STATIC_MIDI_INFO,       _T("このページの設定をデフォルト値に戻します。") },

		{ IDC_LIST_SASI,               IDC_STATIC_SASI_INFO,       _T("接続されているHDDのリストです。") },
		{ IDC_BUTTON_SASI_ADD,         IDC_STATIC_SASI_INFO,       _T("ファイルを指定してHDDを追加します。\nHDDは上記リストの最後に追加されます。") },
		{ IDC_BUTTON_SASI_CREATE,      IDC_STATIC_SASI_INFO,       _T("新規にHDDイメージを作成してHDDを追加します。\nHDDは上記リストの最後に追加されます。") },
		{ IDC_BUTTON_SASI_REMOVE,      IDC_STATIC_SASI_INFO,       _T("選択したHDDをリストから取り外します。\n最後尾以外のHDDを外した場合、以降のHDDは前に詰められるため、識別番号（装置番号）が変化することに注意してください。") },
		{ IDC_CHECK_SASI_UPDATE,       IDC_STATIC_SASI_INFO,       _T("SRAM内のハードディスク接続数（HD_MAX）を自動更新します。\nチェックを外すと、switch.x等で手動で設定する必要があります。") },
		{ IDC_BUTTON_SASI_DEFAULT,     IDC_STATIC_SASI_INFO,       _T("このページの設定をデフォルト値に戻します。") },

		{ IDC_CHECK_STATUS_BAR,        IDC_STATIC_ETC_INFO,        _T("ウィンドウ表示時、画面下部にFDドライブステータスなどの情報を表示します。") },
		{ IDC_CHECK_FS_STATUS,         IDC_STATIC_ETC_INFO,        _T("フルスクリーン表示時、画面下部にFDドライブステータスなどの情報を表示します。") },
		{ IDC_RADIO_TITLEBAR_0,        IDC_STATIC_ETC_INFO,        _T("タイトルバーでの実行速度表示を行いません。") },
		{ IDC_RADIO_TITLEBAR_1,        IDC_STATIC_ETC_INFO,        _T("タイトルバーに実行クロックを表示します。") },
		{ IDC_RADIO_TITLEBAR_2,        IDC_STATIC_ETC_INFO,        _T("タイトルバーに実行速度を%で表示します。") },
		{ IDC_RADIO_TITLEBAR_3,        IDC_STATIC_ETC_INFO,        _T("タイトルバーに描画フレームレートを表示します。") },
		{ IDC_BUTTON_ETC_DEFAULT,      IDC_STATIC_ETC_INFO,        _T("このページの設定をデフォルト値に戻します。") },

		{ IDC_BUTTON_KEYBOARD_DEFAULT, IDC_STATIC_KEYBOARD_INFO,   _T("このページの設定をデフォルト値に戻します。") },

		{ 0, 0, NULL }
	};
	
	mMsgMap.clear();
	for (UINT32 i=0; MSGLIST[i].itemid; i++) {
		MSG_MAP_ITEM item;
		item.boxid = MSGLIST[i].boxid;
		item.msg = MSGLIST[i].msg;
		mMsgMap[MSGLIST[i].itemid] = item;
	}
}

void CWin32Config::ShowHelpMessage(HWND hDlg, UINT32 itemid)
{
	if ( mLastMsgId == itemid ) return;

	map<UINT32,UINT32>::iterator it_key = mMapKeyIdToTable.find(itemid);
	if ( it_key != mMapKeyIdToTable.end() ) {
		const CONFIGURABLE_KEY_ITEM& item = CONF_KEYS[it_key->second];
		UINT32 vk = mMap68KeyToWinKey[item.x68code];
		TCHAR keyname[64];  // GetKeyNameText() の返値バッファなので、CString使わないこと
		CString s;
		if ( vk ) {
			UINT32 keycode = ( ( MapVirtualKey(vk & 0xFF, 0) & 0xFF ) | ( vk & 0x100 ) ) << 16; 
			if ( !GetKeyNameText((LONG)keycode, keyname, 64) ) {
				_stprintf_s(keyname, _T("名称不明なキー"));
			}
			s.Format(_T("X68000の「%s」キーに割り当てるキーを設定します。\n現在の割り当ては「%s」(VK=0x%02X/EXT=%d) です。"), item.name, keyname, vk&0xFF, vk>>8);
		} else {
			s.Format(_T("X68000の「%s」キーに割り当てるキーを設定します。\n現在割り当てられているキーはありません。"), item.name);
		}
		SetDlgItemText(hDlg, IDC_STATIC_KEYBOARD_INFO, s.GetBuffer());
		mLastMsgId = itemid;
		mLastMsgBox = IDC_STATIC_KEYBOARD_INFO;
		return;
	}

	map<UINT32,MSG_MAP_ITEM>::iterator it = mMsgMap.find(itemid);
	if ( it != mMsgMap.end() ) {
		MSG_MAP_ITEM& item = it->second;
		SetDlgItemText(hDlg, item.boxid, item.msg.GetString());
		mLastMsgId = itemid;
		mLastMsgBox = item.boxid;
		return;
	}

	if ( mLastMsgBox ) {
		SetDlgItemText(hDlg, mLastMsgBox, _T(""));
		mLastMsgId = 0;
		mLastMsgBox = 0;
	}
}


// -----------------------------
// ダイアログプロシージャ
// -----------------------------
INT_PTR CALLBACK CWin32Config::PropDialogProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
	case WM_INITDIALOG:
	{
		PROPSHEETPAGE* psp = (PROPSHEETPAGE*)lParam;
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(psp->lParam);

		memcpy(&_this->mDlgCfgBk, &_this->mDlgCfg, sizeof(_this->mDlgCfg));
		SetWindowLongPtr(hDlg, GWL_USERDATA, (LONG_PTR)psp->lParam);

		_this->SetupSystemPage(hDlg);
		_this->SetupKeyboardPage(hDlg);
		_this->SetupJoystickPage(hDlg);
		_this->SetupMousePage(hDlg);
		_this->SetupSoundPage(hDlg);
		_this->SetupMidiPage(hDlg);
		_this->SetupSasiPage(hDlg);
		_this->SetupEtcPage(hDlg);

		SetDlgItemText(hDlg, IDC_STATIC_VERSION_MAIN_INFO, _this->mVersionString.GetString());
		SetDlgItemText(hDlg, IDC_STATIC_VERSION_ADD_INFO, ADDITIONAL_VERSION_STRING);
	}
		return TRUE;

	case WM_COMMAND:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(GetWindowLongPtr(hDlg, GWL_USERDATA));
		UINT32 id = LOWORD(wParam);
		if ( HIWORD(wParam) == BN_CLICKED ) {
			if ( id >= IDC_KEY_BREAK && id<= IDC_KEY_YEN ) {
				SINT32 result = (SINT32)DialogBoxParam(_this->m_hInst, MAKEINTRESOURCE(IDD_DIALOG_KEYCONFIG), hDlg, &_this->KeyConfDialogProc, (LPARAM)_this);
				if ( result >= 0 ) {
					_this->ChangeKeyAssign(id, result);

				}
			}
			else switch ( id )
			{
			case IDC_BUTTON_SYSTEM_DEFAULT:
				_this->DefaultSystemPage(hDlg);
				break;
			case IDC_BUTTON_JOYSTICK_DEFAULT:
				_this->DefaultJoystickPage(hDlg);
				break;
			case IDC_BUTTON_KEYBOARD_DEFAULT:
				_this->DefaultKeyboardPage(hDlg);
				break;
			case IDC_BUTTON_MOUSE_DEFAULT:
				_this->DefaultMousePage(hDlg);
				break;
			case IDC_BUTTON_SOUND_DEFAULT:
				_this->DefaultSoundPage(hDlg);
				break;
			case IDC_BUTTON_MIDI_DEFAULT:
				_this->DefaultMidiPage(hDlg);
				break;
			case IDC_BUTTON_SASI_DEFAULT:
				_this->DefaultSasiPage(hDlg);
				break;
			case IDC_BUTTON_ETC_DEFAULT:
				_this->DefaultEtcPage(hDlg);
				break;

			case IDC_RADIO_OPM_FILTER0:
			case IDC_RADIO_OPM_FILTER1:
				_this->mDlgCfg.nFilterOPM = id - IDC_RADIO_OPM_FILTER0;
				break;

			case IDC_RADIO_ADPCM_FILTER0:
			case IDC_RADIO_ADPCM_FILTER1:
			case IDC_RADIO_ADPCM_FILTER2:
				_this->mDlgCfg.nFilterADPCM = id - IDC_RADIO_ADPCM_FILTER0;
				break;

			case IDC_CHECK_RAM_SIZE_UPDATE:
				_this->mDlgCfg.bRamSizeUpdate = !_this->mDlgCfg.bRamSizeUpdate; 
				break;
			case IDC_CHECK_FAST_FDD:
				_this->mDlgCfg.bFastFdd = !_this->mDlgCfg.bFastFdd; 
				break;

			case IDC_CHECK_MIDI_SEND_RESET:
				_this->mDlgCfg.bMidiSendReset = !_this->mDlgCfg.bMidiSendReset; 
				break;

			case IDC_BUTTON_SASI_ADD:
				_this->SelectSasiFile(hDlg, FALSE);
				break;
			case IDC_BUTTON_SASI_CREATE:
				_this->SelectSasiFile(hDlg, TRUE);
				break;
			case IDC_BUTTON_SASI_REMOVE:
				_this->RemoveSasiFile(hDlg);
				break;
			case IDC_CHECK_SASI_UPDATE:
				_this->mDlgCfg.bSasiSramUpd = !_this->mDlgCfg.bSasiSramUpd; 

			case IDC_CHECK_STATUS_BAR:
				_this->mDlgCfg.bShowStatBar = !_this->mDlgCfg.bShowStatBar; 
				break;
			case IDC_CHECK_FS_STATUS:
				_this->mDlgCfg.bShowFsStat = !_this->mDlgCfg.bShowFsStat; 
				break;
			case IDC_RADIO_TITLEBAR_0:
			case IDC_RADIO_TITLEBAR_1:
			case IDC_RADIO_TITLEBAR_2:
			case IDC_RADIO_TITLEBAR_3:
				_this->mDlgCfg.nTitleBarInfo = id - IDC_RADIO_TITLEBAR_0;
				break;
			}
		} else if ( HIWORD(wParam) == CBN_SELCHANGE ) {
			switch ( id )
			{
			case IDC_COMBO_JOY1_DEVICE:
			case IDC_COMBO_JOY2_DEVICE:
				_this->mDlgCfg.nJoystickIdx[id-IDC_COMBO_JOY1_DEVICE] = (UINT32)SendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0);
				_this->UpdateJoystickCombo(hDlg);
				break;
			case IDC_COMBO_JOY1_BUTTON_A:
			case IDC_COMBO_JOY1_BUTTON_B:
				if ( _this->mDlgCfg.nJoystickIdx[0]==1 ) {
					_this->mDlgCfg.nJoyKeyBtn[0][id-IDC_COMBO_JOY1_BUTTON_A] = (UINT32)SendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0);
				} else {
					_this->mDlgCfg.nJoystickBtn[0][id-IDC_COMBO_JOY1_BUTTON_A] = (UINT32)SendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0);
				}
				break;
			case IDC_COMBO_JOY2_BUTTON_A:
			case IDC_COMBO_JOY2_BUTTON_B:
				if ( _this->mDlgCfg.nJoystickIdx[1]==1 ) {
					_this->mDlgCfg.nJoyKeyBtn[1][id-IDC_COMBO_JOY2_BUTTON_A] = (UINT32)SendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0);
				} else {
					_this->mDlgCfg.nJoystickBtn[1][id-IDC_COMBO_JOY2_BUTTON_A] = (UINT32)SendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0);
				}
				break;
			case IDC_COMBO_RAM_SIZE:
				_this->mDlgCfg.nRamSize = (UINT32)(SendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0)+1)*2;
				break;
			case IDC_COMBO_MIDI_DEVICE:
				_this->mDlgCfg.nMidiDeviceId = (SINT32)SendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0) - 1;
				_this->UpdateMidiPage(hDlg);
				break;
			case IDC_COMBO_MIDI_MODULE:
				_this->mDlgCfg.nMidiModuleType = (UINT32)SendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0);
				break;
			}
		}
		if ( memcmp(&_this->mDlgCfgBk, &_this->mDlgCfg, sizeof(_this->mDlgCfg)) ) {
			PropSheet_Changed(GetParent(hDlg), hDlg);
		}
	}
		return 0;

	case WM_HSCROLL:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(GetWindowLongPtr(hDlg, GWL_USERDATA));
		switch ( GetDlgCtrlID((HWND)lParam) )
		{
		case IDC_SLIDER_VOLUME_OPM:
			_this->mDlgCfg.nVolumeOPM = (SINT32)SendDlgItemMessage(hDlg, IDC_SLIDER_VOLUME_OPM, TBM_GETPOS, 0, 0);
			_this->UpdateVolumeText(hDlg, IDC_STATIC_VOLUME_OPM, _this->mDlgCfg.nVolumeOPM);
			break;
		case IDC_SLIDER_VOLUME_ADPCM:
			_this->mDlgCfg.nVolumeADPCM = (SINT32)SendDlgItemMessage(hDlg, IDC_SLIDER_VOLUME_ADPCM, TBM_GETPOS, 0, 0);
			_this->UpdateVolumeText(hDlg, IDC_STATIC_VOLUME_ADPCM, _this->mDlgCfg.nVolumeADPCM);
			break;
		case IDC_SLIDER_MOUSE_SPEED:
			_this->mDlgCfg.nMouseSpeed = (SINT32)SendDlgItemMessage(hDlg, IDC_SLIDER_MOUSE_SPEED, TBM_GETPOS, 0, 0);
			_this->UpdateMouseSpeedStr(hDlg);
			break;
		}
		if ( memcmp(&_this->mDlgCfgBk, &_this->mDlgCfg, sizeof(_this->mDlgCfg)) ) {
			PropSheet_Changed(GetParent(hDlg), hDlg);
		}
	}
		return 0;

	case WM_SETCURSOR:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(GetWindowLongPtr(hDlg, GWL_USERDATA));
		int id = GetDlgCtrlID((HWND)wParam);
		_this->ShowHelpMessage(hDlg, id);
	}
		break;

	case WM_NOTIFY:
	{
		CWin32Config* _this = reinterpret_cast<CWin32Config*>(GetWindowLongPtr(hDlg, GWL_USERDATA));
		NMHDR* lpnm = (NMHDR*)lParam;
		UINT code = lpnm->code;
		switch (code)
		{
		case PSN_SETACTIVE:
			break;

		case PSN_APPLY:
			// 適用
			memcpy(&_this->mDlgCfgBk, &_this->mDlgCfg, sizeof(_this->mDlgCfg));
			PropSheet_UnChanged(GetParent(hDlg), hDlg);
			return PSNRET_NOERROR;
		
		case PSN_QUERYCANCEL:
			// キャンセル 設定類元に戻す
			memcpy(&_this->mDlgCfg, &_this->mDlgCfgBk, sizeof(_this->mDlgCfg));
			return FALSE;		

		case NM_CLICK:
			if ( lpnm->idFrom == IDC_LIST_SASI ) {
				NMITEMACTIVATE* lpact = (NMITEMACTIVATE*)lParam;
				_this->mSasiSelected = lpact->iItem;
				if ( _this->mSasiSelected >= 0 ) {
					EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SASI_REMOVE), TRUE);
				} else {
					EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SASI_REMOVE), FALSE);
				}
			}
			break;
		}
	}
		return TRUE;

	}
	return 0;
}


// -----------------------------
// プロパティシート表示
// -----------------------------
void CWin32Config::ShowSettingDialog(HINSTANCE hInst, HWND hWnd)
{
	static const UINT32 PROP_PAGES[] = {
		IDD_DIALOG_SYSTEM,
		IDD_DIALOG_KEYBOARD,
		IDD_DIALOG_MOUSE,
		IDD_DIALOG_JOYSTICK,
		IDD_DIALOG_SOUND,
		IDD_DIALOG_MIDI,
		IDD_DIALOG_SASI,
		IDD_DIALOG_ETC,
		IDD_DIALOG_VERSION,
	};
	static const UINT32 PROP_PAGE_MAX = sizeof(PROP_PAGES) / sizeof(PROP_PAGES[0]);

	PROPSHEETPAGE   pspage[PROP_PAGE_MAX];
	PROPSHEETHEADER pshead;

	m_hWnd = m_hWnd;
	m_hInst = m_hInst;

	mLastMsgId = 0;
	mLastMsgBox = 0;

	pshead.hwndParent = hWnd;
	pshead.dwSize     = sizeof(PROPSHEETHEADER);
	pshead.dwFlags    = PSH_PROPSHEETPAGE | PSH_USEICONID | PSH_NOCONTEXTHELP;
	pshead.hInstance  = hInst;
	pshead.pszCaption = _T("設定");
	pshead.nPages     = PROP_PAGE_MAX;
	pshead.nStartPage = 0;
	pshead.pszIcon    = MAKEINTRESOURCE(IDI_MAINICON);
	pshead.ppsp       = pspage;

	for (UINT32 i=0; i<PROP_PAGE_MAX; i++) {
		pspage[i].dwSize      = sizeof(PROPSHEETPAGE);
		pspage[i].dwFlags     = 0;
		pspage[i].hInstance   = hInst;
		pspage[i].pszTemplate = MAKEINTRESOURCE(PROP_PAGES[i]);
		pspage[i].pfnCallback = NULL;
		pspage[i].lParam      = (LPARAM)this;
		pspage[i].pfnDlgProc  = &PropDialogProc;
	}

	PropertySheet(&pshead);
}
