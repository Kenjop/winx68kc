/* -----------------------------------------------------------------------------------
  Win32ウィンドウ・メニュー関連処理
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "win32menu.h"
#include "x68000_driver.h"
#include "resource.h"
#include <atlstr.h>

// ステートファイル拡張子
#define STATEFILEEXT	     _T(".sav")

// ステートファイルID/バージョン
#define STATE_FILE_ID        "WINX68K_STATE_DATA"  // これはファイルヘッダに書き込むIDなので TCHAR にしない
#define STATE_FILE_VERSION   1


// --------------------------------------------------------------------------
//   初期化/終了
// --------------------------------------------------------------------------
CWin32Menu::CWin32Menu()
{
	m_hWnd = NULL;
	m_hMenu = NULL;
	m_pCore = NULL;
	m_hD3D = NULL;

	mDiskEvent.clear();
}

CWin32Menu::~CWin32Menu()
{
}

void CWin32Menu::Init(HINSTANCE hInst, HWND hwnd, HMENU hmenu, CWin32Config* cfg, CWin32Core* core)
{
	m_hInst = hInst;
	m_hWnd = hwnd;
	m_hMenu = hmenu;
	m_pCfg = cfg;
	m_pCore = core;
	m_hD3D = core->GetDrawHandle();

	mStatBar.Init(m_hWnd);
	mStatBar.SelectColorset(STATBAR_COLORSET_NORMAL);
	mStatBar.Show(m_pCfg->mDlgCfg.bShowStatBar);

	m_bFullScreen = FALSE;

	m_bMouseCapture = FALSE;
	m_ptMousePos.x = 0;
	m_ptMousePos.y = 0;
	m_ptMouseMove.x = 0;
	m_ptMouseMove.y = 0;

	m_pCore->SetDiskEjectCallback(&CWin32Menu::DiskEjectCallback, (void*)this);
}


// --------------------------------------------------------------------------
//   ウィンドウサイズ関連
// --------------------------------------------------------------------------
void CWin32Menu::SetWindowSize(int width, int height)
{
	RECT rectw, rectc;
	int	scx, scy;
	int x, y, w, h;

	GetWindowRect(m_hWnd, &rectw);
	GetClientRect(m_hWnd, &rectc);

	x = rectw.left;
	y = rectw.top;
	w = width +(rectw.right-rectw.left)-(rectc.right-rectc.left);
	h = height+(rectw.bottom-rectw.top)-(rectc.bottom-rectc.top);

	scx = GetSystemMetrics(SM_CXSCREEN);
	scy = GetSystemMetrics(SM_CYSCREEN);
	if ( scx<w )
		x = (scx-w)/2;
	else if ( x<0 )
		x = 0;
	else if ( (x+w)>scx )
		x = scx-w;
	if ( scy<h )
		y = (scy-h)/2;
	else if ( y<0 )
		y = 0;
	else if ( (y+h)>scy )
		y = scy-h;

	int sh = mStatBar.GetHeight();
	MoveWindow(m_hWnd, x, y, w, h+mStatBar.GetHeight(), TRUE);
}

void CWin32Menu::SetFullScreen(BOOL sw)
{
	if ( m_bFullScreen == sw ) return;

	// 現在のウィンドウスタイル取得
	UINT32 style, styleex;
	style = GetWindowLong(m_hWnd, GWL_STYLE);
	styleex = GetWindowLong(m_hWnd, GWL_EXSTYLE);

	// スクリーン状態に合わせて処理
	if ( sw ) {
		// フルスクリーンになった
		RECT rect;
		int scx = GetSystemMetrics(SM_CXSCREEN);
		int scy = GetSystemMetrics(SM_CYSCREEN);
		GetWindowRect(m_hWnd, &rect);
		m_ptWinPos.x = rect.left;
		m_ptWinPos.y = rect.top;
		style = (style | WS_POPUP) & (~(WS_CAPTION | WS_OVERLAPPED | WS_SYSMENU));
		styleex |= WS_EX_TOPMOST;
		SetWindowLong(m_hWnd, GWL_STYLE, style);
		SetWindowLong(m_hWnd, GWL_EXSTYLE, styleex);
		SetMenu(m_hWnd, NULL);
		mStatBar.Show(m_pCfg->mDlgCfg.bShowFsStat);
		mStatBar.SelectColorset(STATBAR_COLORSET_FULLSCREEN);
		D3DDraw_SetStatArea(m_hD3D, mStatBar.GetHeight());
		D3DDraw_Resize(m_hD3D, scx, scy);
		MoveWindow(m_hWnd, 0, 0, scx, scy, TRUE);
	} else {
		// ウィンドウ表示になった
		style = (style | WS_CAPTION | WS_OVERLAPPED | WS_SYSMENU) & ~WS_POPUP;
		styleex &= (~WS_EX_TOPMOST);
		SetWindowLong(m_hWnd, GWL_STYLE, style);
		SetWindowLong(m_hWnd, GWL_EXSTYLE, styleex);
		SetMenu(m_hWnd, m_hMenu);
		mStatBar.Show(m_pCfg->mDlgCfg.bShowStatBar);
		mStatBar.SelectColorset(STATBAR_COLORSET_NORMAL);
		D3DDraw_SetStatArea(m_hD3D, mStatBar.GetHeight());
		D3DDraw_Resize(m_hD3D, m_ptWinSize.x, m_ptWinSize.y);
		MoveWindow(m_hWnd, m_ptWinPos.x, m_ptWinPos.y, m_ptWinSize.x, m_ptWinSize.y+mStatBar.GetHeight(), TRUE);
		SetWindowSize(m_ptWinSize.x, m_ptWinSize.y);
	}

	m_bFullScreen = sw;
}

void CWin32Menu::ChangeScreenSize(UINT32 width)
{
	static const UINT32 WIDTH[] = { 768, 1152, 1536, 1920 }; 
	UINT32 w, h;

	if ( width >= (sizeof(WIDTH)/sizeof(WIDTH[0])) ) return;

	m_pCfg->nScreenSize = width;

	w = WIDTH[width];
	switch ( m_pCfg->nScreenAspect )
	{
	case D3DDRAW_ASPECT_3_2:
		h = (w*2) / 3;
		break;
	case D3DDRAW_ASPECT_4_3:
		h = (w*3) / 4;
		break;
	case D3DDRAW_ASPECT_FREE:
	default:
		return;
	}

	m_ptWinSize.x = w;
	m_ptWinSize.y = h;

	if ( !m_pCfg->bFullScreen ) {
		D3DDraw_Resize(m_hD3D, m_ptWinSize.x, m_ptWinSize.y);
		SetWindowSize(w, h);
	}
}

void CWin32Menu::ChangeScreenAspect(UINT32 aspect)
{
	if ( aspect > D3DDRAW_ASPECT_FREE ) return;
	m_pCfg->nScreenAspect = aspect;
	D3DDraw_SetAspect(m_hD3D, (D3DDRAW_ASPECT)m_pCfg->nScreenAspect);
	ChangeScreenSize(m_pCfg->nScreenSize);
}

void CWin32Menu::UpdateStatBarState()
{
	if ( !m_bFullScreen ) {
		mStatBar.Show(m_pCfg->mDlgCfg.bShowStatBar);
		SetWindowSize(m_ptWinSize.x, m_ptWinSize.y);
		D3DDraw_SetStatArea(m_hD3D, mStatBar.GetHeight());
	} else {
		mStatBar.Show(m_pCfg->mDlgCfg.bShowFsStat);
		D3DDraw_SetStatArea(m_hD3D, mStatBar.GetHeight());
	}
}


// --------------------------------------------------------------------------
//   メニューアイテムのチェックマーク処理
// --------------------------------------------------------------------------
typedef struct {
	UINT32   id;
	UINT32   group;
} ST_ID_GROUP_INFO;

static const ST_ID_GROUP_INFO CHECKITEM_LIST[] = {
	{ ID_SIZE_X0,          1 },
	{ ID_SIZE_X1,          1 },
	{ ID_SIZE_X2,          1 },
	{ ID_SIZE_X3,          1 },
	{ ID_ASPECT_3_2,       2 },
	{ ID_ASPECT_4_3,       2 },
	{ ID_ZOOM_IGNORE_CRTC, 0 },
	{ ID_INTERPOLATION,    0 },
	{ ID_HW_VSYNC,         0 },
	{ ID_FULLSCREEN,       0 },
	{ ID_CPUCLK_10,        3 },
	{ ID_CPUCLK_16,        3 },
	{ ID_CPUCLK_24,        3 },
	{ ID_NO_WAIT,          4 },  // ノーウェイトとポーズは排他
	{ ID_EXEC_PAUSE,       4 },
	{ 0, 0 }
};

void CWin32Menu::CheckMenu(UINT32 id, BOOL check)
{
	if ( check ) {
		// チェック時
		const ST_ID_GROUP_INFO* info = &CHECKITEM_LIST[0];
		// 該当IDをリストから探す（map使いてえ…）
		for ( ; info->id; info++) {
			if ( info->id==id ) {
				// ID見つかった
				CheckMenuItem(m_hMenu, id, MF_CHECKED);
				// グループ所属なら同グループの他アイテムのチェックを外す
				if ( info->group ) {
					const ST_ID_GROUP_INFO* info2 = &CHECKITEM_LIST[0];
					for ( ; info2->id; info2++) {
						if ( info2->id!=info->id && info2->group==info->group ) {
							CheckMenuItem(m_hMenu, info2->id, MF_UNCHECKED);
						}
					}
				}
			}
		}
	} else {
		// チェック解除時
		CheckMenuItem(m_hMenu, id, MF_UNCHECKED);
	}
}


// --------------------------------------------------------------------------
//   メニュー処理
// --------------------------------------------------------------------------
void CWin32Menu::SetupMenu()
{
	CheckMenu(ID_SIZE_X0 + m_pCfg->nScreenSize, TRUE);
	CheckMenu(ID_ASPECT_3_2 + m_pCfg->nScreenAspect, TRUE);
	CheckMenu(ID_ZOOM_IGNORE_CRTC, m_pCfg->bZoomIgnoreCrtc);
	CheckMenu(ID_INTERPOLATION, m_pCfg->bScreenInterp);
	CheckMenu(ID_HW_VSYNC, m_pCfg->bHardwareVsync);
	CheckMenu(ID_FULLSCREEN, m_pCfg->bFullScreen);
	CheckMenu(ID_CPUCLK_10 + m_pCfg->nCpuClock, TRUE);

	EnableMenuItem(m_hMenu, ID_EXEC_STEP, MF_DISABLED);

	D3DDraw_SetStatArea(m_hD3D, mStatBar.GetHeight());
	D3DDraw_SetFilter(m_hD3D, ( m_pCfg->bScreenInterp ) ? D3DDRAW_FILTER_LINEAR : D3DDRAW_FILTER_POINT );
	ChangeScreenAspect(m_pCfg->nScreenAspect);
}

void CWin32Menu::MenuProc(WPARAM wParam, LPARAM lParam)
{
	UINT32 id = LOWORD(wParam);
	switch ( id )
	{
	// ----------------------------------------
	// 「システム」メニュー
	case ID_SYSTEM_QUIT:
		SendMessage(m_hWnd, WM_CLOSE, 0, 0);
		break;
	case ID_SYSTEM_RESET:
		m_pCore->Reset();
		break;

//	case ID_SLOTCHANGE:
//		m_pCfg->nStateSlot = (m_pCfg->nStateSlot+1) % STATESLOTS;
//		break;
	case ID_STATELOAD_01: case ID_STATELOAD_02: case ID_STATELOAD_03: case ID_STATELOAD_04:
	case ID_STATELOAD_05: case ID_STATELOAD_06: case ID_STATELOAD_07: case ID_STATELOAD_08:
	case ID_STATELOAD_09: case ID_STATELOAD_10: case ID_STATELOAD_11: case ID_STATELOAD_12:
		LoadState(id-ID_STATELOAD_01+1);
		break;
	case ID_STATELOAD_FILE:
		OpenStateFile(FALSE);
		break;
	case ID_STATESAVE_01: case ID_STATESAVE_02: case ID_STATESAVE_03: case ID_STATESAVE_04:
	case ID_STATESAVE_05: case ID_STATESAVE_06: case ID_STATESAVE_07: case ID_STATESAVE_08:
	case ID_STATESAVE_09: case ID_STATESAVE_10: case ID_STATESAVE_11: case ID_STATESAVE_12:
		SaveState(id-ID_STATESAVE_01+1);
		break;
	case ID_STATESAVE_FILE:
		OpenStateFile(TRUE);
		break;

	// ----------------------------------------
	// 「表示」メニュー
	case ID_SIZE_X0:
		ChangeScreenSize(0);
		CheckMenu(id, TRUE);
		break;
	case ID_SIZE_X1:
		ChangeScreenSize(1);
		CheckMenu(id, TRUE);
		break;
	case ID_SIZE_X2:
		ChangeScreenSize(2);
		CheckMenu(id, TRUE);
		break;
	case ID_SIZE_X3:
		ChangeScreenSize(3);
		CheckMenu(id, TRUE);
		break;

	case ID_ASPECT_3_2:
		ChangeScreenAspect(D3DDRAW_ASPECT_3_2);
		CheckMenu(id, TRUE);
		break;
	case ID_ASPECT_4_3:
		ChangeScreenAspect(D3DDRAW_ASPECT_4_3);
		CheckMenu(id, TRUE);
		break;

	case ID_ZOOM_IGNORE_CRTC:
		m_pCfg->bZoomIgnoreCrtc = !m_pCfg->bZoomIgnoreCrtc;
		CheckMenu(id, m_pCfg->bZoomIgnoreCrtc);
		break;

	case ID_INTERPOLATION:
		m_pCfg->bScreenInterp = !m_pCfg->bScreenInterp;
		CheckMenu(id, m_pCfg->bScreenInterp);
		D3DDraw_SetFilter(m_hD3D, ( m_pCfg->bScreenInterp ) ? D3DDRAW_FILTER_LINEAR : D3DDRAW_FILTER_POINT );
		break;

	case ID_HW_VSYNC:
		m_pCfg->bHardwareVsync = !m_pCfg->bHardwareVsync;
		CheckMenu(id, m_pCfg->bHardwareVsync);
		// ソフトウェアに戻った時は、時間計測用のタイマ値をリセットしておく
		if ( !m_pCfg->bHardwareVsync ) {
			m_pCore->RunTimerReset();
		}
		break;

	case ID_FULLSCREEN:
		m_pCfg->bFullScreen = !m_pCfg->bFullScreen;
		SetFullScreen(m_pCfg->bFullScreen);
		CheckMenu(id, m_pCfg->bFullScreen);
		break;

	// ----------------------------------------
	// 「動作」メニュー
	case ID_CPUCLK_10:
		m_pCfg->nCpuClock = X68K_CLK_10MHZ;
		X68kDriver_SetCpuClock(m_pCore->GetDriver(), m_pCfg->nCpuClock);
		CheckMenu(id, TRUE);
		break;
	case ID_CPUCLK_16:
		m_pCfg->nCpuClock = X68K_CLK_16MHZ;
		X68kDriver_SetCpuClock(m_pCore->GetDriver(), m_pCfg->nCpuClock);
		CheckMenu(id, TRUE);
		break;
	case ID_CPUCLK_24:
		m_pCfg->nCpuClock = X68K_CLK_24MHZ;
		X68kDriver_SetCpuClock(m_pCore->GetDriver(), m_pCfg->nCpuClock);
		CheckMenu(id, TRUE);
		break;

	case ID_NO_WAIT:
		{
			bool state = !m_pCore->GetNoWaitState();
			CheckMenu(id, (state)?TRUE:FALSE);
			m_pCore->SetNoWait(state);
			EnableMenuItem(m_hMenu, ID_EXEC_STEP, MF_DISABLED);
		}
		break;
	case ID_EXEC_PAUSE:
		{
			bool state = !m_pCore->GetPauseState();
			CheckMenu(id, (state)?TRUE:FALSE);
			m_pCore->SetPause(state);
			EnableMenuItem(m_hMenu, ID_EXEC_STEP, ( state ) ? MF_ENABLED : MF_DISABLED);
		}
		break;
	case ID_EXEC_STEP:
		if ( m_pCore->GetPauseState() ) {
			m_pCore->ExecStep();
		}
		break;

	case ID_USE_MOUSE:
		ToggleMouseCapture();
		break;

	case ID_SETTING_DIALOG:
		m_pCfg->ShowSettingDialog(m_hInst, m_hWnd);
		m_pCore->ApplyConfig();
		UpdateStatBarState();
		break;

	// ----------------------------------------
	// 「FDD」メニュー
	case ID_FDD0_INSERT:
		OpenDiskImage(0);
		break;
	case ID_FDD0_EJECT:
		EjectDisk(0);
		break;
	case ID_FDD1_INSERT:
		OpenDiskImage(1);
		break;
	case ID_FDD1_EJECT:
		EjectDisk(1);
		break;

	default:
		break;
	}
}


// --------------------------------------------------------------------------
//   ステートロード/セーブ
// --------------------------------------------------------------------------
// クラス定義
class CStateInterface
{
public:
	CStateInterface();
	~CStateInterface();

	void Open(const TCHAR* file, BOOL write);
	void Close();

	BOOL Read(const STATE* state, UINT32 id, UINT32 sub_id, void* data, UINT32 size);
	BOOL Write(const STATE* state, UINT32 id, UINT32 sub_id, const void* data, UINT32 size);

	static BOOL CALLBACK ReadCb(const void* prm, UINT32 id, UINT32 sub_id, void* data, UINT32 size);
	static BOOL CALLBACK WriteCb(const void* prm, UINT32 id, UINT32 sub_id, const void* data, UINT32 size);

	STATE* GetState() { return &stState; }

private:
	BOOL bWrite;
	HANDLE hFile;
	STATE stState;
};


CStateInterface::CStateInterface()
{
	bWrite = FALSE;
	hFile = INVALID_HANDLE_VALUE;

	// STATE構造体用意
	stState.prm = (void*)this;
	stState.handle = (void*)INVALID_HANDLE_VALUE;  // 未使用
	stState.read_cb = &CStateInterface::ReadCb;
	stState.write_cb = &CStateInterface::WriteCb;
}

CStateInterface::~CStateInterface()
{
	// 閉じ忘れてるときは閉じる
	Close();
}

void CStateInterface::Open(const TCHAR* file, BOOL write)
{
	bWrite = write;

	if ( bWrite ) {
		// 書き込み用オープン
		hFile = CreateFile(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	} else {
		// 読み込み用オープン
		hFile = CreateFile(file, GENERIC_READ,  0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	}
}

void CStateInterface::Close()
{
	if ( hFile != INVALID_HANDLE_VALUE ) {
		CloseHandle(hFile);
		hFile = INVALID_HANDLE_VALUE;
	}
}

// スタティックのコールバック関数
BOOL CALLBACK CStateInterface::ReadCb(const void* prm, UINT32 id, UINT32 sub_id, void* data, UINT32 size)
{
	const STATE* state = (const STATE*)prm;
	CStateInterface* _this = reinterpret_cast<CStateInterface*>(state->prm);
	return _this->Read(state, id, sub_id, data, size);
}

BOOL CALLBACK CStateInterface::WriteCb(const void* prm, UINT32 id, UINT32 sub_id, const void* data, UINT32 size)
{
	const STATE* state = (const STATE*)prm;
	CStateInterface* _this = reinterpret_cast<CStateInterface*>(state->prm);
	return _this->Write(state, id, sub_id, data, size);
}

// コールバック関数から転送される読み書きの実体
// map使う方がスマートだが、ステートサイズでかくて全部読み込むとメモリ結構食うので…

typedef struct {
	UINT32 id;
	UINT32 sub_id;
	UINT32 size;
} STATEFILE_ITEMHEAD;

BOOL CStateInterface::Read(const STATE* state, UINT32 id, UINT32 sub_id, void* data, UINT32 size)
{
	if ( SetFilePointer(hFile, 0, NULL, FILE_BEGIN) != 0 ) return FALSE;

	do {
		STATEFILE_ITEMHEAD ih;
		DWORD bytes;
		if ( !ReadFile(hFile, &ih, sizeof(ih), &bytes, NULL) ) break;
		if ( bytes != sizeof(ih) ) break;
		if ( ih.id==MAKEBEDWORD(id) && ih.sub_id==MAKEBEDWORD(sub_id) && ih.size==MAKEBEDWORD(size) ) {
			if ( !ReadFile(hFile, data, size, &bytes, NULL) ) break;
			if ( bytes != size ) break;
			return TRUE;
		}
		if ( SetFilePointer(hFile, MAKEBEDWORD(ih.size), NULL, FILE_CURRENT) == INVALID_SET_FILE_POINTER ) break;
	} while ( 1 );

	return FALSE;
}

BOOL CStateInterface::Write(const STATE* state, UINT32 id, UINT32 sub_id, const void* data, UINT32 size)
{
	do {
		STATEFILE_ITEMHEAD ih;
		DWORD bytes;
		memset(&ih, 0, sizeof(ih));
		ih.id     = MAKEBEDWORD(id);
		ih.sub_id = MAKEBEDWORD(sub_id);
		ih.size   = MAKEBEDWORD(size);
		if ( !WriteFile(hFile, &ih, sizeof(ih), &bytes, NULL) ) break;
		if ( bytes != sizeof(ih) ) break;
		if ( !WriteFile(hFile, data, size, &bytes, NULL) ) break;
		if ( bytes != size ) break;
		return TRUE;
	} while ( 0 );

	return FALSE;
}


// ステートセーブ・ロード実体
// 上のインターフェースクラスを使って、ドライバのセーブロードを呼び出す
void CWin32Menu::LoadState(TCHAR* filename)
{
	CStateInterface si;
	UINT32 version = 0;
	char id[32];
	memset(id, 0, sizeof(id));
	si.Open(filename, FALSE);
	si.Read(si.GetState(), MAKESTATEID('W','I','N','M'), MAKESTATEID('S','T','I','D'), id, sizeof(STATE_FILE_ID));
	if ( strncmp(id, STATE_FILE_ID, sizeof(STATE_FILE_ID)) != 0 ) {
		MessageBox(m_hWnd, _T("ステートファイルのIDが一致しないため、\n読み込みに失敗しました。"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
	}
	si.Read(si.GetState(), MAKESTATEID('W','I','N','M'), MAKESTATEID('S','V','E','R'), &version, sizeof(version));
	if ( version != STATE_FILE_VERSION ) {
		MessageBox(m_hWnd, _T("ステートファイルのバージョンが一致しないため、\n読み込みに失敗しました。"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
	}
	si.Read(si.GetState(), MAKESTATEID('W','I','N','M'), MAKESTATEID('I','M','G','0'), m_sImageFile[0], sizeof(m_sImageFile[0]));
	si.Read(si.GetState(), MAKESTATEID('W','I','N','M'), MAKESTATEID('I','M','G','1'), m_sImageFile[1], sizeof(m_sImageFile[1]));
	X68kDriver_LoadState(m_pCore->GetDriver(), si.GetState());
	si.Close();

	TCHAR* name;
	name = _tcsrchr(m_sImageFile[0], '\\');
	mStatBar.SetImageName(0, (name) ? (name+1) : m_sImageFile[0]);
	name = _tcsrchr(m_sImageFile[1], '\\');
	mStatBar.SetImageName(1, (name) ? (name+1) : m_sImageFile[1]);
	mStatBar.Invalidate();
}
void CWin32Menu::LoadState(int no)
{
	CString filename;
	filename.Format(_T("%s%s_state%02d") STATEFILEEXT, m_pCore->GetBasePath(), m_pCore->GetBaseName(), no);
	LoadState(filename.GetBuffer());

}

void CWin32Menu::SaveState(TCHAR* filename)
{
	CStateInterface si;
	UINT32 version = STATE_FILE_VERSION;
	si.Open(filename, TRUE);
	si.Write(si.GetState(), MAKESTATEID('W','I','N','M'), MAKESTATEID('S','T','I','D'), STATE_FILE_ID, sizeof(STATE_FILE_ID));
	si.Write(si.GetState(), MAKESTATEID('W','I','N','M'), MAKESTATEID('S','V','E','R'), &version, sizeof(version));
	si.Write(si.GetState(), MAKESTATEID('W','I','N','M'), MAKESTATEID('I','M','G','0'), m_sImageFile[0], sizeof(m_sImageFile[0]));
	si.Write(si.GetState(), MAKESTATEID('W','I','N','M'), MAKESTATEID('I','M','G','1'), m_sImageFile[1], sizeof(m_sImageFile[1]));
	X68kDriver_SaveState(m_pCore->GetDriver(), si.GetState());
	si.Close();
}
void CWin32Menu::SaveState(int no)
{
	CString filename;
	filename.Format(_T("%s%s_state%02d") STATEFILEEXT, m_pCore->GetBasePath(), m_pCore->GetBaseName(), no);
	SaveState(filename.GetBuffer());
}

void CWin32Menu::OpenStateFile(BOOL is_save)
{
	OPENFILENAME ofn;
	TCHAR filename[MAX_PATH];

	memset(&ofn, 0, sizeof(ofn));
	filename[0] = 0;

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hWnd;
	ofn.lpstrFilter = _T("State Files (*.sav)\0*.sav\0")
					  _T("All Files (*.*)\0*.*\0\0");
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_SHAREAWARE | OFN_EXPLORER;
	ofn.Flags |= ( is_save ) ? OFN_CREATEPROMPT : OFN_FILEMUSTEXIST;
	ofn.lpstrDefExt = _T("sav");
	ofn.lpstrTitle = _T("ステートファイルの選択");

	BOOL is_open = GetOpenFileName(&ofn);

	if ( is_open ) {
		if ( is_save ) {
			SaveState(filename);
		} else {
			LoadState(filename);
		}
	}
}


// --------------------------------------------------------------------------
//   ディスクイメージ関連
// --------------------------------------------------------------------------
void CWin32Menu::ExecDiskEvent()
{
	if ( !mDiskEvent.empty() ) {
		CDiskEvent& ev = *mDiskEvent.begin();
		if ( ev.m_sFile[0] != 0 ) {
			DoSetDisk(ev.m_nDrive, ev.m_sFile);
		} else {
			DoEjectDisk(ev.m_nDrive);
		}
		mDiskEvent.pop_front();
	}
}

void CWin32Menu::SetDisk(UINT32 drive, const TCHAR* file)
{
	mDiskEvent.push_back(CDiskEvent(drive, _T("")));
	mDiskEvent.push_back(CDiskEvent(drive, file));
}

void CWin32Menu::EjectDisk(UINT32 drive)
{
	mDiskEvent.push_back(CDiskEvent(drive, _T("")));
}

void CWin32Menu::OpenDiskImage(UINT32 drive)
{
	OPENFILENAME ofn;
	TCHAR filename[MAX_PATH];

	// lpstrFile にデフォルト文字列を入れると、いつしからかいちいち「作成しますか？」が出るように
	// なったので（高速版とか）、ここでは作成できないようにする
	memset(&ofn, 0, sizeof(ofn));
//	_tcscpy(filename, _T("(新規ディスク作成時はファイル名を入力)"));
	filename[0] = 0;

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hWnd;
	ofn.lpstrFilter = _T("Disk Images (*.xdf/*.img/*.hdm/*.dup/*.2hd/*.dim)\0*.xdf;*.img;*.hdm;*.dup;*.2hd;*.dim\0")
					  _T("All Files (*.*)\0*.*\0\0");
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST/*OFN_CREATEPROMPT*/ | OFN_SHAREAWARE | OFN_EXPLORER;
	ofn.lpstrDefExt = _T("xdf");
	ofn.lpstrTitle = _T("ディスクイメージの選択");

	BOOL is_open = GetOpenFileName(&ofn);

	if ( is_open ) {
		SetDisk(drive, filename);
	}
}

