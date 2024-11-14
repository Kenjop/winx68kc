/* -----------------------------------------------------------------------------------
  "SHARP X68000" FDD
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _x68000_fdd_h_
#define _x68000_fdd_h_

#include "emu_driver.h"
#include "x68000_driver.h"
#include "x68000_ioc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	X68_SECTOR_STAT_OK           = 0x01,  // D88 stat 0x00
	X68_SECTOR_STAT_DEL          = 0x02,  // D88 stat 0x10
	X68_SECTOR_STAT_ID_CRC_ERR   = 0x04,  // D88 stat 0xA0
	X68_SECTOR_STAT_DT_CRC_ERR   = 0x08,  // D88 stat 0xB0
	X68_SECTOR_STAT_NO_ADDR_MARK = 0x10,  // D88 stat 0xE0
	X68_SECTOR_STAT_NO_DATA_MARK = 0x20,  // D88 stat 0xF0
	X68_SECTOR_STAT_NO_DATA      = 0x40,
} X68_SECTOR_STAT; 

#pragma pack(1)
typedef struct {
	UINT8 c;
	UINT8 h;
	UINT8 r;
	UINT8 n;
} X68_SECTOR_ID;
#pragma pack()

typedef struct __X68FDDHDL* X68FDD;

X68FDD X68FDD_Init(X68IOC ioc);
void X68FDD_Cleanup(X68FDD hdl);

void X68FDD_SetEjectCallback(X68FDD hdl, DISKEJECTCB cb, void* cbprm);

BOOL X68FDD_SetDisk(X68FDD hdl, SINT32 drive, const UINT8* img, UINT32 imgsz, X68K_DISK_TYPE type);
BOOL X68FDD_EjectDisk(X68FDD hdl, SINT32 drive, BOOL force);

BOOL X68FDD_Seek(X68FDD hdl, SINT32 drive, UINT32 track);
UINT32 X68FDD_Read(X68FDD hdl, SINT32 drive, const X68_SECTOR_ID* id, UINT8* buf, BOOL is_diag);  // return or-ed flag of X68_SECTOR_STAT
BOOL X68FDD_ReadID(X68FDD hdl, SINT32 drive, X68_SECTOR_ID* retid, BOOL for_error);             // return or-ed flag of X68_SECTOR_STAT
UINT32 X68FDD_Write(X68FDD hdl, SINT32 drive, const X68_SECTOR_ID* id, UINT8* buf, BOOL del);
BOOL X68FDD_WriteID(X68FDD hdl, SINT32 drive, UINT32 track, UINT8* buf, UINT32 num);
BOOL X68FDD_IsDriveReady(X68FDD hdl, SINT32 drive);
BOOL X68FDD_IsReadOnly(X68FDD hdl, SINT32 drive);
void X68FDD_SetWriteProtect(X68FDD hdl, SINT32 drive, BOOL sw);
void X68FDD_SetEjectMask(X68FDD hdl, SINT32 drive, BOOL sw);
void X68FDD_SetAccessBlink(X68FDD hdl, SINT32 drive, BOOL sw);
const UINT8* X68FDD_GetDiskImage(X68FDD hdl, SINT32 drive, UINT32* p_size);

void X68FDD_UpdateLED(X68FDD hdl, TUNIT t);
const INFO_X68FDD_LED* X68FDD_GetInfoLED(X68FDD hdl);

void X68FDD_LoadState(X68FDD hdl, STATE* state, UINT32 id);
void X68FDD_SaveState(X68FDD hdl, STATE* state, UINT32 id);

#ifdef __cplusplus
}
#endif

#endif // of _x68000_fdd_h_
