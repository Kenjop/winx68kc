/* -----------------------------------------------------------------------------------
  OS dependent configurations
                                                      (c) 2004-07 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _os_config_h_
#define _os_config_h_

// --------------------------------------------------------------------------
//   Win32環境設定
// --------------------------------------------------------------------------
#if defined(WIN32)

#ifdef _DEBUG
#if (_MSC_VER>=800)
#define _CRT_SECURE_NO_DEPRECATE
#include <crtdbg.h>
#endif
#endif

// 仮想マシン駆動タイマの単位としてdouble型を使う定義
// コメントアウトするとfloat型を使う（多分現代環境ではfloatの速度有利はほぼない…と思う）
#define USE_DOUBLE_AS_TIMERUNIT

// BigEndian環境ではコメントアウト
#define _ENDIAN_LITTLE

// Endianが一致するメモリアクセスで、キャストによる直接アクセスを許可する
// メモリアクセスにalignment制限がある環境ではコメントアウト
#define _CAST_MEM_ACEESS

#ifdef _DEBUG
// ログウィンドウの表示
#define _DEBUG_LOG
// メモリログの表示
//#define _DEBUG_MEM
#endif


// ---------------- 基本include
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <tchar.h>


// ---------------- 関数呼び出し型
#define STDCALL   __stdcall
#define CALLBACK  __stdcall
#define INLINE    __inline
#define FASTCALL  __fastcall
#define IOINLINE


// ---------------- 基本形定義
#define	TRUE             1
#define	FALSE            0
#ifndef NULL
#define NULL             (0)
#endif
typedef int              BOOL;
typedef	signed char      SINT8;
typedef	unsigned char    UINT8;
typedef	signed short     SINT16;
typedef	unsigned short   UINT16;
typedef	signed int       SINT32;
typedef	unsigned int     UINT32;
typedef	signed __int64   SINT64;
typedef	unsigned __int64 UINT64;

#include "inttypes.h"

#define MEMCPY(d,s,sz)   memcpy(d,s,sz)
#define MEMCLR(d,sz)     memset(d,0,sz)

// ---------------- デバッグ用
#ifdef _DEBUG_MEM
	#ifdef __cplusplus
	extern "C" {
	#endif
		void* _Debug_Malloc(int size, const char* name);
		void* _Debug_Free(void* p);
		int   _Debug_MemUsed(void);
		void memtrace_init(HINSTANCE hinst, HINSTANCE hprev);
		void memtrace_term(void);
	#ifdef __cplusplus
	}
	#endif

	#define	_MALLOC(a, b)        _Debug_Malloc(a, b)
	#define	_MFREE(a)            _Debug_Free(a)
	#define	_MEM_USED()          _Debug_MemUsed()
	#define	_MEMINFO()           _Debug_GetMemInfo()
	#define _MEMTRACE_INIT(a, b) memtrace_init(a, b)
	#define _MEMTRACE_END()      memtrace_term()
#else //!_DEBUG_MEM
	#define	_MALLOC(a, b)        malloc(a)
	#define	_MFREE(a)            free(a)
	#define	_MALLOC2(a, b)       _MALLOC(a,b)
	#define	_MFREE2(a)           _MFREE(a)
	#define	_MEM_USED()
	#define	_MEMINFO()
	#define _MEMTRACE_INIT(a, b)
	#define _MEMTRACE_END()
#endif //_DEBUG_MEM

#ifdef _DEBUG_LOG
	#ifdef __cplusplus
	extern "C" {
	#endif
	void Trace_Init(HWND hwnd, HINSTANCE hinst, HINSTANCE hprev);
	void Trace_Clean(void);
	void Trace_Out(const char *str, ...);
	void TraceW_Out(const TCHAR *str, ...);
	#ifdef __cplusplus
	}
	#endif
	#define	LOGSTART(a,b,c) Trace_Init(a,b,c)
	#define	LOGEND()        Trace_Clean()
	#define	LOGOUT          Trace_Out
	#define	LOGWOUT         TraceW_Out
	#define	LOG(a)          LOGOUT a
	#define	LOGW(a)         LOGWOUT a
#else // !_DEBUG_LOG
	#define	LOGSTART(a,b,c)
	#define	LOGEND()
	#define	LOGOUT
	#define	LOGWOUT
	#define	LOG(a)
	#define	LOGW(a)
#endif // of _DEBUG_LOG

#define MUTEX_DEFINE(m)     CRITICAL_SECTION m
#define MUTEX_INIT(m)       InitializeCriticalSection(&m)
#define MUTEX_LOCK(m)       EnterCriticalSection(&m)
#define MUTEX_UNLOCK(m)     LeaveCriticalSection(&m)
#define MUTEX_RELEASE(m)    DeleteCriticalSection(&m)

#define EVENT_DEFINE(m)     HANDLE m
#define EVENT_INIT(m)       m = CreateEvent(NULL, FALSE, FALSE, NULL)
#define EVENT_RELEASE(m)    CloseHandle(m)
#define EVENT_SIGNAL(m)     SetEvent(m)
#define EVENT_WAIT(m)       WaitForSingleObject(m, INFINITE)
#define EVENT_CHECK(m)      WaitForSingleObject(m,0)==WAIT_OBJECT_0
#define THREAD_EXIT         ExitThread(0)

#if defined(_WIN64)
	#define GWL_USERDATA	GWLP_USERDATA
	#define GWL_HINSTANCE	GWLP_HINSTANCE
#endif


#endif  // defined(WIN32)


// --------------------------------------------------------------------------
//   共通マクロ
// --------------------------------------------------------------------------
// ---------------- メモリアクセス（エンディアン吸収用）
#if defined(_ENDIAN_LITTLE) && defined(_CAST_MEM_ACEESS)
	#define	READLEDWORD(a)      ( *(UINT32*)(a) )
	#define	READLEWORD(a)       ( *(UINT16*)(a) )
	#define	READLEBYTE(a)       ( *(UINT8*)(a) )
	#define	WRITELEDWORD(a,b)   *(UINT32*)(a) = (UINT32)(b)
	#define	WRITELEWORD(a,b)    *(UINT16*)(a) = (UINT16)(b)
	#define	WRITELEBYTE(a,b)    *(UINT8*)(a) = (UINT8)(b)
#else
	#define	READLEDWORD(a)   ( (((UINT32)(((UINT8*)(a))[0]))    ) | \
							   (((UINT32)(((UINT8*)(a))[1]))<<8 ) | \
							   (((UINT32)(((UINT8*)(a))[2]))<<16) | \
							   (((UINT32)(((UINT8*)(a))[3]))<<24) )

	#define	READLEWORD(a)    ( (((UINT32)(((UINT8*)(a))[0]))    ) | \
							   (((UINT32)(((UINT8*)(a))[1]))<<8 ) )

	#define	READLEBYTE(a)      ( *(UINT8*)(a) )

	#define	WRITELEDWORD(a,b)  ((UINT8*)(a))[0] = (UINT8)((b)    ); \
							   ((UINT8*)(a))[1] = (UINT8)((b)>>8 ); \
							   ((UINT8*)(a))[2] = (UINT8)((b)>>16); \
							   ((UINT8*)(a))[3] = (UINT8)((b)>>24);

	#define	WRITELEWORD(a,b)   ((UINT8*)(a))[0] = (UINT8)((b)    ); \
							   ((UINT8*)(a))[1] = (UINT8)((b)>>8 );

	#define	WRITELEBYTE(a,b)   ((UINT8*)(a))[0] = (UINT8)((b)    );
#endif


#if (!defined(_ENDIAN_LITTLE)) && defined(_CAST_MEM_ACEESS)
	#define	READBEDWORD(a)      ( *(UINT32*)(a) )
	#define	READBEWORD(a)       ( *(UINT16*)(a) )
	#define	READBEBYTE(a)       ( *(UINT8*)(a) )
	#define	WRITEBEDWORD(a,b)   *(UINT32*)(a) = (UINT32)(b)
	#define	WRITEBEWORD(a,b)    *(UINT16*)(a) = (UINT16)(b)
	#define	WRITEBEBYTE(a,b)    *(UINT8*)(a) = (UINT8)(b)
#else
	#define	READBEDWORD(a)   ( (((UINT32)(((UINT8*)(a))[3]))    ) | \
							   (((UINT32)(((UINT8*)(a))[2]))<<8 ) | \
							   (((UINT32)(((UINT8*)(a))[1]))<<16) | \
							   (((UINT32)(((UINT8*)(a))[0]))<<24) )

	#define	READBEWORD(a)    ( (((UINT32)(((UINT8*)(a))[1]))    ) | \
							   (((UINT32)(((UINT8*)(a))[0]))<<8 ) )

	#define	READBEBYTE(a)      ( *(UINT8*)(a) )

	#define	WRITEBEDWORD(a,b)  ((UINT8*)(a))[3] = (UINT8)((b)    ); \
							   ((UINT8*)(a))[2] = (UINT8)((b)>>8 ); \
							   ((UINT8*)(a))[1] = (UINT8)((b)>>16); \
							   ((UINT8*)(a))[0] = (UINT8)((b)>>24);

	#define	WRITEBEWORD(a,b)   ((UINT8*)(a))[1] = (UINT8)((b)    ); \
							   ((UINT8*)(a))[0] = (UINT8)((b)>>8 );

	#define	WRITEBEBYTE(a,b)   ((UINT8*)(a))[0] = (UINT8)((b)    );
#endif

#if defined(_ENDIAN_LITTLE)
	#define MAKELEBYTE(a)   a
	#define MAKELEWORD(a)   a
	#define MAKELEDWORD(a)  a
	#define MAKEBEBYTE(a)   a
	#define MAKEBEWORD(a)   ( (((a)&0x00ff)<<8)|(((a)&0xff00)>>8) )
	#define MAKEBEDWORD(a)  ( (((a)&0x000000ff)<<24) | \
	                          (((a)&0x0000ff00)<<8)  | \
	                          (((a)&0x00ff0000)>>8)  | \
	                          (((a)&0xff000000)>>24) )
#else
	#define MAKELEBYTE(a)   a
	#define MAKELEWORD(a)   ( (((a)&0x00ff)<<8)|(((a)&0xff00)>>8) )
	#define MAKELEDWORD(a)  ( (((a)&0x000000ff)<<24) | \
	                          (((a)&0x0000ff00)<<8)  | \
	                          (((a)&0x00ff0000)>>8)  | \
	                          (((a)&0xff000000)>>24) )
	#define MAKEBEBYTE(a)   a
	#define MAKEBEWORD(a)   a
	#define MAKEBEDWORD(a)  a
#endif


// その他のマクロ
#define NUMLIMIT(n,mn,mx)  ((n)<(mn))?(mn):(((n)>(mx))?(mx):(n))
#define _MIN(a,b)   (((a)<(b))?(a):(b))
#define _MAX(a,b)   (((a)>(b))?(a):(b))

// タイマ関連
#include <float.h>
#ifdef USE_DOUBLE_AS_TIMERUNIT
	// double型を使う場合
	typedef double TUNIT;
	#define TUNIT_ZERO            (0.0)
	#define TUNIT_NEVER           (DBL_MAX)
	#define TUNIT2DBL(t)          (double)(t)
	#define DBL2TUNIT(d)          (TUNIT)(d)
#else
	// UINT32固定小数を使う場合
	typedef float TUNIT;
	#define TUNIT_ZERO            (0.0f)
	#define TUNIT_NEVER           (FLT_MAX)
	#define TUNIT2DBL(t)          (double)(t)
	#define DBL2TUNIT(d)          (TUNIT)(d)
#endif


// GCCなら強制inline展開指定が使えるはず
#ifdef __GNUC__
#define FORCE_INLINE(retval,funcname,arglist)                                 \
	static __inline__ retval funcname arglist __attribute__((always_inline)); \
	static __inline__ retval funcname arglist
#else
#define FORCE_INLINE(retval,funcname,arglist)                                 \
	static INLINE retval funcname arglist
#endif


#ifdef _ENDIAN_LITTLE
#define READENDIANBYTE   READLEBYTE
#define READENDIANWORD   READLEWORD
#define READENDIANDWORD  READLEDWORD
#define WRITEENDIANBYTE  WRITELEBYTE
#define WRITEENDIANWORD  WRITELEWORD
#define WRITEENDIANDWORD WRITELEDWORD
#else  // Big Endian
#define READENDIANBYTE   READBEBYTE
#define READENDIANWORD   READBEWORD
#define READENDIANDWORD  READBEDWORD
#define WRITEENDIANBYTE  WRITEBEBYTE
#define WRITEENDIANWORD  WRITEBEWORD
#define WRITEENDIANDWORD WRITEBEDWORD
#endif


#endif // of _os_config_h_
