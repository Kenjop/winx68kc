/* -----------------------------------------------------------------------------------
  Win32エミュレータ実行用コアクラス
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "win32core.h"
#include "x68000_driver.h"

#include <atlstr.h>
#include <vector>


// --------------------------------------------------------------------------
//   定数
// --------------------------------------------------------------------------
#define SOUND_HZ          48000
#define FRAME_RATE        60.0  // Win側のフレームレート
#define FRAME_PERIOD_US   (1000000.0 / FRAME_RATE)

#define SRAM_FILE         _T("SRAM.DAT")


// --------------------------------------------------------------------------
//   サウンド関連
// --------------------------------------------------------------------------
/*
	DSoundからの吸い上げを直接SndStreamに流し込まないようにするための中継処理

	SndStreamは溜め込んでるサンプルが不足している場合、上位層（サウンドデバイス）
	に追加サンプルを要求する構造になっている。
	音が途切れないようにするための構造だが、X68000ではこれをやられると、PCM4など
	CPU主導で回すADPCMが破綻するので、敢えてまだるっこしい仕組みにしている。
*/
void CALLBACK CWin32Core::GetPCM(void* prm, SINT16* buf, UINT32 len)
{
	CWin32Core* _this = reinterpret_cast<CWin32Core*>(prm);
	SOUND_PCM_INFO* snd = (SOUND_PCM_INFO*)&_this->m_stSoundInfo;
	SINT16* p = buf;;
	UINT32 n = len;
	while ( n-- ) {
		if ( snd->mPosR != snd->mPosW ) {
			*p++ = snd->mBuffer[snd->mPosR*2+0];
			*p++ = snd->mBuffer[snd->mPosR*2+1];
			snd->mPosR = (snd->mPosR+1) & (SOUND_BUFFER_SIZE-1);
		} else {
			*p++ = 0;
			*p++ = 0;
		}
	}
}

void CWin32Core::SoundSync(double t)
{
	SOUND_PCM_INFO* snd = (SOUND_PCM_INFO*)&m_stSoundInfo;
	UINT32 len;

	snd->mCount += (double)m_pDrv->sndfreq * t;
	len = (UINT32)snd->mCount;
	snd->mCount -= (double)len;

	if ( len > 0 ) {
		SndStream_GetPCM(m_pDrv->sound, m_nTmpSndBuf, len);

		UINT32 n = len;
		SINT16* p = m_nTmpSndBuf;
		while ( n-- ) {
			if ( snd->mPosR != ((snd->mPosW+1) & (SOUND_BUFFER_SIZE-1)) ) {
				snd->mBuffer[snd->mPosW*2+0] = *p++;
				snd->mBuffer[snd->mPosW*2+1] = *p++;
				snd->mPosW = (snd->mPosW+1) & (SOUND_BUFFER_SIZE-1);
			}
		}
	}
}

void CWin32Core::SoundBufferReset()
{
	SOUND_PCM_INFO* snd = (SOUND_PCM_INFO*)&m_stSoundInfo;
	snd->mPosW = snd->mPosR = 0;
}


// --------------------------------------------------------------------------
//   内部処理
// --------------------------------------------------------------------------
// 経過時間取得（us）
double CWin32Core::GetElipsedTime()
{
	LARGE_INTEGER cur;
	double r;
	QueryPerformanceCounter(&cur);
	r = (double)cur.QuadPart - (double)m_lLastTick.QuadPart;
	m_lLastTick.QuadPart = cur.QuadPart;
	r *= m_dFreqTick;
	return r;
}

