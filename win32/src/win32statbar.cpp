/* -----------------------------------------------------------------------------------
  Win32ステータスバー処理
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	実際には「ステータスバーもどき」な自前ウィンドウを作って自力描画するクラス
*/

#include "win32statbar.h"
#include "x68000_driver.h"
#include <atlstr.h>

#define STATBAR_HEIGHT          22
#define STATBAR_HORIZONTAL_PAD  5
#define STATBAR_DRIVE_WIDTH     300
#define STATBAR_CRTINFO_WIDTH   100
#define STATBAR_HDDINFO_WIDTH   40


CWin32StatBar::CWin32StatBar()
{
	m_hMainWnd = NULL;
	m_hWnd = NULL;

	m_nHeight = STATBAR_HEIGHT;//rectStat.bottom-rectStat.top;
	m_nColorSet = STATBAR_COLORSET_NORMAL;
	memset(&m_stLedState, 0, sizeof(m_stLedState));

	m_sImageName[0][0] = 0;
	m_sImageName[1][0] = 0;

	m_fHSync = 0.0f;
	m_fVSync = 0.0f;

	m_bShow = TRUE;
}

CWin32StatBar::~CWin32StatBar()
{
}

void CWin32StatBar::Init(HWND hwnd)
{
	static const UINT32 COLOR_TABLE_BASE[STATBAR_COLORSET_MAX][STATBAR_COLOR_MAX] =
	{
		/*   BK       FONT    LED OFF    GREEN      RED   */
		{ 0xFFFFFF, 0x404040, 0x404040, 0x00C000, 0x0000E0 },  // NORMAL
		{ 0x303030, 0xA0A0A0, 0x000000, 0x00C000, 0x0000E0 }   // FULL SCREEN
	};

	m_hMainWnd = hwnd;
	m_hWnd = CreateWindowEx(0, _T("STATIC"), _T("DUMMY"), WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 100, STATBAR_HEIGHT, m_hMainWnd, 0, 0, 0);

	// COLOR_3DFACE は起動時に動的に設定
	for (UINT32 i=0; i<STATBAR_COLORSET_MAX; i++) {
		memcpy(m_nColorTable[i], COLOR_TABLE_BASE[i], sizeof(UINT32)*STATBAR_COLOR_MAX);
	}
	m_nColorTable[STATBAR_COLORSET_NORMAL][STATBAR_COLOR_BK] = GetSysColor(COLOR_3DFACE);

	UpdatePos();
}

void CWin32StatBar::UpdatePos()
{
	RECT rectMain;
	int heightMain;
	GetClientRect(m_hMainWnd, &rectMain);
	heightMain = rectMain.bottom - rectMain.top;
	MoveWindow(m_hWnd, 0, heightMain-m_nHeight, rectMain.right-rectMain.left, heightMain, TRUE);
}

void CWin32StatBar::Invalidate()
{
	if ( !m_bShow ) return;
	// テキストを更新してやれば  WM_DRAWITEM が飛ぶ
	SetWindowText(m_hWnd, _T(""));
}

void CWin32StatBar::Show(BOOL sw)
{
	m_bShow = sw;
	if ( m_bShow ) {
		UpdatePos();
	}
	ShowWindow(m_hWnd, m_bShow);
}

void CWin32StatBar::CheckDriveStatus(EMUDRIVER* drv, BOOL has_hdd)
{
	if ( !m_bShow ) return;

	if ( drv ) {
		const INFO_X68FDD_LED* pNewLED = X68kDriver_GetDriveLED(drv);
		if ( memcmp(&m_stLedState, pNewLED, sizeof(m_stLedState)) ) {
			memcpy(&m_stLedState, pNewLED, sizeof(m_stLedState));
			Invalidate();
		}

		X68FDD_LED_STATE hdd = ( has_hdd ) ? X68kDriver_GetHddLED(drv) : X68FDD_LED_OFF;
		if ( m_nHddLed != hdd ) {
			m_nHddLed = hdd;
			Invalidate();
		}

		float hsync = X68kDriver_GetHSyncFreq(drv);
		float vsync = X68kDriver_GetVSyncFreq(drv);
		if ( m_fHSync!=hsync || m_fVSync!=vsync ) {
			m_fHSync = hsync;
			m_fVSync = vsync;
			Invalidate();
		}
	}
}

