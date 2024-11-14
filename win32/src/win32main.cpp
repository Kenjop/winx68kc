/* -----------------------------------------------------------------------------------
  X68000�G�~�� Windows���C��
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "win32core.h"
#include "win32menu.h"
#include "win32config.h"
#include "x68000_driver.h"
#include "resource.h"
#include <atlstr.h>

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shlwapi.lib")

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")


// --------------------------------------------------------------------------
//   ������`�E���[�J���ϐ�
// --------------------------------------------------------------------------

typedef struct {
	HINSTANCE      hInst;
	HWND           hWnd;
	HMENU          hMenu;
	UINT_PTR       hTimer;

	BOOL           bActive;
	BOOL           bFullScreenMenu;
	DWORD          dwFrameLast;

	CWin32Config   mCfg;
	CWin32Core     mCore;
	CWin32Menu     mWinMenu;
} ST_MAININFO;

static ST_MAININFO sInfo;


// --------------------------------------------------------------------------
//   �f�B�X�N�C���[�W�i�E�B���h�E�ւ̃h���b�v���j
// --------------------------------------------------------------------------
void DropDisk(HANDLE h)
{
	UINT32 i;
	UINT32 cnt = DragQueryFile((HDROP)h, 0xFFFFFFFF, NULL, 0);
	if ( cnt == 1 ) {
		// XXX �X�e�[�^�X�o�[�ゾ������A�h���C�u���\���ʒu���l�����ăh���C�u�����肵�����������I
		TCHAR file[MAX_PATH];
		RECT r;
		POINT pt;
		UINT32 drv = 0;
		GetClientRect(sInfo.hWnd, &r);
		DragQueryPoint((HDROP)h, &pt);
		if ( pt.x > ((r.right-r.left)/2) ) drv = 1;
		file[0] = 0;
		if ( DragQueryFile((HDROP)h, 0, file, MAX_PATH) ) {
			sInfo.mWinMenu.SetDisk(drv, file);
		}
	} else {
		// XXX �����h���b�v���̎����h���C�u�����܂�
		for (i=0; i<2; i++) {
			TCHAR file[MAX_PATH];
			file[0] = 0;
			if ( DragQueryFile((HDROP)h, i, file, MAX_PATH) ) {
				sInfo.mWinMenu.SetDisk(i, file);
			}
		}
	}
}


// --------------------------------------------------------------------------
//   �C�x���g�v���V�[�W��
// --------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		case WM_DESTROY:
			PostQuitMessage(0);
			break;

		case WM_COMMAND:
			sInfo.mWinMenu.MenuProc(wParam, lParam);
			break;

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			if ( wParam == VK_SPACE ) {
				if ( sInfo.mCore.GetPauseState() ) {
					sInfo.mCore.ExecStep();
					// �X�e�b�v���s�w���̃X�y�[�X�L�[�̓R�A�ɗ����Ȃ�
					break;
				}
			}
			sInfo.mCore.KeyPress(wParam, lParam);
			break;

		case WM_KEYUP:
		case WM_SYSKEYUP:
			sInfo.mCore.KeyRelease(wParam, lParam);
			break;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
			sInfo.mWinMenu.MouseEvent(wParam, lParam);
			break;
		case WM_MOUSEMOVE:
			sInfo.mWinMenu.MouseEvent(wParam, lParam);
			if ( sInfo.mCfg.bFullScreen ) {
				SINT32 x = LOWORD(lParam), y = HIWORD(lParam);
				if ( y < 16 ) {
					if ( !sInfo.bFullScreenMenu ) {
						SetMenu(sInfo.hWnd, sInfo.hMenu);
						sInfo.bFullScreenMenu = TRUE;
					}
				} else {
					if ( sInfo.bFullScreenMenu ) {
						SetMenu(sInfo.hWnd, NULL);
						sInfo.bFullScreenMenu = FALSE;
					}
				}
			}
			break;

		case WM_MBUTTONDOWN:
			sInfo.mWinMenu.ToggleMouseCapture();
			break;

		case WM_SIZE:
			{
                UINT w = LOWORD(lParam);
                UINT h = HIWORD(lParam);
				D3DDraw_UpdateWindowSize(sInfo.mCore.GetDrawHandle(), w, h);
				sInfo.mWinMenu.UpdateStatPos();
			}
			break;

		case WM_DRAWITEM:
			sInfo.mWinMenu.DrawStatBar((DRAWITEMSTRUCT*)lParam);
			return TRUE;

		case WM_ACTIVATE:
			switch ( wParam ) {
				case WA_INACTIVE:
					sInfo.bActive = FALSE;
					sInfo.mCore.KeyClear();
					break;
				case WA_ACTIVE:
				case WA_CLICKACTIVE:
					sInfo.bActive = TRUE;
					break;
			}
			break;

		case WM_DROPFILES:
			DropDisk((HANDLE)wParam);
			break;

		default:
			return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0L;
}



// --------------------------------------------------------------------------
//   �����֐�
// --------------------------------------------------------------------------
// �E�B���h�E�N���X�o�^
static BOOL InitWndClass(HINSTANCE hInst, HINSTANCE hPreInst)
{
	WNDCLASS wc;
	HWND hwnd;

	// ���d�N���̖h�~����
	if ( (hwnd=FindWindow(__SYS_APPNAME__, NULL))!=NULL ) {
		ShowWindow(hwnd, SW_RESTORE);
		SetForegroundWindow(hwnd);
		return FALSE;
	}

	// �E�B���h�E�N���X�o�^
	if ( !hPreInst ) {
		wc.style         = CS_BYTEALIGNCLIENT | CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc   = WndProc;
		wc.cbClsExtra    = 0;
		wc.cbWndExtra    = 0;
		wc.hInstance     = hInst;
		wc.hIcon         = LoadIcon(hInst, MAKEINTRESOURCE(IDI_MAINICON));
		wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wc.lpszMenuName  = MAKEINTRESOURCE(IDR_MAINMENU);
		wc.lpszClassName = __SYS_APPNAME__;
		if ( !RegisterClass(&wc) ) {
			return FALSE;
		}
	}
	return TRUE;
}

// ���s���x�\��
void CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	float clocks = (float)sInfo.mCore.GetExecutedClocks();
	float frames = (float)sInfo.mCore.GetExecutedFrames();
	if ( sInfo.dwFrameLast != 0 ) {  // ����͖�������idt������ɏo�Ȃ��̂Łj
		CString str;
		float dt = (float)(dwTime-sInfo.dwFrameLast);
		if ( dt > 0.0f ) {
			switch ( sInfo.mCfg.mDlgCfg.nTitleBarInfo )
			{
			default:
			case 0:
				str.Format(__SYS_APPTITLE__);
				break;
			case 1:
				{
				float clks = ( clocks / dt ) / 1000.0f;
				str.Format(__SYS_APPTITLE__ _T(" / %.02fMHz"), clks);
				}
				break;
			case 2:
				{
				static const UINT32 CLOCK_TABLE[] = { 10000000, 16666667, 24000000 };  // XXX �h���C�o����擾��������]�܂�����
				float clks = ( (clocks*1000.0f*100.0f) / dt );
				float base = (float)CLOCK_TABLE[sInfo.mCfg.nCpuClock];
				str.Format(__SYS_APPTITLE__ _T(" / %.01f%%"), clks/base);
				}
				break;
			case 3:
				{
				float frms = ( (frames*1000.0f) / dt );
				str.Format(__SYS_APPTITLE__ _T(" / %.02ffps"), frms);
				}
				break;
			}
			SetWindowText(hwnd, str.GetBuffer());
		}
	}
	sInfo.dwFrameLast = dwTime;
}


// --------------------------------------------------------------------------
//   ���C���G���g���|�C���g
// --------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPreInst, LPSTR lpszCmdLine, int nCmdShow)
{
	MSG msg;
	HIMC hIMC;
	HACCEL hAcc;

	// �������N���A
	memset(&msg, 0, sizeof(msg));
	memset(&sInfo, 0, offsetof(ST_MAININFO,mCfg));

	// ���d�N���h�~�ƃE�B���h�N���X�쐬
	if ( !InitWndClass(hInst, hPreInst) ) return FALSE;
	sInfo.hInst = hInst;

	CoInitialize(NULL);

	// �E�B���h�E�쐬
	sInfo.hWnd = CreateWindowEx(0,
			__SYS_APPNAME__, __SYS_APPTITLE__,
			(WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN) & (~(WS_MAXIMIZEBOX)),
			CW_USEDEFAULT, CW_USEDEFAULT,
			X68K_BASE_SCREEN_W, X68K_BASE_SCREEN_H,
			NULL, NULL, hInst, NULL);

	// �E�B���h�E��{�ݒ�
	sInfo.hMenu = GetMenu(sInfo.hWnd);
	hIMC = ImmAssociateContext(sInfo.hWnd, 0);
	hAcc = LoadAccelerators(hInst, MAKEINTRESOURCE(IDR_MAINACCEL));
	DragAcceptFiles(sInfo.hWnd, TRUE);
	timeBeginPeriod(1);

	// �f�o�b�O�֘A�E�B���h�E�̏���
	LOGSTART(NULL, hInst, hPreInst);
	_MEMTRACE_INIT(hInst, hPreInst);

	// ���[�U�[�ݒ�ǂݍ���
	sInfo.mCfg.Init();
	sInfo.mCfg.Load();

	// ���z�}�V���֘A�̏�����
	sInfo.mCore.Init(sInfo.hWnd, &sInfo.mCfg);
	if ( !sInfo.mCore.LoadRoms() ) goto err_end;
	if ( !sInfo.mCore.InitEmulator() ) goto err_end;

	// �E�B���h�E�\��
	sInfo.mWinMenu.Init(hInst, sInfo.hWnd, sInfo.hMenu, &sInfo.mCfg, &sInfo.mCore);
	sInfo.mWinMenu.SetupMenu();
	ShowWindow(sInfo.hWnd, SW_SHOW);

	// �N�����t���X�N���[���Ȃ�K�p
	sInfo.mWinMenu.SetFullScreen(sInfo.mCfg.bFullScreen);

	// ���X�V�n�^�C�}���N��
	sInfo.hTimer = SetTimer(sInfo.hWnd, 0, 3000, TimerProc);

	// �R�A�̃t���[���Ԋu�^�C�}��������
	sInfo.mCore.RunTimerReset();

	// �A�N�e�B�u��
	sInfo.bActive = TRUE;

	// ���b�Z�[�W�|���v
	while ( msg.message != WM_QUIT )
	{
		double next = 0;

		// �L���[���̃��b�Z�[�W������
		if ( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) )
		{
			if ( !TranslateAccelerator(msg.hwnd, hAcc, &msg) ) {
				TranslateMessage(&msg);
			}
			DispatchMessage(&msg);
		}
		else
		{
			// �R�A���s
			next = sInfo.mCore.Run();
			// ���̑��̃t���[������
			if ( sInfo.mCore.IsExecuted() ) {
				sInfo.mWinMenu.ExecDiskEvent();
				sInfo.mWinMenu.UpdateStatInfo();
			}
		}

		// ���̎��s�܂�1ms�ȏ�c���Ă�Ȃ� Sleep(1)�A�Ȃ��Ȃ� Sleep(0)
		Sleep( ( next > 1000.0 ) ? 1 : 0 );
	}

	// ��A�N�e�B�u��
	sInfo.bActive = FALSE;

	// ���X�V�^�C�}�j��
	KillTimer(sInfo.hWnd, sInfo.hTimer);

err_end:
	// ���z�}�V���֘A�̔j��
	sInfo.mCore.CleaupEmulator();

	// ���[�U�[�ݒ�ۑ�
	sInfo.mCfg.Save();

	// �f�o�b�O�֘A�E�B���h�E�̔j��
	LOGEND();
	_MEMTRACE_END();

	// �������K�v�Ȃ��߂�
	ImmAssociateContext(sInfo.hWnd, hIMC);
	timeEndPeriod(1);

	CoUninitialize();

	// �f�o�b�O�p���������[�N�`�F�b�N
	_CrtDumpMemoryLeaks();

	return (int)msg.wParam;
}
