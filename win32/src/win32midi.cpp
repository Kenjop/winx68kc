/* -----------------------------------------------------------------------------------
  Windows MIDI support
                                                         (c) 2024 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	����ҁ[�ł̂䂢����̎��������ɃX���b�h����������
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
//   ���C���X���b�h���̏���
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
		// �K���R�[���o�b�N�o�^����ɃX���b�h�����
		MidiThreadCreate();
		X68kDriver_SetMidiCallback(m_pDrv, &CWin32Midi::MidiCallback, (void*)this);
	}
}

void CWin32Midi::Cleanup()
{
	// �K����ɃR�[���o�b�N����
	X68kDriver_SetMidiCallback(m_pDrv, NULL, (void*)0);
	MidiThreadTerminate();
}

void CWin32Midi::ApplyConfig()
{
	if ( m_nDeviceId != m_pCfg->mDlgCfg.nMidiDeviceId ) {
		// �f�o�C�X���ύX����Ă���ꍇ�A�X���b�h�̍폜���č쐬���s���iMidiOut���X���b�h���ŊJ���Ă��邽�߁j
		Cleanup();
		// m_nDeviceId / m_nModuleType �� Init() �̖`���ōX�V�����
		Init(m_pDrv, m_pCfg);
	} else {
		// ���W���[���͕ϐ������ւ������ł悢
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
	// �N���e�B�J���Z�N�V�����ŕی삳��Ă鏈�����g��
	MidiCallbackMain(MIDIFUNC_RESET, 0);
}

void CALLBACK CWin32Midi::MidiCallback(void* prm, MIDI_FUNCTIONS func, UINT8 data)
{
	// X68000�R�A����̃R�[���o�b�N�ŌĂ΂��
	CWin32Midi* _this = reinterpret_cast<CWin32Midi*>(prm);
	_this->MidiCallbackMain(func, data);
}

void CWin32Midi::MidiCallbackMain(MIDI_FUNCTIONS func, UINT8 data)
{
	// �G���[���̓o�b�t�@��������Ȃ��Ȃ�̂ł����ŋA��
	if ( m_bError ) return;

	switch ( func ) 
	{
	default:
	case MIDIFUNC_RESET:
		EnterCriticalSection(&m_csLock);
		// �V�O�i�����グ�邾���i�������̓X���b�h�����ōs���j
		m_bDoReset = TRUE;
		LeaveCriticalSection(&m_csLock);
		break;

	case MIDIFUNC_DATAOUT:
		{
			EnterCriticalSection(&m_csLock);
			UINT32 next_posw = ( m_nInBufPosW + 1 ) & (MIDI_BUF_SIZE-1);
			if ( next_posw != m_nInBufPosR ) {  // �󂫂�����
				m_pInBuf[m_nInBufPosW] = data;
				m_nInBufPosW = next_posw;
			}
			LeaveCriticalSection(&m_csLock);
		}
		break;
	}
}


// --------------------------------------------------------------------------
//   MIDI�X���b�h���̏���
// --------------------------------------------------------------------------
DWORD WINAPI CWin32Midi::MidiThread(LPVOID prm)
{
	CWin32Midi* _this = reinterpret_cast<CWin32Midi*>(prm);
	// �������� _this �ŃA�N�Z�X����̂߂�ǂ��̂œ]��
	_this->MidiThreadMain();
	return 0;
}

void CWin32Midi::MidiThreadMain()
{
	// �X���b�h���ŎQ�Ƃ���ϐ���������
	m_nParseLen = 0;
	m_nInBufPosW = 0;
	m_nInBufPosR = 0;
	m_nState = MIDICTRL_READY;
	m_nLastMes = 0x80;
	m_listMsg.clear();
	m_bInExclusive = FALSE;
	memset(&m_stExHeader, 0, sizeof(m_stExHeader));

	// �f�o�C�X�I�[�v��
	// �����X���b�h����̃A�N�Z�X�ɂȂ�Ȃ��悤�A�X���b�h�`���ŃI�[�v�����A�X���b�h�����ŃN���[�Y����
	// �i�����X���b�h�ׂ��ł̎Q�Ƃł��������Ȃ�Ƃ͎v��Ȃ����c�j
	if ( midiOutOpen(&m_hMidi, m_nDeviceId, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR ) {
		midiOutReset(m_hMidi);
		m_bError = FALSE;
	} else {
		// �G���[����
		m_bError = TRUE;
		return;
	}

	// ���Z�b�g���M�i���L���[�ɐςށj
	SendReset();

	BOOL wait_term = FALSE;

	while ( 1 )
	{
		MSG msg;

		// ���񑩂̃��b�Z�[�W����
		while ( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) ) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			// �I�����b�Z�[�W��������I���
			if ( msg.message==WM_QUIT ) {
				wait_term = TRUE;
				// ������O�Ƀ��Z�b�g�R�}���h�𗬂�
				// SendReset �� m_listMsg �Ƀ��b�Z�[�W�𒼐ڐςނ̂ŁA���Z�b�g���I���܂ł͂��̃��[�v����
				// �����Ȃ��悤�ɂȂ��Ă���A�͂�
				SendReset();
			}
		}

		// ���Z�b�g�V�O�i�����������烊�Z�b�g����
		EnterCriticalSection(&m_csLock);
		if ( m_bDoReset ) {
			DoReset();
			m_bDoReset = FALSE;
		}
		LeaveCriticalSection(&m_csLock);

		// �G�N�X�N���[�V�u���M���Ȃ�ҋ@
		if ( m_bInExclusive ) {
			MMRESULT r = midiOutUnprepareHeader(m_hMidi, &m_stExHeader, sizeof(MIDIHDR));
			if ( r == MIDIERR_STILLPLAYING ) {
				Sleep(1);
				continue;
			}
			m_bInExclusive = FALSE;
		}

		// ��M�o�b�t�@���e���p�[�X���ă��b�Z�[�W���i�v�N���e�B�J���Z�N�V�����ی�j
		if ( !wait_term ) {
			// �I���������͐V���ȃ��b�Z�[�W��荞�݂͂��Ȃ�
			EnterCriticalSection(&m_csLock);
			if ( m_nInBufPosR != m_nInBufPosW ) {
				ParseMsg();
			}
			LeaveCriticalSection(&m_csLock);
		}

		// ���b�Z�[�W���M����
		if ( m_listMsg.size() > 0 ) {
			SendMsg();
			Sleep(0);  // ����Ӗ����邩�Ȃ�
		} else {
			// ���M���b�Z�[�W���Ȃ���ԂŏI���t���O�������Ă��炻���ŏI���
			if ( wait_term ) break;
			Sleep(1);  // ���M���b�Z�[�W���Ȃ��Ȃ炿����ƃX���[�v
		}
	}

	// ��Еt��
	midiOutReset(m_hMidi);
	midiOutClose(m_hMidi);
}

void CWin32Midi::ParseMsg()
{
	while ( m_nInBufPosR != m_nInBufPosW )
	{
		UINT8 mes = m_pInBuf[m_nInBufPosR];
		m_nInBufPosR = ( m_nInBufPosR + 1 ) & (MIDI_BUF_SIZE-1);

		// �����̑Ή��͂��D�݂�
		switch ( mes )
		{
		case MIDI_TIMING:
		case MIDI_START:
		case MIDI_CONTINUE:
		case MIDI_STOP:
		case MIDI_ACTIVESENSE:
		case MIDI_SYSTEMRESET:  // �ꉞ�C���[�K��
			continue;
		}

		if ( mes & 0x80 ) {
			// �R���g���[���o�C�g
			// ���� or ���b�Z�[�W�̃f�[�^���ɃR���g���[���o�C�g���o�����iGENOCIDE2�j
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
					m_nLastMes = mes;  // ���̕������s���Ȃ�
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
				// Key-on�݂̂ȋC�������񂾂��ǖY�ꂽ�c
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
			else if ( m_nParseLen >= MIDI_BUF_SIZE ) {  // ���[�΁[�ӂ�[
				// �����ɗ�������Ȍ㐳��ɍĐ�����Ȃ����ǁA�܂��c
				m_nState = MIDICTRL_READY;
			}
			break;
		case MIDICTRL_TIMECODE:
			if ( m_nParseLen >= 2 ) {
				if ( ( mes == 0x7E ) || ( mes == 0x7F ) ) {
					// exclusive�Ɠ����ł������c
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
	// SendMsg()�ōēxDWORD�ɖ߂��̂Ŗ��ʂȂ񂾂��c
	// �܂����C�������̃p�t�H�[�}���X�ɂ͉e�����Ȃ���������
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
	// Bx-79 : ���Z�b�g�I�[���R���g���[���[
	UINT32 msg;
	for (msg=0x79B0; msg<0x79C0; msg++) {
		ReserveShortMsg(msg);
	}
	// ���W���[�����Ƃ̃��Z�b�g����
	// �d�l��A���Z�b�g���ߌ� 50ms �͎��𑗂��Ă͂����Ȃ��炵���̂ŃE�F�C�g������
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
		// XG������GM��XG�Ƒ���̂��X�^���_�[�h�炵��
		ReserveLongMsg(EXCV_GMRESET, sizeof(EXCV_GMRESET));
		ReserveWait();
		ReserveLongMsg(EXCV_XGRESET, sizeof(EXCV_XGRESET));
		ReserveWait();
		break;
	}
}

void CWin32Midi::DoReset()
{
	// �o�b�t�@�̓N���A�A�������̃X�e�[�g�Ȃǂ��N���A
	m_nParseLen = 0;
	m_nInBufPosW = 0;
	m_nInBufPosR = 0;
	m_nState = MIDICTRL_READY;
	m_nLastMes = 0x80;
	m_listMsg.clear();
	midiOutReset(m_hMidi);
	SendReset();
	// �G�N�X�N���[�V�u���M�r���̏ꍇ�͏I���̂�҂K�v������̂ŁA�����ł� m_bInExclusive �͏��������Ă͂����Ȃ�
}