void CWin32StatBar::Draw(DRAWITEMSTRUCT* dis)
{
	if ( !m_bShow ) return;

	if ( dis->hwndItem == m_hWnd ) {
		const UINT32* DRAW_COLORS = m_nColorTable[m_nColorSet];
		HBRUSH brs, obrs;
		brs = CreateSolidBrush(DRAW_COLORS[STATBAR_COLOR_BK]);
		obrs = (HBRUSH)SelectObject(dis->hDC, brs);
		Rectangle(dis->hDC, dis->rcItem.left-1, dis->rcItem.top-1, dis->rcItem.right+1, dis->rcItem.bottom+1);
		SelectObject(dis->hDC, obrs);
		DeleteObject(brs);
		DrawDriveInfo(dis, 0);
		DrawDriveInfo(dis, 1);
		DrawHddInfo(dis);
		DrawCrtInfo(dis);
	}
}

void CWin32StatBar::SelectColorset(STATBAR_COLORSET n)
{
	m_nColorSet = n;
}

void CWin32StatBar::SetImageName(UINT32 drive, const TCHAR* name)
{
	if ( drive < 2 ) {
		_tcscpy(m_sImageName[drive], name);
	}
}


void CWin32StatBar::DrawDriveInfo(DRAWITEMSTRUCT* dis, UINT32 drive)
{
	const UINT32* DRAW_COLORS = m_nColorTable[m_nColorSet];
	const UINT32* LED_COLORS = DRAW_COLORS + STATBAR_COLOR_LED_OFF;
	UINT32 x =  STATBAR_HORIZONTAL_PAD + STATBAR_DRIVE_WIDTH*drive;
	UINT32 col_eject = LED_COLORS[m_stLedState.eject[drive]];
	UINT32 col_access = LED_COLORS[m_stLedState.access[drive]];
	BOOL inserted = m_stLedState.inserted[drive];
	HFONT f1, f2, ofont;
	HBRUSH b1, b2, obrs;
	HPEN p1, p2, open;
	RECT r;

	// ファイル名の表示領域制限用
	SetRect(&r, dis->rcItem.left+x, dis->rcItem.top, dis->rcItem.left+x+STATBAR_DRIVE_WIDTH-28, dis->rcItem.bottom);

	// くぼみのエッジ描画
	if ( m_nColorSet == STATBAR_COLORSET_NORMAL ) {
		RECT r_edge;
		SetRect(&r_edge, dis->rcItem.left+x-STATBAR_HORIZONTAL_PAD, dis->rcItem.top+1, dis->rcItem.left+x+STATBAR_DRIVE_WIDTH-STATBAR_HORIZONTAL_PAD-2, dis->rcItem.top+STATBAR_HEIGHT-1);
		DrawEdge(dis->hDC, &r_edge, BDR_SUNKENOUTER/*|BDR_SUNKENINNER*/, BF_RECT);
	}

	// イメージファイル名
	f1 = CreateFont(10, 5, 0, 0, FW_THIN, 0, 0, 0, SHIFTJIS_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY, FIXED_PITCH|FF_DONTCARE, _T("Terminal"));
	f2 = CreateFont(12, 6, 0, 0, FW_THIN, 0, 0, 0, SHIFTJIS_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY, FIXED_PITCH|FF_DONTCARE, _T("ＭＳ ゴシック"));
	SetBkColor(dis->hDC, DRAW_COLORS[STATBAR_COLOR_BK]);
	SetTextColor(dis->hDC, DRAW_COLORS[STATBAR_COLOR_FONT]);
	ofont = (HFONT)SelectObject(dis->hDC, f1);
	TextOut(dis->hDC, dis->rcItem.left+x,    dis->rcItem.top+4, (drive)?_T("1"):_T("0"), 1);
	if ( inserted ) {
		SelectObject(dis->hDC, f2);
		ExtTextOut(dis->hDC, dis->rcItem.left+x+22, dis->rcItem.top+6, ETO_CLIPPED, &r, m_sImageName[drive], (int)_tcslen(m_sImageName[drive]), NULL);
	}
	SelectObject(dis->hDC, ofont);
	DeleteObject(f1);
	DeleteObject(f2);

	// アクセスランプ・イジェクトランプ
	b1 = CreateSolidBrush(col_eject);
	b2 = CreateSolidBrush(col_access);
	p1 = CreatePen(PS_SOLID, 1, col_eject);
	p2 = CreatePen(PS_SOLID, 1, col_access);
	obrs = (HBRUSH)SelectObject(dis->hDC, b1);
	open = (HPEN)SelectObject(dis->hDC, p1);
	Rectangle(dis->hDC, x+10, dis->rcItem.top+15, x+16, dis->rcItem.top+17);
	SelectObject(dis->hDC, b2);
	SelectObject(dis->hDC, p2);
	Ellipse(dis->hDC, x+10, dis->rcItem.top+5, x+16, dis->rcItem.top+11);
	SelectObject(dis->hDC, obrs);
	SelectObject(dis->hDC, open);
	DeleteObject(b1);
	DeleteObject(b2);
	DeleteObject(p1);
	DeleteObject(p2);
}

