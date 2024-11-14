/* -----------------------------------------------------------------------------------
  Windows MIDI support
                                                         (c) 2024 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	けろぴーでのゆいさんの実装を元にスレッド化したもの
*/

#include "win32midi.h"
#include <mmeapi.h>

#pragma comment(lib, "winmm.lib")

#define	MIDI_EXCLUSIVE    0xF0
#define MIDI_TIMECODE     0xF1
#define MIDI_SONGPOS      0xF2
#define MIDI_SONGSELECT   0xF3
#define	MIDI_TUNEREQUEST  0xF6
#define	MIDI_EOX          0xF7
#define	MIDI_TIMING       0xF8
#define MIDI_START        0xFA
#define MIDI_CONTINUE     0xFB
#define	MIDI_STOP         0xFC
#define	MIDI_ACTIVESENSE  0xFE
#define	MIDI_SYSTEMRESET  0xFF

static const UINT8 EXCV_MTRESET[] = { 0xFE, 0xFE, 0xFE };
static const UINT8 EXCV_GMRESET[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
static const UINT8 EXCV_GSRESET[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
static const UINT8 EXCV_XGRESET[] = { 0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7 };

enum {
	MIDI_MODULE_GM = 0,
	MIDI_MODULE_GS,
	MIDI_MODULE_LA,
	MIDI_MODULE_XG,
};

#define MAKE_SHORT_MSG(a,b,c)  ( ( (UINT32)(c) << 16 ) | ( (UINT32)(b) << 8 ) | (UINT32)(a) )


// --------------------------------------------------------------------------
//   メインスレッド側の処理
// --------------------------------------------------------------------------
CWin32Midi::CWin32Midi()
{
	m_pDrv = NULL;
	m_hMidi = NULL;
	m_hThread = INVALID_HANDLE_VALUE;
	m_dwThreadId = 0;
	m_bError = FALSE;
	m_bDoReset = FALSE;

	m_nInBufPosR = 0;
	m_nInBufPosW = 0;

	m_nDeviceId = -1;
	m_nModuleType = 0;

	InitializeCriticalSection(&m_csLock);
}

CWin32Midi::~CWin32Midi()
{
	DeleteCriticalSection(&m_csLock);
}

void CWin32Midi::Init(EMUDRIVER* drv, CWin32Config* cfg)
{
	m_pDrv = drv;
	m_pCfg = cfg;

	m_nDeviceId = m_pCfg->mDlgCfg.nMidiDeviceId;
	m_nModuleType = m_pCfg->mDlgCfg.nMidiModuleType;

	if ( m_nDeviceId >= 0 ) {
		// 必ずコールバック登録より先にスレッドを作る
		MidiThreadCreate();
		X68kDriver_SetMidiCallback(m_pDrv, &CWin32Midi::MidiCallback, (void*)this);
	}
}

void CWin32Midi::Cleanup()
{
	// 必ず先にコールバック解除
	X68kDriver_SetMidiCallback(m_pDrv, NULL, (void*)0);
	MidiThreadTerminate();
}

void CWin32Midi::ApplyConfig()
{
	if ( m_nDeviceId != m_pCfg->mDlgCfg.nMidiDeviceId ) {
		// デバイスが変更されている場合、スレッドの削除→再作成を行う（MidiOutをスレッド側で開いているため）
		Cleanup();
		// m_nDeviceId / m_nModuleType は Init() の冒頭で更新される
		Init(m_pDrv, m_pCfg);
	} else {
		// モジュールは変数差し替えだけでよい
		m_nModuleType = m_pCfg->mDlgCfg.nMidiModuleType;
	}
}

void CWin32Midi::MidiThreadCreate()
{
	m_hThread = CreateThread(NULL, 0, &CWin32Midi::MidiThread, (void*)this, 0, &m_dwThreadId);
	if ( m_hThread != INVALID_HANDLE_VALUE ) {
		SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL);
	}
}

void CWin32Midi::MidiThreadTerminate()
{
	if ( m_hThread != INVALID_HANDLE_VALUE ) {
		DWORD ret = WAIT_TIMEOUT;

		PostThreadMessage(m_dwThreadId, WM_QUIT, 0, 0);

		do {
			MSG msg;
			while ( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) ) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			ret = WaitForSingleObject(m_hThread, 10);
		} while ( ret == WAIT_TIMEOUT );

		CloseHandle(m_hThread);
		m_hThread = INVALID_HANDLE_VALUE;
	}
}