void CWin32Menu::ToUpper(TCHAR* str)
{
	const TCHAR *org = str;
	TCHAR c;
	while ( c=*str ) {
		if ( c>='a' && c<='z' ) {
			*str = c - 'a' + 'A';
		} else if ( c<0 ) {
			str++;
		}
		str++;
	}
//LOGW((_T("ToUpper : %s", org)));
}

X68K_DISK_TYPE CWin32Menu::FindDiskType(const TCHAR* str)
{
	const TCHAR* ext = _tcsrchr(str, '.');
	X68K_DISK_TYPE ret = X68K_DISK_MAX;
	if ( ext ) {
		ext++;
		if ( !_tcsncmp(ext, _T("DIM"), 3) ) {
			ret = X68K_DISK_DIM;
		} else
		if ( !_tcsncmp(ext, _T("XDF"), 3) ) {
			ret = X68K_DISK_XDF;
		} else
		if ( !_tcsncmp(ext, _T("DUP"), 3) ) {
			ret = X68K_DISK_XDF;
		} else
		if ( !_tcsncmp(ext, _T("HDM"), 3) ) {
			ret = X68K_DISK_XDF;
		} else
		if ( !_tcsncmp(ext, _T("IMG"), 3) ) {
			ret = X68K_DISK_XDF;
		}
	}
//LOGW((_T("FindDiskType : %d (%s)"), ret, ext));
	return ret;
}

