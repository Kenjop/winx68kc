/* -----------------------------------------------------------------------------------
  Win32�G�~�����[�^���s�p�R�A�N���X
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "win32core.h"
#include "x68000_driver.h"

#include <atlstr.h>
#include <vector>


// --------------------------------------------------------------------------
//   �萔
// --------------------------------------------------------------------------
#define SOUND_HZ          48000
#define FRAME_RATE        60.0  // Win���̃t���[�����[�g
#define FRAME_PERIOD_US   (1000000.0 / FRAME_RATE)

#define SRAM_FILE         _T("SRAM.DAT")


// --------------------------------------------------------------------------
//   �T�E���h�֘A
// --------------------------------------------------------------------------
/*
	DSound����̋z���グ�𒼐�SndStream�ɗ������܂Ȃ��悤�ɂ��邽�߂̒��p����

	SndStream�͗��ߍ���ł�T���v�����s�����Ă���ꍇ�A��ʑw�i�T�E���h�f�o�C�X�j
	�ɒǉ��T���v����v������\���ɂȂ��Ă���B
	�����r�؂�Ȃ��悤�ɂ��邽�߂̍\�������AX68000�ł͂���������ƁAPCM4�Ȃ�
	CPU�哱�ŉ�ADPCM���j�]����̂ŁA�����Ă܂�����������d�g�݂ɂ��Ă���B
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
//   ��������
// --------------------------------------------------------------------------
// �o�ߎ��Ԏ擾�ius�j
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

// �ėp�t�@�C���ǂݍ���
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

// �ėp�t�@�C�������o��
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
//   SASI HDD�p�R�[���o�b�N
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
//   ���JI/F
// --------------------------------------------------------------------------
// �R���X�g���N�^
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

// �f�X�g���N�^
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

// ROM�ǂݍ���
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
			MessageBox(m_hWnd, _T("IPL ROM��������܂���B\n����EXE�Ɠ����t�H���_��ROM�t�@�C����z�u���ċN�����Ă��������B"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
			break;
		}

		for (i=0; i<FONTROM_FILES.size(); i++)
		{
			path.Format(_T("%s%s"), m_sBasePath, FONTROM_FILES[i]);
			if ( LoadFile(path.GetBuffer(), m_pFontRom, sizeof(m_pFontRom)) ) break;
		}
		if ( i==FONTROM_FILES.size() ) {
			MessageBox(m_hWnd, _T("FONT ROM�iCGROM.DAT��CGROM.TMP�̂����ꂩ�j��������܂���B\n����EXE�Ɠ����t�H���_��ROM�t�@�C����z�u���ċN�����Ă��������B"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
			break;
		}

		m_bRomLoaded = true;
		return true;
	} while ( 0 );

	m_bRomLoaded = false;
	return false;
}

// �C�W�F�N�g�R�[���o�b�N�o�^
void CWin32Core::SetDiskEjectCallback(DISKEJECTCB cb, void* cbprm)
{
	if ( m_pDrv ) {
		X68kDriver_SetEjectCallback(m_pDrv, cb, cbprm);
	}
}

// �G�~���N��
bool CWin32Core::InitEmulator()
{
	do {
		if ( !m_bRomLoaded ) break;

		// ----- �ȉ��͊��Ɉˑ����Ȃ�����������
		// �h���C�o�̏�����
		m_pDrv = X68kDriver_Initialize(m_pIplRom, m_pFontRom, SOUND_HZ);
		if ( !m_pDrv ) {
			MessageBox(m_hWnd, _T("�G�~�����[�V�����R�A�̍쐬�Ɏ��s���܂����B\n������������Ȃ��\��������܂��B"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
			break;
		}

		// �X�g���[���o�͂̉��ʏ����ݒ�
		// XXX
		SndStream_SetVolume(m_pDrv->sound, (80*256)/100);

		// ----- �ȉ��͑S��Win���L�̏���
		// SRAM�ǂݍ���
		UINT32 sz = 0;
		UINT8* ptr = X68kDriver_GetSramPtr(m_pDrv, &sz);
		if ( ptr ) {
			CString path;
			path.Format(_T("%s%s"), m_sBasePath, SRAM_FILE);
			LoadFile(path.GetBuffer(), ptr, sz);
		}

		m_hSound = DSound_Create(m_hWnd, SOUND_HZ, 2);
		if ( !m_hSound ) {
			MessageBox(m_hWnd, _T("DirectSound�̏������Ɏ��s���܂����B\n���̃A�v���P�[�V�����ɂ�DirectSound�����삷��T�E���h�J�[�h���K�v�ł��B"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
			break;
		}
		DSound_SetCB(m_hSound, &GetPCM, (void*)this);

#if 0
		// �\���֌W������
		int w = (m_pDrv->scr->w);
		int h = (m_pDrv->scr->h);
		WinUtil_SetWindowSize(w, h);

		UpdateWindow(m_hWnd);
		ShowWindow(m_hWnd, SW_SHOW);
#endif
		m_hDraw = D3DDraw_Create(m_hWnd, m_pDrv->scr);
		if ( !m_hDraw ) {
			MessageBox(m_hWnd, _T("Direct3D�̏������Ɏ��s���܂����B\n���̃A�v���P�[�V�����ɂ�Direct3D11�����삷��r�f�I�J�[�h���K�v�ł��B"), __SYS_APPTITLE__, MB_ICONERROR | MB_OK);
			break;
		}

		m_mMidi.Init(m_pDrv, m_pCfg);

		// ���͊֘A������
		mJoystick.Init(m_pDrv, m_pCfg);
		mKeyboard.Init(m_pDrv, m_pCfg);

		// SASI�p�R�[���o�b�N�o�^
		X68kDriver_SetSasiCallback(m_pDrv, &CWin32Core::SasiCallback, (void*)this);

		// �ݒ��K�p
		X68kDriver_SetCpuClock(m_pDrv, m_pCfg->nCpuClock);
		ApplyConfig();

		return true;
	} while ( 0 );

	return false;
}

// �G�~���I��
void CWin32Core::CleaupEmulator()
{
	// ----- Win���ł̏I������
	m_mMidi.Cleanup();

	// �T�E���h���Ɏ~�߂邱�ƁiWin�ł͕ʃX���ŉ񂵂Ă�̂Łj
	DSound_Destroy(m_hSound);
	m_hSound = NULL;

	// ���ɕ`����~�߂�
	D3DDraw_Dispose(m_hDraw);
	m_hDraw = NULL;

	// SRAM��ۑ�
	UINT32 sz = 0;
	UINT8* ptr = X68kDriver_GetSramPtr(m_pDrv, &sz);
	if ( ptr ) {
		CString path;
		path.Format(_T("%s%s"), m_sBasePath, SRAM_FILE);
		SaveFile(path.GetBuffer(), ptr, sz);
	}

	// ----- �ȉ��͊��Ɉˑ����Ȃ��I������
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

// �G�~�����s
double CWin32Core::Run()
{
	BOOL exec = FALSE;
	BOOL non_wait = ( m_pCfg->bHardwareVsync || m_bNoWait ) ? TRUE : FALSE;

	if ( non_wait ) {
		exec = TRUE;
	} else {
		// �o�ߎ��Ԃ��擾�A�J�E���^���猸�Z
		m_dRemainTime -= GetElipsedTime();
		// 0�ɂȂ��Ă���1�t���[���i�s
		if ( m_dRemainTime <= 0.0 ) {
			// 0���傫���Ȃ�܂ōĉ��Z�i������̏ꍇ�t���[���X�L�b�v���ׂ������j
			while ( m_dRemainTime <= 0.0 ) {
				m_dRemainTime += FRAME_PERIOD_US;
			}
			exec = TRUE;
		}
	}

	// 1�t���[�����s
	if ( exec ) {
		RunStep();
	}

	// �n�[�h�E�F�AVSYNC�̏ꍇ�A�c���Ԃ͏��0��Ԃ�
	return ( non_wait ) ? 0.0 : m_dRemainTime;
}

void CWin32Core::RunStep()
{
	if ( !m_bPause || m_bStepExec ) {
		// ���͂̃|�[�����O
		mJoystick.CheckInput();
		mKeyboard.CheckInput();

		// �G�~���R�A�̎��s
		DSound_Lock(m_hSound);
		m_nExecClocks += X68kDriver_Exec(m_pDrv, TIMERPERIOD_HZ(FRAME_RATE));
		m_nExecFrames += m_pDrv->scr->frame;
		SoundSync(1.0/(double)FRAME_RATE);
		DSound_Unlock(m_hSound);

		m_bStepExec = false;
	}

	// �`��
	ST_DISPAREA area;
	BOOL enable = X68kDriver_GetDrawInfo(m_pDrv, &area);
	if ( m_pCfg->bZoomIgnoreCrtc ) {
		// CRTC�l���g��Ȃ��ꍇ�́A�t���T�C�Y�ɂȂ�悤�p�����[�^������
		// �i�`�悪�L�������`�F�b�N���邽�߂� GetCrtcTiming() ���̂͌Ăԁj
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

	// SRAM�����X�V��
	UINT32 sz = 0;
	UINT8* ptr = X68kDriver_GetSramPtr(m_pDrv, &sz);
	if ( ptr ) {
		// RAM�T�C�Y
		if ( m_pCfg->mDlgCfg.bRamSizeUpdate ) {
			UINT32 ram = m_pCfg->mDlgCfg.nRamSize << 20;
			WRITEBEDWORD(&ptr[0x08], ram);
		}
		// HDD�ڑ���
		if ( m_pCfg->mDlgCfg.bSasiSramUpd ) {
			ptr[0x5A] = (UINT8)m_pCfg->mDlgCfg.nSasiNum;
		}
	}
}