void CWin32Midi::SignalReset()
{
	// クリティカルセクションで保護されてる処理を使う
	MidiCallbackMain(MIDIFUNC_RESET, 0);
}

void CALLBACK CWin32Midi::MidiCallback(void* prm, MIDI_FUNCTIONS func, UINT8 data)
{
	// X68000コアからのコールバックで呼ばれる
	CWin32Midi* _this = reinterpret_cast<CWin32Midi*>(prm);
	_this->MidiCallbackMain(func, data);
}

void CWin32Midi::MidiCallbackMain(MIDI_FUNCTIONS func, UINT8 data)
{
	// エラー時はバッファ処理されなくなるのでここで帰る
	if ( m_bError ) return;

	switch ( func ) 
	{
	default:
	case MIDIFUNC_RESET:
		EnterCriticalSection(&m_csLock);
		// シグナルを上げるだけ（実処理はスレッド内部で行う）
		m_bDoReset = TRUE;
		LeaveCriticalSection(&m_csLock);
		break;

	case MIDIFUNC_DATAOUT:
		{
			EnterCriticalSection(&m_csLock);
			UINT32 next_posw = ( m_nInBufPosW + 1 ) & (MIDI_BUF_SIZE-1);
			if ( next_posw != m_nInBufPosR ) {  // 空きがある
				m_pInBuf[m_nInBufPosW] = data;
				m_nInBufPosW = next_posw;
			}
			LeaveCriticalSection(&m_csLock);
		}
		break;
	}
}


// --------------------------------------------------------------------------
//   MIDIスレッド側の処理
// --------------------------------------------------------------------------
DWORD WINAPI CWin32Midi::MidiThread(LPVOID prm)
{
	CWin32Midi* _this = reinterpret_cast<CWin32Midi*>(prm);
	// いちいち _this でアクセスするのめんどいので転送
	_this->MidiThreadMain();
	return 0;
}

void CWin32Midi::MidiThreadMain()
{
	// スレッド側で参照する変数を初期化
	m_nParseLen = 0;
	m_nInBufPosW = 0;
	m_nInBufPosR = 0;
	m_nState = MIDICTRL_READY;
	m_nLastMes = 0x80;
	m_listMsg.clear();
	m_bInExclusive = FALSE;
	memset(&m_stExHeader, 0, sizeof(m_stExHeader));

	// デバイスオープン
	// 複数スレッドからのアクセスにならないよう、スレッド冒頭でオープンし、スレッド末尾でクローズする
	// （今時スレッド跨いでの参照でおかしくなるとは思わないが…）
	if ( midiOutOpen(&m_hMidi, m_nDeviceId, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR ) {
		midiOutReset(m_hMidi);
		m_bError = FALSE;
	} else {
		// エラー落ち
		m_bError = TRUE;
		return;
	}

	// リセット送信（をキューに積む）
	SendReset();

	BOOL wait_term = FALSE;

	while ( 1 )
	{
		MSG msg;

		// お約束のメッセージ処理
		while ( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) ) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			// 終了メッセージが来たら終わる
			if ( msg.message==WM_QUIT ) {
				wait_term = TRUE;
				// 抜ける前にリセットコマンドを流す
				// SendReset は m_listMsg にメッセージを直接積むので、リセットが終わるまではこのループから
				// 抜けないようになっている、はず
				SendReset();
			}
		}

		// リセットシグナルを見つけたらリセット処理
		EnterCriticalSection(&m_csLock);
		if ( m_bDoReset ) {
			DoReset();
			m_bDoReset = FALSE;
		}
		LeaveCriticalSection(&m_csLock);

		// エクスクルーシブ送信中なら待機
		if ( m_bInExclusive ) {
			MMRESULT r = midiOutUnprepareHeader(m_hMidi, &m_stExHeader, sizeof(MIDIHDR));
			if ( r == MIDIERR_STILLPLAYING ) {
				Sleep(1);
				continue;
			}
			m_bInExclusive = FALSE;
		}

		// 受信バッファ内容をパースしてメッセージ化（要クリティカルセクション保護）
		if ( !wait_term ) {
			// 終了処理中は新たなメッセージ取り込みはしない
			EnterCriticalSection(&m_csLock);
			if ( m_nInBufPosR != m_nInBufPosW ) {
				ParseMsg();
			}
			LeaveCriticalSection(&m_csLock);
		}

		// メッセージ送信処理
		if ( m_listMsg.size() > 0 ) {
			SendMsg();
			Sleep(0);  // これ意味あるかなあ
		} else {
			// 送信メッセージがない状態で終了フラグが立ってたらそこで終わる
			if ( wait_term ) break;
			Sleep(1);  // 送信メッセージがないならちょっとスリープ
		}
	}

	// 後片付け
	midiOutReset(m_hMidi);
	midiOutClose(m_hMidi);
}

