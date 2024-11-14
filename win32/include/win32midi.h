/* -----------------------------------------------------------------------------------
  Windows MIDI support
                                                         (c) 2024 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _win32_midi_h
#define _win32_midi_h

#include "osconfig.h"
#include "x68000_driver.h"
#include "win32config.h"
#include <list>
#include <vector>

using namespace std;

class CWin32Midi
{
public:
	CWin32Midi();
	virtual ~CWin32Midi();

	void Init(EMUDRIVER* drv, CWin32Config* cfg);
	void Cleanup();
	void ApplyConfig();
	void SignalReset();

private:
	// ���C���X���b�h��
	void MidiThreadCreate();
	void MidiThreadTerminate();
	static void CALLBACK MidiCallback(void* prm, MIDI_FUNCTIONS func, UINT8 data);
	void MidiCallbackMain(MIDI_FUNCTIONS func, UINT8 data);

	// MIDI�X���b�h��
	static DWORD WINAPI MidiThread(LPVOID prm);
	void MidiThreadMain();
	void ParseMsg();
	void SendMsg();
	void ReserveShortMsg(UINT32 msg);
	void ReserveLongMsg(const UINT8* ptr, UINT32 size);
	void ReserveWait();
	void SendReset();
	void DoReset();

	// ���������񂶂�Ȃ����Ȃ��c
	static const UINT32 MIDI_BUF_SIZE = 512;

	// �p�[�X�����p�X�e�[�g
	typedef enum {
		MIDICTRL_READY = 0,
		MIDICTRL_2BYTES,
		MIDICTRL_3BYTES,
		MIDICTRL_EXCLUSIVE,
		MIDICTRL_TIMECODE,
		MIDICTRL_SYSTEM_1BYTE,
		MIDICTRL_SYSTEM_2BYTE,
		MIDICTRL_SYSTEM_3BYTE,
	} MIDICTRL_STATE;

	// �p�[�X���ꂽ���b�Z�[�W���X�g�p
	enum {
		MIDIMSG_TYPE_SHORT = 0,
		MIDIMSG_TYPE_LONG,
		MIDIMSG_TYPE_WAIT,
	};
	typedef struct {
		UINT32 type;
		vector<UINT8> data;
	} MIDIMSG;

	// �K�{�I�u�W�F�N�g�A�n���h��
	EMUDRIVER*       m_pDrv;
	CWin32Config*    m_pCfg;
	HMIDIOUT         m_hMidi;
	HANDLE           m_hThread;
	DWORD            m_dwThreadId;
	CRITICAL_SECTION m_csLock;
	BOOL             m_bError;
	BOOL             m_bDoReset;

	// �R���t�B�O����
	SINT32           m_nDeviceId;
	UINT32           m_nModuleType;

	// X68000�����瑗���Ă��鑗�M�f�[�^�o�b�t�@
	UINT8            m_pInBuf[MIDI_BUF_SIZE];
	UINT32           m_nInBufPosR;
	UINT32           m_nInBufPosW;

	// ���b�Z�[�W�p�[�X�Ɋւ������
	MIDICTRL_STATE   m_nState;
	UINT8            m_nLastMes;
	BOOL             m_bInExclusive;
	UINT8            m_pParseBuf[MIDI_BUF_SIZE];
	UINT32           m_nParseLen;
	list<MIDIMSG>    m_listMsg;

	// �G�N�X�N���[�V�u���M�p�Œ�o�b�t�@
	MIDIHDR          m_stExHeader;
	UINT8            m_pExBuf[MIDI_BUF_SIZE];
};

#endif //_win32_midi_h