void CWin32StatBar::DrawCrtInfo(DRAWITEMSTRUCT* dis)
{
	const UINT32* DRAW_COLORS = m_nColorTable[m_nColorSet];
	HFONT font, ofont;
	CString s;

	s.Format(_T("%2dkHz / %2.2fHz"), (SINT32)(m_fHSync/1000), m_fVSync);

	// くぼみのエッジ描画
	if ( m_nColorSet == STATBAR_COLORSET_NORMAL ) {
		RECT r_edge;
		SetRect(&r_edge, dis->rcItem.right-STATBAR_CRTINFO_WIDTH-2 , dis->rcItem.top+1, dis->rcItem.right, dis->rcItem.top+STATBAR_HEIGHT-1);
		DrawEdge(dis->hDC, &r_edge, BDR_SUNKENOUTER/*|BDR_SUNKENINNER*/, BF_RECT);
	}

	font = CreateFont(12, 6, 0, 0, FW_THIN, 0, 0, 0, SHIFTJIS_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY, FIXED_PITCH|FF_DONTCARE, _T("ＭＳ ゴシック"));
	SetBkColor(dis->hDC, DRAW_COLORS[STATBAR_COLOR_BK]);
	SetTextColor(dis->hDC, DRAW_COLORS[STATBAR_COLOR_FONT]);
	ofont = (HFONT)SelectObject(dis->hDC, font);
	TextOut(dis->hDC, dis->rcItem.right-STATBAR_CRTINFO_WIDTH-2+6, dis->rcItem.top+6, s.GetBuffer(), (int)_tcslen(s));
	SelectObject(dis->hDC, ofont);
	DeleteObject(font);
}

void CWin32StatBar::DrawHddInfo(DRAWITEMSTRUCT* dis)
{
	const UINT32* DRAW_COLORS = m_nColorTable[m_nColorSet];
	HFONT font, ofont;
	HBRUSH brs, obrs;
	HPEN pen, open;
	CString s = _T("HDD");
	SINT32 x = dis->rcItem.right-STATBAR_CRTINFO_WIDTH-STATBAR_HDDINFO_WIDTH-4;

	// くぼみのエッジ描画
	if ( m_nColorSet == STATBAR_COLORSET_NORMAL ) {
		RECT r_edge;
		SetRect(&r_edge, x , dis->rcItem.top+1, x+STATBAR_HDDINFO_WIDTH, dis->rcItem.top+STATBAR_HEIGHT-1);
		DrawEdge(dis->hDC, &r_edge, BDR_SUNKENOUTER/*|BDR_SUNKENINNER*/, BF_RECT);
	}

	font = CreateFont(12, 6, 0, 0, FW_THIN, 0, 0, 0, SHIFTJIS_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY, FIXED_PITCH|FF_DONTCARE, _T("ＭＳ ゴシック"));
	SetBkColor(dis->hDC, DRAW_COLORS[STATBAR_COLOR_BK]);
	SetTextColor(dis->hDC, DRAW_COLORS[STATBAR_COLOR_FONT]);
	ofont = (HFONT)SelectObject(dis->hDC, font);
	TextOut(dis->hDC, x+18, dis->rcItem.top+6, s, (int)_tcslen(s));
	SelectObject(dis->hDC, ofont);
	DeleteObject(font);

	brs = CreateSolidBrush(DRAW_COLORS[m_nHddLed+2]);
	pen = CreatePen(PS_SOLID, 1, DRAW_COLORS[m_nHddLed+2]);
	obrs = (HBRUSH)SelectObject(dis->hDC, brs);
	open = (HPEN)SelectObject(dis->hDC, pen);
//	Ellipse(dis->hDC, x+7, dis->rcItem.top+8, x+13, dis->rcItem.top+14);
	Rectangle(dis->hDC, x+6, dis->rcItem.top+9, x+14, dis->rcItem.top+13);
	SelectObject(dis->hDC, obrs);
	SelectObject(dis->hDC, open);
	DeleteObject(brs);
	DeleteObject(pen);
}