void CWin32Midi::ParseMsg()
{
	while ( m_nInBufPosR != m_nInBufPosW )
	{
		UINT8 mes = m_pInBuf[m_nInBufPosR];
		m_nInBufPosR = ( m_nInBufPosR + 1 ) & (MIDI_BUF_SIZE-1);

		// ここの対応はお好みで
		switch ( mes )
		{
		case MIDI_TIMING:
		case MIDI_START:
		case MIDI_CONTINUE:
		case MIDI_STOP:
		case MIDI_ACTIVESENSE:
		case MIDI_SYSTEMRESET:  // 一応イリーガル
			continue;
		}

		if ( mes & 0x80 ) {
			// コントロールバイト
			// 初回 or メッセージのデータ部にコントロールバイトが出た時（GENOCIDE2）
			if ( m_nState == MIDICTRL_READY || m_nState != MIDICTRL_EXCLUSIVE || mes != MIDI_EOX ) {
				// status
				m_nParseLen = 0;
				switch( mes & 0xF0 )
				{
				case 0xC0:
				case 0xD0:
					m_nState = MIDICTRL_2BYTES;
					break;
				case 0x80:
				case 0x90:
				case 0xA0:
				case 0xB0:
				case 0xE0:
					m_nLastMes = mes;  // この方が失敗しない
					m_nState = MIDICTRL_3BYTES;
					break;
				default:
					switch ( mes )
					{
					case MIDI_EXCLUSIVE:
						m_nState = MIDICTRL_EXCLUSIVE;
						break;
					case MIDI_TIMECODE:
						m_nState = MIDICTRL_TIMECODE;
						break;
					case MIDI_SONGPOS:
						m_nState = MIDICTRL_SYSTEM_3BYTE;
						break;
					case MIDI_SONGSELECT:
						m_nState = MIDICTRL_SYSTEM_2BYTE;
						break;
					case MIDI_TUNEREQUEST:
						m_nState = MIDICTRL_SYSTEM_1BYTE;
						break;
					default:
						continue;
					}
					break;
				}
			}
		} else {
			if ( m_nState == MIDICTRL_READY ) {
				// Key-onのみな気がしたんだけど忘れた…
				// running status
				m_pParseBuf[0] = m_nLastMes;
				m_nParseLen = 1;
				m_nState = MIDICTRL_3BYTES;
			}
		}

		m_pParseBuf[m_nParseLen++] = mes;

		switch ( m_nState )
		{
		case MIDICTRL_2BYTES:
			if ( m_nParseLen >= 2 ) {
				ReserveShortMsg(MAKE_SHORT_MSG(m_pParseBuf[0], m_pParseBuf[1], 0));
				m_nState = MIDICTRL_READY;
			}
			break;
		case MIDICTRL_3BYTES:
			if ( m_nParseLen >= 3 ) {
				ReserveShortMsg(MAKE_SHORT_MSG(m_pParseBuf[0], m_pParseBuf[1], m_pParseBuf[2]));
				m_nState = MIDICTRL_READY;
			}
			break;
		case MIDICTRL_EXCLUSIVE:
			if ( mes == MIDI_EOX ) {
				ReserveLongMsg(m_pParseBuf, m_nParseLen);
				m_nState = MIDICTRL_READY;
			}
			else if ( m_nParseLen >= MIDI_BUF_SIZE ) {  // おーばーふろー
				// ここに落ちたら以後正常に再生されないけど、まあ…
				m_nState = MIDICTRL_READY;
			}
			break;
		case MIDICTRL_TIMECODE:
			if ( m_nParseLen >= 2 ) {
				if ( ( mes == 0x7E ) || ( mes == 0x7F ) ) {
					// exclusiveと同じでいい筈…
					m_nState = MIDICTRL_EXCLUSIVE;
				}
				else {
					m_nState = MIDICTRL_READY;
				}
			}
			break;
		case MIDICTRL_SYSTEM_1BYTE:
			if ( m_nParseLen >= 1 ) {
				m_nState = MIDICTRL_READY;
			}
			break;
		case MIDICTRL_SYSTEM_2BYTE:
			if ( m_nParseLen >= 2 ) {
				m_nState = MIDICTRL_READY;
			}
			break;
		case MIDICTRL_SYSTEM_3BYTE:
			if ( m_nParseLen >= 3 ) {
				m_nState = MIDICTRL_READY;
			}
			break;
		}
	}
}

