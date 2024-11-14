/* -----------------------------------------------------------------------------------
  Win32 デバッグ用ログウィンドウ
                                                      (c) 2004-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "osconfig.h"

#ifdef _DEBUG_LOG

#pragma comment(lib, "comctl32.lib")

#define	LOGFILENAME     _T("tracelog.txt")

#define	IDC_VIEW        (WM_USER + 100)
#define IDM_LOG_SAVESW  (WM_USER + 110)
#define IDM_LOG_CLEAR   (WM_USER + 111)
#define IDM_LOG_PAUSE   (WM_USER + 112)

#define	VIEW_FGCOLOR    GetSysColor(COLOR_WINDOWTEXT)
#define	VIEW_BGCOLOR    GetSysColor(COLOR_WINDOW)
#define	VIEW_TEXT       _T("ＭＳ ゴシック")
#define	VIEW_SIZE       12

static const TCHAR ProgTitle[] = _T("Trace Log Window");
static const TCHAR ClassName[] = _T("DMKH_DEBUG_WIN");
static const TCHAR* ToolTipStr[] = {
	_T("ファイル保存切替"), _T("ログのクリア"), _T("ログ一時停止")
};

static HINSTANCE hInst = NULL;
static HWND      hWnd = NULL;
static HWND      hView = NULL;
static HWND      hToolBar = NULL;
static HFONT     hfView = NULL;
static HBRUSH    hBrush = NULL;
static int       TBHeight = 0;
static int       FileOut = 0;
static HANDLE    hLogFp = INVALID_HANDLE_VALUE;
static int       LoggedLen = 0;
static int       LogPause = 0;
static TCHAR     szTemp[1024];

static void UpdateMsg(void)
{
	MSG msg;
	while ( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) ) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

static void View_ScrollToBottom(HWND hwnd)
{
	int MinPos, MaxPos;
	GetScrollRange(hwnd, SB_VERT, &MinPos, &MaxPos);
	PostMessage(hwnd, EM_LINESCROLL, 0, MaxPos);
	UpdateMsg();
}

static void View_AddString(const TCHAR *lpszString, BOOL prof)
{
	int len;
    int limit = (int)SendMessage(hView,EM_GETLIMITTEXT,0,0);

	len = (int)_tcslen(lpszString);
	if ( !len ) {
		return;
	}
	if ( hLogFp != INVALID_HANDLE_VALUE ) {
		DWORD wbytes;
		WriteFile(hLogFp, lpszString, len*sizeof(TCHAR), &wbytes, NULL);
		WriteFile(hLogFp, _T("\r\n"), 2*sizeof(TCHAR), &wbytes, NULL);
	}

	_tcscpy(szTemp, lpszString);
	_tcscat(szTemp, _T("\r\n"));

	SendMessage(hView, EM_SETSEL,limit-1,limit-1);
    SendMessage(hView, EM_REPLACESEL, 0, (LPARAM)szTemp);
    SendMessage(hView, EM_SCROLL, SB_LINEDOWN, 0);
	UpdateMsg();
}

static const struct STRUCT_BTNS {
	int bitmapid;
	int commandid;
} Btns[] = {
	{STD_FILESAVE, IDM_LOG_SAVESW},
	{STD_DELETE,   IDM_LOG_PAUSE},
	{6,            0},
	{STD_FILENEW,  IDM_LOG_CLEAR}
};
#define NUM_BUTTONS (sizeof(Btns)/sizeof(struct STRUCT_BTNS))
static HWND SetupToolBar(HWND hwndParent) 
{ 
	HWND hTB;
	TBBUTTON tbb[NUM_BUTTONS];
	TBADDBITMAP tbab;
	INITCOMMONCONTROLSEX icex;
	int i;
    
	icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
	icex.dwICC  = ICC_BAR_CLASSES;
	InitCommonControlsEx(&icex);

	hTB = CreateWindowEx(0, TOOLBARCLASSNAME, NULL,
		WS_CHILD | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_ADJUSTABLE,
		0, 0, 0, 0, hwndParent, 0, hInst, NULL);

	SendMessage(hTB, TB_BUTTONSTRUCTSIZE, (WPARAM) sizeof(TBBUTTON), 0); 

	tbab.hInst = HINST_COMMCTRL;
	tbab.nID = IDB_STD_SMALL_COLOR;
	SendMessage(hTB, TB_ADDBITMAP, 0, (LPARAM)&tbab); 

	for (i=0; i<NUM_BUTTONS; i++) {
		tbb[i].iBitmap = Btns[i].bitmapid;
		tbb[i].idCommand = Btns[i].commandid;
		tbb[i].fsState = TBSTATE_ENABLED;
		tbb[i].fsStyle = ((Btns[i].commandid)?TBSTYLE_BUTTON:TBSTYLE_SEP);
		tbb[i].dwData = 0;
		tbb[i].iString = -1;
	}

	SendMessage(hTB, TB_ADDBUTTONS, (WPARAM)NUM_BUTTONS, (LPARAM)&tbb);
	SendMessage(hTB, TB_AUTOSIZE, 0, 0);
	ShowWindow(hTB, SW_SHOWNORMAL);

	return hTB;
}


static void OpenLogFile(void)
{
	TCHAR buf[MAX_PATH], file[MAX_PATH], *filepart;
	if ( hLogFp == INVALID_HANDLE_VALUE ) {
		GetModuleFileName(NULL, buf, MAX_PATH);
		GetFullPathName(buf, MAX_PATH, file, &filepart);
		*filepart = 0;
		_tcscat(file, LOGFILENAME);
		hLogFp = CreateFile(file, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		SetFilePointer(hLogFp, 0, NULL, FILE_END);
	}
}


static void CloseLogFile(void)
{
	if ( hLogFp != INVALID_HANDLE_VALUE ) {
		CloseHandle(hLogFp);
		hLogFp = INVALID_HANDLE_VALUE;
	}
}


static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	RECT rc, rctb;
	long w, h;
	switch (msg) {
		case WM_CREATE:
			hToolBar = SetupToolBar(hwnd);
			GetClientRect(hToolBar, &rctb);
			TBHeight = rctb.bottom;
			GetClientRect(hwnd, &rc);
			hView = CreateWindowEx(WS_EX_CLIENTEDGE,
							_T("EDIT"), NULL,
							WS_CHILD | WS_VISIBLE | ES_READONLY | ES_LEFT |
							ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL,
							0, TBHeight, rc.right, rc.bottom-TBHeight,
							hwnd, (HMENU)IDC_VIEW, hInst, NULL);
			if ( !hView ) break;
			SendMessage(hView, EM_SETLIMITTEXT, (WPARAM)0, 0);

			hfView = CreateFont(VIEW_SIZE, 0, 0, 0, 0, 0, 0, 0, 
					SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					DEFAULT_QUALITY, FIXED_PITCH, VIEW_TEXT);
			if ( !hfView ) break;
			SendMessage(hView, WM_SETFONT, (WPARAM)hfView, MAKELPARAM(TRUE, 0));
			hBrush = CreateSolidBrush(VIEW_BGCOLOR);
			SetFocus(hView);
			return TRUE;

		case WM_MOVE:
			if ( !(GetWindowLong(hwnd, GWL_STYLE)&(WS_MAXIMIZE|WS_MINIMIZE)) ) {
				GetWindowRect(hwnd, &rc);
			}
			break;

		case WM_SIZE:							// window resize
			w = LOWORD(lp);
			h = HIWORD(lp);
			MoveWindow(hView, 0, TBHeight, w, h-TBHeight, TRUE);
			View_ScrollToBottom(hView);
			break;

		case WM_SETFOCUS:
			SetFocus(hView);
			return(0L);

		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLOREDIT:
			SetTextColor((HDC)wp, VIEW_FGCOLOR);
			SetBkColor((HDC)wp, VIEW_BGCOLOR);
			return((LRESULT)hBrush);

		case WM_CLOSE:
			break;

		case WM_DESTROY:
			if (hBrush) {
				DeleteObject(hBrush);
			}
			if (hfView) {
				DeleteObject(hfView);
			}
			CloseLogFile();
			break;

		case WM_NOTIFY:
			switch (((LPNMHDR)lp)->code) {
				case TTN_GETDISPINFO:
				{
					LPTOOLTIPTEXT lpttt = (LPTOOLTIPTEXT)lp; 
					lpttt->hinst = hInst;
	                lpttt->lpszText = (TCHAR*)ToolTipStr[lpttt->hdr.idFrom-IDM_LOG_SAVESW];
					break; 
				} 
				default: 
					break; 
			}
			break; 

		case WM_COMMAND:
			switch ( LOWORD(wp) ) {
				case IDM_LOG_CLEAR:
					SetWindowText(hView, _T(""));
					break;
				case IDM_LOG_SAVESW:
					FileOut = !FileOut;
					SendMessage(hToolBar, TB_SETSTATE, (WPARAM)IDM_LOG_SAVESW,
					            MAKELONG( (TBSTATE_ENABLED|((FileOut)?TBSTATE_CHECKED:0)), 0 ) ); 
					if ( FileOut )
						OpenLogFile();
					else
						CloseLogFile();
					break;
				case IDM_LOG_PAUSE:
					LogPause = !LogPause;
					SendMessage(hToolBar, TB_SETSTATE, (WPARAM)IDM_LOG_PAUSE,
					            MAKELONG( (TBSTATE_ENABLED|((LogPause)?TBSTATE_CHECKED:0)), 0 ) ); 
					break;
			}
			break; 

		default:
			return(DefWindowProc(hwnd, msg, wp, lp));
	}
	return(0L);
}


void Trace_Init(HWND hwnd, HINSTANCE hinst, HINSTANCE hprev)
{
	hInst = hinst;
	if ( !hprev ) {
		WNDCLASS wc;
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = WndProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = hInst;
		wc.hIcon = NULL;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)GetSysColorBrush(COLOR_MENU);
		wc.lpszMenuName = NULL;
		wc.lpszClassName = (TCHAR*)ClassName;
		if (!RegisterClass(&wc)) {
			return;
		}
	}
	hWnd = CreateWindowEx(WS_EX_CONTROLPARENT,
			ClassName, ProgTitle, WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, 640, 240,
			hwnd, NULL, hInst, NULL);
	if ( !hWnd ) return;
	ShowWindow(hWnd, SW_SHOW);
	UpdateWindow(hWnd);
}


void Trace_Clean(void)
{
	if ( hWnd ) {
		DestroyWindow(hWnd);
		hWnd = NULL;
	}
}

static TCHAR sTraceBuf[0x1000];
static void TraceOutBase(void)
{
	if ( hView ) {
		TCHAR *c = sTraceBuf, *p;
		p = c;
		while ( *c ) {
			if ( *c==0x0a ) {
				*c = 0;
				View_AddString(p, FALSE);
				p = (c+1);
			}
			c++;
		}
		if ( p!=c ) View_AddString(p, FALSE);
	}
}

void Trace_Out(const char *fmt, ...)
{
	if ( LogPause ) return;
	if ( hView ) {
#ifdef UNICODE
		char tmp[0x1000];
		va_list ap;
		va_start(ap, fmt);
		vsprintf(tmp, fmt, ap);
		va_end(ap);
		MultiByteToWideChar(CP_ACP, 0, tmp, -1, sTraceBuf, 0x1000);
#else
		va_list ap;
		va_start(ap, fmt);
		_vstprintf_s(sTraceBuf, 0x1000, fmt, ap);
		va_end(ap);
		TraceOutBase();
#endif
		TraceOutBase();
	}
}

void TraceW_Out(const TCHAR *fmt, ...)
{
	if ( LogPause ) return;
	if ( hView ) {
		va_list ap;
		va_start(ap, fmt);
		_vstprintf_s(sTraceBuf, 0x1000, fmt, ap);
		va_end(ap);
		TraceOutBase();
	}
}

#endif //_DEBUG_LOG