void CWin32Menu::DoSetDisk(UINT32 drive, const TCHAR* image_file)
{
	TCHAR file[MAX_PATH];
	X68K_DISK_TYPE type;
	UINT8* image = NULL;

	_tcscpy(file, image_file);
	ToUpper(file);
	type = FindDiskType(file);

	if ( type < X68K_DISK_MAX ) {
		UINT32 sz;
		UINT32 attr = 0;
		HANDLE hdl = CreateFile(file, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		do {
			DWORD rbytes;
			if ( hdl==INVALID_HANDLE_VALUE ) break;
			sz = GetFileSize(hdl, NULL);
			if ( sz==INVALID_FILE_SIZE ) break;
			image = (UINT8*)_MALLOC(sz, "DISK image");
			if ( !image ) break;
			ReadFile(hdl, image, sz, &rbytes, NULL);
			CloseHandle(hdl);
			attr = GetFileAttributes(file);
		} while ( 0 );

		if ( image && type>=0 ) {
			X68kDriver_SetDisk(m_pCore->GetDriver(), drive, image, sz, type, (attr&FILE_ATTRIBUTE_READONLY)?TRUE:FALSE);
			_tcscpy(m_sImageFile[drive], image_file);
			TCHAR* name = _tcsrchr(file, '\\');
			mStatBar.SetImageName(drive, (name) ? (name+1) : file);
//LOGW((_T("SetDisk : Drive#%d - %s", drive, gMain.sImageName[drive])));
		}
	}

	if ( image ) _MFREE(image);

	mStatBar.Invalidate();
}

void CWin32Menu::DoEjectDisk(UINT32 drive)
{
	X68kDriver_EjectDisk(m_pCore->GetDriver(), drive, TRUE);  // ここでイジェクトコールバックが呼ばれる

	m_sImageFile[drive][0] = 0;
	mStatBar.SetImageName(drive, _T(""));

	mStatBar.Invalidate();
}

void CWin32Menu::SaveDiskImage(UINT32 drive, const UINT8* ptr, UINT32 sz)
{
	if ( ( m_sImageFile[drive][0] ) && ( ptr ) && ( sz > 0 ) ) {
		UINT32 attr = 0;
		HANDLE hdl = CreateFile(m_sImageFile[drive], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		do {
			DWORD rbytes;
			if ( hdl==INVALID_HANDLE_VALUE ) break;
			WriteFile(hdl, ptr, sz, &rbytes, NULL);
			CloseHandle(hdl);
		} while ( 0 );
	}
}

void CALLBACK CWin32Menu::DiskEjectCallback(void* prm, UINT32 drive, const UINT8* ptr, UINT32 sz)
{
	CWin32Menu* _this = reinterpret_cast<CWin32Menu*>(prm);
	_this->SaveDiskImage(drive, ptr, sz);
}


// --------------------------------------------------------------------------
//   マウス入力処理
// --------------------------------------------------------------------------
void CWin32Menu::MouseCapture(BOOL sw)
{
	RECT rect;
	POINT pt;

	if ( m_bMouseCapture==sw ) return;
	m_bMouseCapture = sw;

	if ( m_bMouseCapture ) {
		// マウスキャプチャ開始
		GetCursorPos(&m_ptMousePos);
		pt.x = 0;
		pt.y = 0;
		ClientToScreen(m_hWnd, &pt);
		SetRect(&rect, pt.x, pt.y, pt.x + X68K_BASE_SCREEN_W, pt.y + X68K_BASE_SCREEN_H);
		ClipCursor(&rect);
		SetCapture(m_hWnd);
		ShowCursor(FALSE);
		pt.x = (X68K_BASE_SCREEN_W / 2);
		pt.y = (X68K_BASE_SCREEN_H / 2);
		ClientToScreen(m_hWnd, &pt);
		SetCursorPos(pt.x, pt.y);
	} else {
		// マウスキャプチャ停止
		ReleaseCapture();
		ShowCursor(TRUE);
		ClipCursor(NULL);
		SetCursorPos(m_ptMousePos.x, m_ptMousePos.y);
	}
}

void CWin32Menu::MouseEvent(WPARAM wParam, LPARAM lParam)
{
	if ( m_bMouseCapture ) {
		int dx, dy, btn = 0;
		dx = LOWORD(lParam) - X68K_BASE_SCREEN_W/2;
		dy = HIWORD(lParam) - X68K_BASE_SCREEN_H/2;
		if ( wParam & MK_LBUTTON ) btn |= X68K_MOUSE_BTN_L;
		if ( wParam & MK_RBUTTON ) btn |= X68K_MOUSE_BTN_R;
		if ( dx || dy ) {
			POINT pt;
			pt.x = (X68K_BASE_SCREEN_W / 2);
			pt.y = (X68K_BASE_SCREEN_H / 2);
			ClientToScreen(m_hWnd, &pt);
			SetCursorPos(pt.x, pt.y);
		}
		// マウス感度（1～200(%)）分乗算して加算
		m_ptMouseMove.x += dx * m_pCfg->mDlgCfg.nMouseSpeed;
		m_ptMouseMove.y += dy * m_pCfg->mDlgCfg.nMouseSpeed;
		// 1/100して送信
		X68kDriver_MouseInput(m_pCore->GetDriver(), m_ptMouseMove.x/100, m_ptMouseMove.y/100, btn);
		// 余り部分を残す
		m_ptMouseMove.x %= 100;
		m_ptMouseMove.y %= 100;
	}
}


