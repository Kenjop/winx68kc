/* -----------------------------------------------------------------------------------
  Memory access handler
                                                      (c) 2006-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#ifndef _mem_manager_h_
#define _mem_manager_h_

#ifdef __cplusplus
extern "C" {
#endif

#define MEM16ID_HANDLER_ACCESS  0
#define MEM16ID_IGNORE_ACCESS   0xFFFFFFFE
#define MEM16ID_LISTEND         0xFFFFFFFF

typedef struct __MEMORY16_HDL* MEM16HDL;

typedef UINT32 (FASTCALL *_MEM16RHDLR)(void* prm, UINT32 adr);
typedef void   (FASTCALL *_MEM16WHDLR)(void* prm, UINT32 adr, UINT32 data);
typedef _MEM16RHDLR  MEM16RHDLR;
typedef _MEM16WHDLR  MEM16WHDLR;

#define MEM16R_HANDLER(name)  UINT32 FASTCALL name(void* prm, UINT32 adr)
#define MEM16W_HANDLER(name)  void FASTCALL name(void* prm, UINT32 adr, UINT32 data)

typedef struct {
	UINT32     areaid;
	UINT32     st, ed;
	MEM16RHDLR handler;
	void*      hdlprm;
	UINT32     adrmask;
} MEM16RHDLINFO;

typedef struct {
	UINT32     areaid;
	UINT32     st, ed;
	MEM16WHDLR handler;
	void*      hdlprm;
	UINT32     adrmask;
} MEM16WHDLINFO;


MEM16HDL Mem16_Init(void* prm, MEM16RHDLR hldr_rerr, MEM16WHDLR hldr_werr);
void Mem16_Cleanup(MEM16HDL hdl);

void Mem16_SetHandler(MEM16HDL hdl, UINT32 st, UINT32 ed, const void* rdmem, void* wrmem, MEM16RHDLR rdhdr, MEM16WHDLR wrhdr, void* hdrprm);

UINT32 FASTCALL Mem16_Read8BE8(MEM16HDL hdl, UINT32 adr);
UINT32 FASTCALL Mem16_Read16BE8(MEM16HDL hdl, UINT32 adr);
UINT32 FASTCALL Mem16_Read32BE8(MEM16HDL hdl, UINT32 adr);
void   FASTCALL Mem16_Write8BE8(MEM16HDL hdl, UINT32 adr, UINT32 data);
void   FASTCALL Mem16_Write16BE8(MEM16HDL hdl, UINT32 adr, UINT32 data);
void   FASTCALL Mem16_Write32BE8(MEM16HDL hdl, UINT32 adr, UINT32 data);

#ifdef __cplusplus
}
#endif

#endif // of _mem_manager_h_
