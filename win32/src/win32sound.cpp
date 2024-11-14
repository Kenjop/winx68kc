/* -----------------------------------------------------------------------------------
  DirectSound Streaming
                                                      (c) 2004-07 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "osconfig.h"
#include <mmsystem.h>
#include <mmreg.h>
#include <dsound.h>
#include "win32sound.h"

#pragma comment(lib, "dxguid.lib")


// --------------------------------------------------------------------------
//   カスタム定義
// --------------------------------------------------------------------------
#define DSBUFFERMSEC  100  // 総バッファ長（ms）
#define BUFFERCNT     2    // バッファ数（1バッファ当たりの長さは、総バッファ長／バッファ数になる）
#define USE_DSHWBUFFER
//#define FORCEDX8


// --------------------------------------------------------------------------
//   内部定義・ローカル変数
// --------------------------------------------------------------------------
#define SAFE_RELEASE(p)  if ( p ) { (p)->Release(); (p)=NULL; }
#define VOLUME_FULL       0L
#define VOLUME_SILENCE    -10000L

#ifdef FORCEDX8
typedef HRESULT(WINAPI * DIRECTSOUNDCREATE)(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);
#else
typedef HRESULT(WINAPI * DIRECTSOUNDCREATE)(LPCGUID, LPDIRECTSOUND*, LPUNKNOWN);
#endif

typedef struct {
	DIRECTSOUNDCREATE   pDSCreate;
	HINSTANCE           hDSoundDLL;

#ifdef FORCEDX8
	LPDIRECTSOUND8      pDS;               // DirectSound8 Object
#else
	LPDIRECTSOUND       pDS;               // DirectSound Object
#endif
	LPDIRECTSOUNDBUFFER pDSB;              // DS Buffer
	DWORD               DSBSize;           // 総バッファ長（in byte）
	DWORD               DSBUnit;           // 1ユニットの長さ（in byte）
	DWORD               DSBBlk;            // 1バッファ長（in byte）
	HANDLE              hEvent[BUFFERCNT]; // DS Notifyイベント通知用
	HANDLE              hThread;           // サウンドバッファ埋め用スレッド
	DWORD               ThreadID;

	CRITICAL_SECTION    CritCs;
	void                (CALLBACK *GetPCM)(void*, short*, unsigned int);
	void*               GetPCMPrm;
	int                 volume;
} ST_DSINFO;


// --------------------------------------------------------------------------
//   内部関数
// --------------------------------------------------------------------------
static DWORD WaitEvent(HANDLE h, DWORD time)
{
	DWORD ret = WAIT_TIMEOUT;
	BOOL infinite = FALSE;

	if ( time==INFINITE ) {
		time = 10;
		infinite = TRUE;
	}

	do {
		MSG msg;
		while ( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) ) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		ret = WaitForSingleObject(h, time);
	} while ( (infinite)&&(ret==WAIT_TIMEOUT) );
	return ret;
}


// DSバッファのRestore
static int RestoreBuffer(ST_DSINFO* info, BOOL* flag)
{
	HRESULT hr;
	DWORD stat;
	if ( !info->pDSB ) return 0;
	if ( flag ) *flag = FALSE;
	if ( FAILED(hr=info->pDSB->GetStatus(&stat)) ) return 0;
	if( stat&DSBSTATUS_BUFFERLOST ) {
		// ロストしてる
		do {
			hr = info->pDSB->Restore();
			if ( hr==DSERR_BUFFERLOST ) Sleep(10);
		} while ( hr!=DS_OK );
		// リストアされたよ～
		if ( flag ) *flag = TRUE;
	}
	return TRUE;
}


// DSバッファを全て埋める（初期化時）
static int FillBuffer(ST_DSINFO* info)
{
	HRESULT hr;
	VOID* buf = NULL;
	DWORD size = 0;
	DWORD read = 0;
	int ret = FALSE;

	do {
		if ( !info->pDSB ) break;
		if ( !RestoreBuffer(info, NULL) ) break;

		// 全バッファを取得して埋める
		if ( FAILED(hr=info->pDSB->Lock(0, info->DSBSize, &buf, &size, NULL, NULL, 0L)) ) break;
		if ( info->GetPCM ) {
			DWORD cnt = size/info->DSBUnit;
			info->GetPCM(info->GetPCMPrm, (short*)buf, cnt);
		} else {
			memset(buf, 0, size);
		}
		info->pDSB->Unlock(buf, size, NULL, 0);
		ret = TRUE;
	} while ( 0 );

	return ret;
}


// 1バッファ埋め
static int AddBuffer(ST_DSINFO* info, DWORD n)
{
	HRESULT hr;
	VOID *buf = NULL, *buf2 = NULL;
	DWORD size = 0, size2 = 0;
	DWORD read = 0;
	BOOL rest;
	BOOL loop = TRUE;
	int ret = FALSE;

	EnterCriticalSection(&info->CritCs);

	do {
		if ( !info->pDSB ) break;
		if ( !RestoreBuffer(info, &rest) ) break;
		if ( rest ) {
			// リストアされたときは全て埋めて終了
			if ( !FillBuffer(info) ) break;
			ret = TRUE;
			break;
		}
		// 次位置のバッファを埋める
		if ( FAILED(hr=info->pDSB->Lock(n*info->DSBBlk, info->DSBBlk, &buf, &size, &buf2, &size2, 0L)) ) break;
		// 総バッファ長は1バッファ長の整数倍、よってbuf2に値が入ることはないはず
		if( buf2!=NULL ) break;
		// バッファ内容取得
		if ( info->GetPCM ) {
			DWORD cnt = size/info->DSBUnit;
			info->GetPCM(info->GetPCMPrm, (short*)buf, cnt);
		} else {
			memset(buf, 0, size);
		}
		info->pDSB->Unlock(buf, size, NULL, 0);
		ret = TRUE;
	} while ( 0 );

	LeaveCriticalSection(&info->CritCs);
	return ret;
}


// プライマリバッファの設定
static int SetPrimaryBufferFormat(ST_DSINFO* info, int ch, int freq, int bits)
{
	HRESULT hr;
	LPDIRECTSOUNDBUFFER pdsb = NULL;
	DSBUFFERDESC dsbd;
	WAVEFORMATEX wfx;

	if ( !info->pDS ) return 0;

	// プライマリバッファ作成
	ZeroMemory( &dsbd, sizeof(DSBUFFERDESC) );
	dsbd.dwSize        = sizeof(DSBUFFERDESC);
	dsbd.dwFlags       = DSBCAPS_PRIMARYBUFFER;
	dsbd.dwBufferBytes = 0;
	dsbd.lpwfxFormat   = NULL;
	if ( FAILED(hr=info->pDS->CreateSoundBuffer(&dsbd, &pdsb, NULL)) ) return 0;

	// フォーマットの設定
	ZeroMemory( &wfx, sizeof(WAVEFORMATEX) ); 
	wfx.wFormatTag      = WAVE_FORMAT_PCM; 
	wfx.nChannels       = ch; 
	wfx.nSamplesPerSec  = freq; 
	wfx.wBitsPerSample  = bits; 
	wfx.nBlockAlign     = (WORD)(ch*(bits>>3));
	wfx.nAvgBytesPerSec = freq*wfx.nBlockAlign;
	if ( FAILED(hr=pdsb->SetFormat(&wfx)) ) {
		SAFE_RELEASE(pdsb);
		return 0;
	}

	SAFE_RELEASE(pdsb);
    return 1;
}


// DS初期化
static int DSound_Init(ST_DSINFO* info, HWND hWnd, int ch, int freq, int bits)
{
	HRESULT hr;
	WAVEFORMATEX wfx;
	DSBUFFERDESC dsbd;
	LPDIRECTSOUNDNOTIFY pdsn = NULL;
	DSBPOSITIONNOTIFY pos[BUFFERCNT];
	int i;

	if ( !info->pDSCreate ) return 0;

	SAFE_RELEASE(info->pDS);

	// 1サンプル当たりのバイト数
	info->DSBUnit = ch*(bits>>3);
	// 1バッファ長（byte）
	info->DSBBlk = ((freq*DSBUFFERMSEC*info->DSBUnit)/1000)/BUFFERCNT;
	// 1バッファ長はブロックサイズの整数倍にする
	info->DSBBlk -= info->DSBBlk%info->DSBUnit;
	// 総バッファ長は1バッファ長の整数倍
	info->DSBSize = info->DSBBlk*BUFFERCNT;

	// ストリームバッファのフォーマット
	ZeroMemory(&wfx, sizeof(WAVEFORMATEX));
	wfx.wFormatTag      = WAVE_FORMAT_PCM;
	wfx.nChannels       = ch;
	wfx.nSamplesPerSec  = freq;
	wfx.nBlockAlign     = (WORD)info->DSBUnit;
	wfx.nAvgBytesPerSec = info->DSBUnit*freq;
	wfx.wBitsPerSample  = bits;

	// ストリームバッファ作成用
	ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
	dsbd.dwSize          = sizeof(DSBUFFERDESC);
	// バックグラウンド時も再生するなら DSBCAPS_GLOBALFOCUS を指定
#ifdef USE_DSHWBUFFER
	dsbd.dwFlags         = DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GLOBALFOCUS | DSBCAPS_LOCDEFER | DSBCAPS_GETCURRENTPOSITION2;
#else
	dsbd.dwFlags         = DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GLOBALFOCUS | DSBCAPS_LOCSOFTWARE | DSBCAPS_GETCURRENTPOSITION2;
#endif
	dsbd.dwBufferBytes   = info->DSBSize;
#if DIRECTSOUND_VERSION >= 0x0800
	dsbd.guid3DAlgorithm = GUID_NULL;
#endif
	dsbd.lpwfxFormat     = &wfx;

	do {
		// DS初期化
		if ( FAILED(hr=info->pDSCreate(NULL, &info->pDS, NULL )) ) break;
		if ( FAILED(hr=info->pDS->SetCooperativeLevel(hWnd, DSSCL_PRIORITY)) ) {
			if ( FAILED(hr=info->pDS->SetCooperativeLevel(hWnd, DSSCL_NORMAL)) ) break;
		}
		if ( !SetPrimaryBufferFormat(info, ch, freq, bits) ) break;

		// バッファ作成
		if ( FAILED(hr=info->pDS->CreateSoundBuffer(&dsbd, &info->pDSB, NULL)) ) break;

		// Notifyポイントを設定
		if ( FAILED(hr=info->pDSB->QueryInterface(IID_IDirectSoundNotify, (VOID**)&pdsn)) ) break;
		for (i=0; i<BUFFERCNT; i++) {
			pos[i].dwOffset     = (info->DSBBlk*i)+info->DSBBlk-1;
			pos[i].hEventNotify = info->hEvent[i];
		}
		if ( FAILED(hr=pdsn->SetNotificationPositions(BUFFERCNT, pos)) ) break;
		SAFE_RELEASE(pdsn);

		// 再生を開始
		FillBuffer(info);
		info->pDSB->SetCurrentPosition(0);
		return 1;
	} while ( 0 );

	SAFE_RELEASE(pdsn);
	SAFE_RELEASE(info->pDS);
	return 0;
}


// DS開放
static void DSound_Clean(ST_DSINFO* info)
{
	if ( info->pDSB ) info->pDSB->Stop();
	SAFE_RELEASE(info->pDSB);
	SAFE_RELEASE(info->pDS);
}


// DS停止
static void DSound_Stop(ST_DSINFO* info)
{
	if ( info->pDSB ) info->pDSB->Stop();
}



// バッファ埋め用スレッド
static DWORD WINAPI ThreadProc(LPVOID prm)
{
	ST_DSINFO* info = (ST_DSINFO*)prm;
	MSG msg;
	DWORD ret;
	BOOL term = FALSE;

	while ( !term ) {
		ret = MsgWaitForMultipleObjects(BUFFERCNT, info->hEvent, FALSE, INFINITE, QS_ALLEVENTS);
		if ( (ret>=WAIT_OBJECT_0)&&(ret<(WAIT_OBJECT_0+BUFFERCNT)) ) {
			// Notify
			if ( !AddBuffer(info, ret-WAIT_OBJECT_0) ) term = TRUE;
		} else {
			switch( ret ) {
				case WAIT_OBJECT_0+BUFFERCNT:		// メッセージ処理
					while ( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) ) {
						TranslateMessage(&msg);
						DispatchMessage(&msg);
						// 終了メッセージが来たら終わる
						if ( msg.message==WM_QUIT ) term = TRUE;
					}
					break;
			}
		}
	}

	return 0;
}


// ---------------------------------------------------------------
// 公開関数
// ---------------------------------------------------------------
// サウンドI/F作成
DSHANDLE DSound_Create(HWND hWnd, UINT freq, UINT ch)
{
	ST_DSINFO* info;
	int i;

	info = (ST_DSINFO*)_MALLOC(sizeof(ST_DSINFO), "DirectSound info");

	do {
		if ( !info ) break;
		memset(info, 0, sizeof(ST_DSINFO));

		InitializeCriticalSection(&info->CritCs);

		// DSOUND.DLL読み込み
		info->hDSoundDLL = LoadLibrary(_T("DSOUND.DLL"));
		if ( !info->hDSoundDLL ) break;

#ifdef FORCEDX8
		info->pDSCreate = (DIRECTSOUNDCREATE)GetProcAddress(info->hDSoundDLL, "DirectSoundCreate8");
#else
		info->pDSCreate = (DIRECTSOUNDCREATE)GetProcAddress(info->hDSoundDLL, "DirectSoundCreate");
#endif
		if ( !info->pDSCreate ) break;

		// Notify用イベント作成
		for (i=0; i<BUFFERCNT; i++) {
			info->hEvent[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
			if ( !info->hEvent[i] ) goto ds_error;
		}

		// DS初期化
		if ( !DSound_Init(info, hWnd, ch, freq, 16) ) break;

		// バッファ埋め用スレッド作成
		info->hThread = CreateThread(NULL, 0, ThreadProc, (void*)info, 0, &info->ThreadID);
		if ( !info->hThread ) break;
		SetThreadPriority(info->hThread, THREAD_PRIORITY_TIME_CRITICAL);

		info->pDSB->Play(0, 0, DSBPLAY_LOOPING);

		LOG(("DirectSound : Initialized."));
		return (DSHANDLE)info;

	} while ( 0 );

ds_error:
	DSound_Destroy((DSHANDLE)info);
	return NULL;
}


// サウンドI/F開放
void DSound_Destroy(DSHANDLE handle)
{
	ST_DSINFO* info = (ST_DSINFO*)handle;
	int i;

	if ( !info ) return;

	DSound_Stop(info);

	// スレッドを殺す
	if ( info->hThread ) {
		PostThreadMessage(info->ThreadID, WM_QUIT, 0, 0);
		WaitEvent(info->hThread, INFINITE);
		CloseHandle(info->hThread);
		info->hThread = NULL;
	}

	// イベントハンドル削除
	for (i=0; i<BUFFERCNT; i++) {
		if ( info->hEvent[i] ) CloseHandle(info->hEvent[i]), info->hEvent[i] = NULL;
	}

	// DSを殺す
	DSound_Clean(info);

	info->pDSCreate = NULL;
	if ( info->hDSoundDLL ) {
		FreeLibrary(info->hDSoundDLL);
		info->hDSoundDLL = NULL;
	}

	DeleteCriticalSection(&info->CritCs);

	_MFREE(info);
}


void DSound_SetVolume(DSHANDLE handle, int n)
{
	ST_DSINFO* info = (ST_DSINFO*)handle;
	if ( !info ) return;
	if ( n<0 ) n = 0;
	else if ( n>100 ) n = 100;
	EnterCriticalSection(&info->CritCs);
	info->volume = (n*256)/100;
	LeaveCriticalSection(&info->CritCs);
}


void DSound_Pause(DSHANDLE handle)
{
	ST_DSINFO* info = (ST_DSINFO*)handle;
	if ( !info ) return;
	if ( info->pDSB ) info->pDSB->Stop();
}


void DSound_Restart(DSHANDLE handle)
{
	ST_DSINFO* info = (ST_DSINFO*)handle;
	if ( !info ) return;
	if ( info->pDSB ) info->pDSB->Play(0, 0, DSBPLAY_LOOPING);
}


void DSound_SetCB(DSHANDLE handle, void (CALLBACK *ptr)(void*, short*, unsigned int), void* prm)
{
	ST_DSINFO* info = (ST_DSINFO*)handle;
	if ( !info ) return;
	EnterCriticalSection(&info->CritCs);
	info->GetPCM = ptr;
	info->GetPCMPrm = prm;
	LeaveCriticalSection(&info->CritCs);
}


void DSound_Lock(DSHANDLE handle)
{
	ST_DSINFO* info = (ST_DSINFO*)handle;
	if ( !info ) return;
	EnterCriticalSection(&info->CritCs);
}


void DSound_Unlock(DSHANDLE handle)
{
	ST_DSINFO* info = (ST_DSINFO*)handle;
	if ( !info ) return;
	LeaveCriticalSection(&info->CritCs);
}