// 汎用ファイル読み込み
bool CWin32Core::LoadFile(const TCHAR* file, void* dst, UINT32 size)
{
	bool ret = false;
	HANDLE hfile = CreateFile(file, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	do {
		DWORD byte;
		if ( hfile==INVALID_HANDLE_VALUE ) break;
		if ( !ReadFile(hfile, dst, size, &byte, NULL) ) break;
		if ( byte!=size ) break;
		ret = true;
	} while ( 0 );
	CloseHandle(hfile);
	return ret;
}

// 汎用ファイル書き出し
bool CWin32Core::SaveFile(const TCHAR* file, const void* dst, UINT32 size)
{
	bool ret = false;
	HANDLE hfile = CreateFile(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	do {
		DWORD byte;
		if ( hfile==INVALID_HANDLE_VALUE ) break;
		if ( !WriteFile(hfile, dst, size, &byte, NULL) ) break;
		if ( byte!=size ) break;
		ret = true;
	} while ( 0 );
	CloseHandle(hfile);
	return ret;
}

// --------------------------------------------------------------------------
//   SASI HDD用コールバック
// --------------------------------------------------------------------------
BOOL CALLBACK CWin32Core::SasiCallback(void* prm, SASI_FUNCTIONS func, UINT32 devid, UINT32 pos, UINT8* data, UINT32 size)
{
	CWin32Core* _this = reinterpret_cast<CWin32Core*>(prm);
	return _this->SasiCallbackMain(func, devid, pos, data, size);
}

BOOL CWin32Core::SasiCallbackMain(SASI_FUNCTIONS func, UINT32 devid, UINT32 pos, UINT8* data, UINT32 size)
{
	HANDLE hfile = INVALID_HANDLE_VALUE;
	BOOL ret = FALSE;

	if ( devid >= 16 ) return FALSE;
	if ( !m_pCfg->mDlgCfg.sSasiFile[devid][0] ) return FALSE;

	switch ( func )
	{
	default:
	case SASIFUNC_IS_READY:
		if ( pos >= m_pCfg->mDlgCfg.nSasiSize[devid] ) break;
		ret = TRUE;
		break;

	case SASIFUNC_READ:
		{
			hfile = CreateFile(m_pCfg->mDlgCfg.sSasiFile[devid], GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if ( hfile == INVALID_HANDLE_VALUE ) break;
			DWORD newpos = SetFilePointer(hfile, pos, NULL, FILE_BEGIN);
			if ( newpos != pos ) break;
			DWORD byte;
			if ( !ReadFile(hfile, data, size, &byte, NULL) ) break;
			if ( byte != size ) break;
			ret = TRUE;
		}
		break;

	case SASIFUNC_WRITE:
		{
			hfile = CreateFile(m_pCfg->mDlgCfg.sSasiFile[devid], GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if ( hfile == INVALID_HANDLE_VALUE ) break;
			DWORD newpos = SetFilePointer(hfile, pos, NULL, FILE_BEGIN);
			if ( newpos != pos ) break;
			DWORD byte;
			if ( !WriteFile(hfile, data, size, &byte, NULL) ) break;
			if ( byte != size ) break;
			ret = TRUE;
		}
		break;
	}

	if ( hfile != INVALID_HANDLE_VALUE ) CloseHandle(hfile);
	return ret;
}


// --------------------------------------------------------------------------
//   公開I/F
// --------------------------------------------------------------------------
// コンストラクタ
CWin32Core::CWin32Core()
{
	m_hWnd = NULL;
	m_pCfg = NULL;
	m_pDrv = NULL;
	m_hDraw = NULL;
	m_hSound = NULL;

	m_bPause = false;
	m_bNoWait = false;
	m_bExecuted = false;
	m_bRomLoaded = false;

	m_nExecClocks = 0;
	m_nExecFrames = 0;
}

// デストラクタ
CWin32Core::~CWin32Core()
{
}

void CWin32Core::Init(HWND hwnd, CWin32Config* cfg)
{
	m_hWnd = hwnd;
	m_pCfg = cfg;

	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq); 
	m_dFreqTick = 1000000.0 / (double)freq.QuadPart;

	QueryPerformanceCounter(&m_lLastTick);
	m_dRemainTime = FRAME_PERIOD_US;

	TCHAR buf[MAX_PATH];
	TCHAR* filepart;
	TCHAR* extpart;
	GetModuleFileName(NULL, buf, MAX_PATH);
	GetFullPathName(buf, MAX_PATH, m_sBasePath, &filepart);
	_tcscpy(m_sBaseName, filepart);
	*filepart = 0;
	extpart = _tcsrchr(m_sBaseName, '.');
	if ( extpart ) *extpart = 0;

	memset(m_pIplRom, 0, sizeof(m_pIplRom));
	memset(m_pFontRom, 0, sizeof(m_pFontRom));
}

// ROM読み込み
bool CWin32Core::LoadRoms()
{
	std::vector<TCHAR*> IPLROM_FILES = {
		_T("IPLROM.DAT"), _T("IPLROMXV.DAT"), _T("IPLROMCO.DAT"), _T("IPLROM30.DAT")
	};
	std::vector<TCHAR*> FONTROM_FILES = {
		_T("CGROM.DAT"), _T("CGROM.TMP")
	};
	CString path;
	size_t i;

	do {
		for (i=0; i<IPLROM_FILES.size(); i++)
		{
			path.Format(_T("%s%s"), m_sBasePath, IPLROM_FILES[i]);
			if ( LoadFile(path.GetBuffer(), m_pIplRom, sizeof(m_pIplRom)) ) break;
		}
		if ( i==IPLROM_FILES.size() ) {
			MessageBox(m_hWnd, _T("IPL ROMが見つかりません。\nこのEXEと同じフォルダにROMファイルを配置して起動してください。"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
			break;
		}

		for (i=0; i<FONTROM_FILES.size(); i++)
		{
			path.Format(_T("%s%s"), m_sBasePath, FONTROM_FILES[i]);
			if ( LoadFile(path.GetBuffer(), m_pFontRom, sizeof(m_pFontRom)) ) break;
		}
		if ( i==FONTROM_FILES.size() ) {
			MessageBox(m_hWnd, _T("FONT ROM（CGROM.DATかCGROM.TMPのいずれか）が見つかりません。\nこのEXEと同じフォルダにROMファイルを配置して起動してください。"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
			break;
		}

		m_bRomLoaded = true;
		return true;
	} while ( 0 );

	m_bRomLoaded = false;
	return false;
}

// イジェクトコールバック登録
void CWin32Core::SetDiskEjectCallback(DISKEJECTCB cb, void* cbprm)
{
	if ( m_pDrv ) {
		X68kDriver_SetEjectCallback(m_pDrv, cb, cbprm);
	}
}

// エミュ起動
bool CWin32Core::InitEmulator()
{
	do {
		if ( !m_bRomLoaded ) break;

		// ----- 以下は環境に依存しない初期化処理
		// ドライバの初期化
		m_pDrv = X68kDriver_Initialize(m_pIplRom, m_pFontRom, SOUND_HZ);
		if ( !m_pDrv ) {
			MessageBox(m_hWnd, _T("エミュレーションコアの作成に失敗しました。\nメモリが足りない可能性があります。"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
			break;
		}

		// ストリーム出力の音量初期設定
		// XXX
		SndStream_SetVolume(m_pDrv->sound, (80*256)/100);

		// ----- 以下は全てWin特有の処理
		// SRAM読み込み
		UINT32 sz = 0;
		UINT8* ptr = X68kDriver_GetSramPtr(m_pDrv, &sz);
		if ( ptr ) {
			CString path;
			path.Format(_T("%s%s"), m_sBasePath, SRAM_FILE);
			LoadFile(path.GetBuffer(), ptr, sz);
		}

		m_hSound = DSound_Create(m_hWnd, SOUND_HZ, 2);
		if ( !m_hSound ) {
			MessageBox(m_hWnd, _T("DirectSoundの初期化に失敗しました。\nこのアプリケーションにはDirectSoundが動作するサウンドカードが必要です。"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
			break;
		}
		DSound_SetCB(m_hSound, &GetPCM, (void*)this);

#if 0
		// 表示関係初期化
		int w = (m_pDrv->scr->w);
		int h = (m_pDrv->scr->h);
		WinUtil_SetWindowSize(w, h);

		UpdateWindow(m_hWnd);
		ShowWindow(m_hWnd, SW_SHOW);
#endif
		m_hDraw = D3DDraw_Create(m_hWnd, m_pDrv->scr);
		if ( !m_hDraw ) {
			MessageBox(m_hWnd, _T("Direct3Dの初期化に失敗しました。\nこのアプリケーションにはDirect3D11が動作するビデオカードが必要です。"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
			break;
		}

		m_mMidi.Init(m_pDrv, m_pCfg);

		// 入力関連初期化
		mJoystick.Init(m_pDrv, m_pCfg);
		mKeyboard.Init(m_pDrv, m_pCfg);

		// SASI用コールバック登録
		X68kDriver_SetSasiCallback(m_pDrv, &CWin32Core::SasiCallback, (void*)this);

		// 設定を適用
		X68kDriver_SetCpuClock(m_pDrv, m_pCfg->nCpuClock);
		ApplyConfig();

		return true;
	} while ( 0 );

	return false;
}

// エミュ終了
void CWin32Core::CleaupEmulator()
{
	// ----- Win環境での終了処理
	m_mMidi.Cleanup();

	// サウンドを先に止めること（Winでは別スレで回してるので）
	DSound_Destroy(m_hSound);
	m_hSound = NULL;

	// 次に描画を止める
	D3DDraw_Dispose(m_hDraw);
	m_hDraw = NULL;

	// SRAMを保存
	UINT32 sz = 0;
	UINT8* ptr = X68kDriver_GetSramPtr(m_pDrv, &sz);
	if ( ptr ) {
		CString path;
		path.Format(_T("%s%s"), m_sBasePath, SRAM_FILE);
		SaveFile(path.GetBuffer(), ptr, sz);
	}

	// ----- 以下は環境に依存しない終了処理
	X68kDriver_Cleanup(m_pDrv);
	m_pDrv = NULL;
}

void CWin32Core::Reset()
{
	X68kDriver_Reset(m_pDrv);
	if ( m_pCfg->mDlgCfg.bMidiSendReset ) {
		m_mMidi.SignalReset();
	}
}

void CWin32Core::RunTimerReset()
{
	QueryPerformanceCounter(&m_lLastTick);
	m_dRemainTime = FRAME_PERIOD_US;
}

// エミュ実行
double CWin32Core::Run()
{
	BOOL exec = FALSE;
	BOOL non_wait = ( m_pCfg->bHardwareVsync || m_bNoWait ) ? TRUE : FALSE;

	if ( non_wait ) {
		exec = TRUE;
	} else {
		// 経過時間を取得、カウンタから減算
		m_dRemainTime -= GetElipsedTime();
		// 0になってたら1フレーム進行
		if ( m_dRemainTime <= 0.0 ) {
			// 0より大きくなるまで再加算（複数回の場合フレームスキップすべきだが）
			while ( m_dRemainTime <= 0.0 ) {
				m_dRemainTime += FRAME_PERIOD_US;
			}
			exec = TRUE;
		}
	}

	// 1フレーム実行
	if ( exec ) {
		RunStep();
	}

	// ハードウェアVSYNCの場合、残時間は常に0を返す
	return ( non_wait ) ? 0.0 : m_dRemainTime;
}

void CWin32Core::RunStep()
{
	if ( !m_bPause || m_bStepExec ) {
		// 入力のポーリング
		mJoystick.CheckInput();
		mKeyboard.CheckInput();

		// エミュコアの実行
		DSound_Lock(m_hSound);
		m_nExecClocks += X68kDriver_Exec(m_pDrv, TIMERPERIOD_HZ(FRAME_RATE));
		m_nExecFrames += m_pDrv->scr->frame;
		SoundSync(1.0/(double)FRAME_RATE);
		DSound_Unlock(m_hSound);

		m_bStepExec = false;
	}

	// 描画
	ST_DISPAREA area;
	BOOL enable = X68kDriver_GetDrawInfo(m_pDrv, &area);
	if ( m_pCfg->bZoomIgnoreCrtc ) {
		// CRTC値を使わない場合は、フルサイズになるようパラメータを入れる
		// （描画が有効かをチェックするために GetCrtcTiming() 自体は呼ぶ）
		area.scrn.x1 = area.disp.x1 = 0;
		area.scrn.x2 = area.disp.x2 = (SINT32)m_pDrv->scr->w;
		area.scrn.y1 = area.disp.y1 = 0;
		area.scrn.y2 = area.disp.y2 = (SINT32)m_pDrv->scr->h;
	}
	D3DDraw_Draw(m_hDraw, ( m_bNoWait ) ? FALSE : m_pCfg->bHardwareVsync, &area, enable);

	m_bExecuted = true;
}

void CWin32Core:: SetNoWait(bool sw)
{
	m_bNoWait = sw;
	if ( sw ) {
		m_bPause = false;
		m_bStepExec = false;
	} else {
		SoundBufferReset();
		RunTimerReset();
	}
}

void CWin32Core::SetPause(bool sw)
{
	m_bPause = sw;
	if ( sw ) {
		m_bNoWait = false;
		m_bStepExec = false;
	} else {
		SoundBufferReset();
		RunTimerReset();
	}
}

void CWin32Core::ApplyConfig()
{
	X68kDriver_SetMemorySize(m_pDrv, m_pCfg->mDlgCfg.nRamSize);
	X68kDriver_SetVolume(m_pDrv, X68K_SOUND_OPM,   (float)m_pCfg->mDlgCfg.nVolumeOPM/10.0f);
	X68kDriver_SetVolume(m_pDrv, X68K_SOUND_ADPCM, (float)m_pCfg->mDlgCfg.nVolumeADPCM/10.0f);
	X68kDriver_SetFilter(m_pDrv, X68K_SOUND_OPM,    m_pCfg->mDlgCfg.nFilterOPM);
	X68kDriver_SetFilter(m_pDrv, X68K_SOUND_ADPCM,  m_pCfg->mDlgCfg.nFilterADPCM);
	X68kDriver_SetFastFddAccess(m_pDrv, m_pCfg->mDlgCfg.bFastFdd);

	m_mMidi.ApplyConfig();

	// SRAM自動更新類
	UINT32 sz = 0;
	UINT8* ptr = X68kDriver_GetSramPtr(m_pDrv, &sz);
	if ( ptr ) {
		// RAMサイズ
		if ( m_pCfg->mDlgCfg.bRamSizeUpdate ) {
			UINT32 ram = m_pCfg->mDlgCfg.nRamSize << 20;
			WRITEBEDWORD(&ptr[0x08], ram);
		}
		// HDD接続数
		if ( m_pCfg->mDlgCfg.bSasiSramUpd ) {
			ptr[0x5A] = (UINT8)m_pCfg->mDlgCfg.nSasiNum;
		}
	}
}
