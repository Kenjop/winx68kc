/* -----------------------------------------------------------------------------------
  Win32エミュレータ実行用コアクラス
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef __win32_core_h
#define __win32_core_h

#include "osconfig.h"
#include "emu_driver.h"
#include "win32d3d.h"
#include "win32sound.h"
#include "win32joystick.h"
#include "win32keyboard.h"
#include "win32midi.h"
#include "win32config.h"


// DSoundがダブルバッファの片面50ms取ってるので、再生周波数*50/1000 の長さが必要
#define SOUND_BUFFER_SIZE 4096


typedef struct {
	SINT16             mBuffer[SOUND_BUFFER_SIZE*2];
	double             mCount;
	UINT32             mPosW;
	UINT32             mPosR;
} SOUND_PCM_INFO;


class CWin32Core
{
	public:
		CWin32Core();
		virtual ~CWin32Core();

		void Init(HWND hwnd, CWin32Config* cfg);
		bool LoadRoms();
		void SetDiskEjectCallback(DISKEJECTCB cb, void* cbprm);

		bool InitEmulator();
		void CleaupEmulator();

		void Reset();
		void RunTimerReset();

		double Run();
		void RunStep();
		bool IsExecuted() { bool ret = m_bExecuted; m_bExecuted = false; return ret; }
		UINT32 GetExecutedClocks() { UINT32 ret = m_nExecClocks; m_nExecClocks = 0; return ret; }
		UINT32 GetExecutedFrames() { UINT32 ret = m_nExecFrames; m_nExecFrames = 0; return ret; }

		EMUDRIVER* GetDriver() const { return m_pDrv; }
		D3DDRAWHDL GetDrawHandle() const { return m_hDraw; }
		DSHANDLE   GetSoundHandle() const { return m_hSound; }

		const TCHAR* GetBasePath() const { return m_sBasePath; }
		const TCHAR* GetBaseName() const { return m_sBaseName; }

		void SetNoWait(bool sw);
		void SetPause(bool sw);
		void ExecStep() { m_bStepExec = true; };
		bool GetPauseState() const { return m_bPause; };
		bool GetNoWaitState() const { return m_bNoWait; };
		void ApplyConfig();

		void KeyPress(WPARAM wParam, LPARAM lParam) {
			mKeyboard.Press(wParam, lParam);
			mJoystick.JoyKeyPress(wParam);
		}
		void KeyRelease(WPARAM wParam, LPARAM lParam) {
			mKeyboard.Release(wParam, lParam);
			mJoystick.JoyKeyRelease(wParam);
		}
		void KeyClear() {
			mKeyboard.Clear();
			mJoystick.JoyKeyClear();
		}

	private:
		HWND           m_hWnd;
		CWin32Config*  m_pCfg;
		TCHAR          m_sBasePath[MAX_PATH];
		TCHAR          m_sBaseName[MAX_PATH];

		UINT8          m_pIplRom[0x20000];
		UINT8          m_pFontRom[0xC0000];
		bool           m_bRomLoaded;

		// エミュドライバ
		EMUDRIVER*     m_pDrv;

		// 描画マネージャハンドル
		D3DDRAWHDL     m_hDraw;

		// サウンドマネージャハンドル
		DSHANDLE       m_hSound;

		CWin32Midi     m_mMidi;

		CWin32Joystick mJoystick;
		CWin32Keyboard mKeyboard;

		LARGE_INTEGER  m_lLastTick;
		double         m_dFreqTick;
		double         m_dRemainTime;

		bool           m_bPause;
		bool           m_bNoWait;
		bool           m_bStepExec;
		bool           m_bExecuted;
		UINT32         m_nExecClocks;
		UINT32         m_nExecFrames;

		SOUND_PCM_INFO m_stSoundInfo;
		SINT16         m_nTmpSndBuf[SOUND_BUFFER_SIZE*2];

		static void CALLBACK GetPCM(void* prm, SINT16* buf, UINT32 len);
		void           SoundSync(double t);
		void           SoundBufferReset();

		double         GetElipsedTime();

		bool           LoadFile(const TCHAR* file, void* dst, UINT32 size);
		bool           SaveFile(const TCHAR* file, const void* dst, UINT32 size);

		static BOOL CALLBACK SasiCallback(void* prm, SASI_FUNCTIONS func, UINT32 devid, UINT32 pos, UINT8* data, UINT32 size);
		BOOL           SasiCallbackMain(SASI_FUNCTIONS func, UINT32 devid, UINT32 pos, UINT8* data, UINT32 size);
};


#endif //__win32_core_h