void CWin32Midi::ReserveShortMsg(UINT32 msg)
{
	// SendMsg()で再度DWORDに戻すので無駄なんだが…
	// まあメイン処理のパフォーマンスには影響しないしいいか
	MIDIMSG m;
	m.type = MIDIMSG_TYPE_SHORT;
	m.data.resize(3);
	m.data[0] = (UINT8)( ( msg >>  0 ) & 0xFF );
	m.data[1] = (UINT8)( ( msg >>  8 ) & 0xFF );
	m.data[2] = (UINT8)( ( msg >> 16 ) & 0xFF );
	m_listMsg.push_back(m);
}

void CWin32Midi::ReserveLongMsg(const UINT8* ptr, UINT32 size)
{
	MIDIMSG m;
	m.type = MIDIMSG_TYPE_LONG;
	m.data.resize(size);
	memcpy(m.data.data(), ptr, size);
	m_listMsg.push_back(m);
}

void CWin32Midi::ReserveWait()
{
	MIDIMSG m;
	m.type = MIDIMSG_TYPE_WAIT;
	m_listMsg.push_back(m);
}

void CWin32Midi::SendMsg()
{
	MMRESULT r;
	while ( ( m_listMsg.size() > 0 ) && ( !m_bInExclusive ) )
	{
		MIDIMSG& m = m_listMsg.front();
		switch ( m.type )
		{
		default:
		case MIDIMSG_TYPE_SHORT:
			r = midiOutShortMsg(m_hMidi, (DWORD)MAKE_SHORT_MSG(m.data[0], m.data[1], m.data[2]));
			break;
		case MIDIMSG_TYPE_LONG:
			memcpy(&m_pExBuf, m.data.data(), m.data.size());
			memset(&m_stExHeader, 0, sizeof(MIDIHDR));
			m_stExHeader.lpData = (LPSTR)m_pExBuf;
			m_stExHeader.dwBufferLength = (DWORD)m.data.size();
			r = midiOutPrepareHeader(m_hMidi, &m_stExHeader, sizeof(MIDIHDR));
			r = midiOutLongMsg(m_hMidi, &m_stExHeader, sizeof(MIDIHDR));
			m_bInExclusive = TRUE;
			break;
		case MIDIMSG_TYPE_WAIT:
			Sleep(50);
			break;
		}
		m_listMsg.pop_front();
	}
}

void CWin32Midi::SendReset()
{
	// Bx-79 : リセットオールコントローラー
	UINT32 msg;
	for (msg=0x79B0; msg<0x79C0; msg++) {
		ReserveShortMsg(msg);
	}
	// モジュールごとのリセット命令
	// 仕様上、リセット命令後 50ms は次を送ってはいけないらしいのでウェイトを挟む
	switch ( m_nModuleType )
	{
	default:
	case MIDI_MODULE_GM:
		ReserveLongMsg(EXCV_GMRESET, sizeof(EXCV_GMRESET));
		ReserveWait();
		break;
	case MIDI_MODULE_GS:
		ReserveLongMsg(EXCV_GSRESET, sizeof(EXCV_GSRESET));
		ReserveWait();
		break;
	case MIDI_MODULE_LA:
		ReserveLongMsg(EXCV_MTRESET, sizeof(EXCV_MTRESET));
		ReserveWait();
		break;
	case MIDI_MODULE_XG:
		// XG音源はGM→XGと送るのがスタンダードらしい
		ReserveLongMsg(EXCV_GMRESET, sizeof(EXCV_GMRESET));
		ReserveWait();
		ReserveLongMsg(EXCV_XGRESET, sizeof(EXCV_XGRESET));
		ReserveWait();
		break;
	}
}

void CWin32Midi::DoReset()
{
	// バッファはクリア、処理中のステートなどもクリア
	m_nParseLen = 0;
	m_nInBufPosW = 0;
	m_nInBufPosR = 0;
	m_nState = MIDICTRL_READY;
	m_nLastMes = 0x80;
	m_listMsg.clear();
	midiOutReset(m_hMidi);
	SendReset();
	// エクスクルーシブ送信途中の場合は終わるのを待つ必要があるので、ここでは m_bInExclusive は初期化してはいけない
}
