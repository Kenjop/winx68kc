/* -----------------------------------------------------------------------------------
  Memory access handler
                                                      (c) 2006-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

// 多段アレイ等の機能を削除しX68000専用化したメモリハンドラ

#include "osconfig.h"
#include "mem_manager.h"

#define MEM_BLOCK_SHIFT 13
#define MEM_BLOCK_SIZE  (1<<MEM_BLOCK_SHIFT)
#define MEM_BLOCK_MASK  (MEM_BLOCK_SIZE-1)
#define MEM_ADDR_MASK   0xFFFFFF

#ifdef _ENDIAN_LITTLE
  #define BE_ADRBIT     1
  #define LE_ADRBIT     0
#else
  #define BE_ADRBIT     0
  #define LE_ADRBIT     1
#endif


// --------------------------------------------------------------------------
//   構造体定義
// --------------------------------------------------------------------------
typedef struct {
	const UINT8* ptr;
	MEM16RHDLR   handler;
	void*        hdrprm;
} MEM16_RBLK;

typedef struct {
	UINT8*       ptr;
	MEM16WHDLR   handler;
	void*        hdrprm;
} MEM16_WBLK;

typedef struct {
	MEM16_RBLK  rblk[0x1000000/MEM_BLOCK_SIZE];
	MEM16_WBLK  wblk[0x1000000/MEM_BLOCK_SIZE];
	MEM16RHDLR  rerr_handler;
	MEM16WHDLR  werr_handler;
	void*       hdrprm;
} INFO_MEM16;


// --------------------------------------------------------------------------
//   公開関数
// --------------------------------------------------------------------------
// 初期化
MEM16HDL Mem16_Init(void* hdrprm, MEM16RHDLR hldr_rerr, MEM16WHDLR hldr_werr)
{
	INFO_MEM16* mem = (INFO_MEM16*)_MALLOC(sizeof(INFO_MEM16), "Memory struct");
	do {
		if ( !mem ) break;
		memset(mem, 0, sizeof(INFO_MEM16));
		mem->hdrprm = hdrprm;
		mem->rerr_handler = hldr_rerr;
		mem->werr_handler = hldr_werr;

		LOG(("Mamory manager initialized."));
		return (MEM16HDL)mem;
	} while ( 0 );

	Mem16_Cleanup((MEM16HDL)mem);
	return NULL;
}


// 破棄
void Mem16_Cleanup(MEM16HDL hdl)
{
	INFO_MEM16* mem = (INFO_MEM16*)hdl;
	if ( mem ) {
		_MFREE(mem);
	}
}


void Mem16_SetHandler(MEM16HDL hdl, UINT32 st, UINT32 ed, const void* rdmem, void* wrmem, MEM16RHDLR rdhdr, MEM16WHDLR wrhdr, void* hdrprm)
{
	INFO_MEM16* mem = (INFO_MEM16*)hdl;
	UINT32 sb = ( st & MEM_ADDR_MASK ) >> MEM_BLOCK_SHIFT;
	UINT32 eb = ( ed & MEM_ADDR_MASK ) >> MEM_BLOCK_SHIFT;
	UINT32 blk;

	if ( !mem ) return;

	for (blk=sb; blk<=eb; blk++) {
		MEM16_RBLK* rblk = &mem->rblk[blk];
		MEM16_WBLK* wblk = &mem->wblk[blk];
		rblk->ptr = ( rdmem ) ? (const UINT8*)rdmem - st : NULL;
		rblk->handler = ( rdhdr ) ? rdhdr  : ( ( rblk->ptr ) ? NULL : mem->rerr_handler );
		rblk->hdrprm  = ( rdhdr ) ? hdrprm : mem->hdrprm;
		wblk->ptr = ( wrmem ) ? (UINT8*)wrmem - st : NULL;
		wblk->handler = ( wrhdr ) ? wrhdr  : ( ( wblk->ptr ) ? NULL : mem->werr_handler );
		wblk->hdrprm  = ( wrhdr ) ? hdrprm : mem->hdrprm;
	}
}


// --------------------------------------------------------------------------
//   各メモリアクセスルーチン
//   ハンドラ経由時はEndian考慮はしないので、ハンドラ側で適宜変換すること
// --------------------------------------------------------------------------
#define READER_START(_name) \
UINT32 FASTCALL _name(MEM16HDL hdl, UINT32 adr) { \
    const INFO_MEM16* mem = (const INFO_MEM16*)hdl;          \
    const UINT32 a = adr & MEM_ADDR_MASK;                    \
    const MEM16_RBLK* rb = &mem->rblk[a>>MEM_BLOCK_SHIFT];   \

#define READER_END                                           \
	return 0;                                                \
}

#define WRITER_START(_name) \
void FASTCALL _name(MEM16HDL hdl, UINT32 adr, UINT32 data) { \
    const INFO_MEM16* mem = (const INFO_MEM16*)hdl;          \
    const UINT32 a = adr & MEM_ADDR_MASK;                    \
    const MEM16_WBLK* wb = &mem->wblk[a>>MEM_BLOCK_SHIFT];   \

#define WRITER_END                                           \
}


// --------------------------------------------
//   16bit I/O, BigEndian
// --------------------------------------------
READER_START(Mem16_Read8BE8)
	if ( rb->ptr ) {
		const UINT8* ptr = rb->ptr+(a^BE_ADRBIT);
		return (UINT32)READENDIANBYTE(ptr);
	} else if ( rb->handler ) {
		MEM16RHDLR hdlr = rb->handler;
		return (UINT32)hdlr(rb->hdrprm, adr);
	}
READER_END

READER_START(Mem16_Read16BE8)
	if ( rb->ptr ) {
		const UINT8* ptr = rb->ptr+(a&~1);
		return (UINT32)(UINT32)READENDIANWORD(ptr);
	} else if ( rb->handler ) {
		MEM16RHDLR hdlr = rb->handler;
		adr &= ~1;
		return (UINT32)((hdlr(rb->hdrprm, adr+0)<<8) | (hdlr(rb->hdrprm, adr+1)<<0));
	}
READER_END

READER_START(Mem16_Read32BE8)
	if ( rb->ptr ) {
		const UINT8* ptr = rb->ptr+(a&~1);
		UINT32 ret;
		ret  = READENDIANWORD(ptr)<<16; ptr += 2;
		ret |= READENDIANWORD(ptr);
		return ret;
	} else if ( rb->handler ) {
		MEM16RHDLR hdlr = rb->handler;
		adr &= ~1;
		return (UINT32)((hdlr(rb->hdrprm, adr+0)<<24) | (hdlr(rb->hdrprm, adr+1)<<16) | (hdlr(rb->hdrprm, adr+2)<<8) | (hdlr(rb->hdrprm, adr+3)<<0));
	}
READER_END

// ライトハンドラは、ポインタと関数の両方が定義されてる場合、ポインタ→関数の順で両方処理する（Dirty管理とか用）
WRITER_START(Mem16_Write8BE8)
	if ( wb->ptr ) {
		UINT8* ptr = wb->ptr+(a^BE_ADRBIT);
		WRITEENDIANBYTE(ptr, data);
	}
	if ( wb->handler ) {
		MEM16WHDLR hdlr = wb->handler;
		hdlr(wb->hdrprm, adr, data);
	}
WRITER_END

WRITER_START(Mem16_Write16BE8)
	if ( wb->ptr ) {
		UINT8* ptr = wb->ptr+(a&~1);
		WRITEENDIANWORD(ptr, data);
	}
	if ( wb->handler ) {
		MEM16WHDLR hdlr = wb->handler;
		adr &= ~1;
		hdlr(wb->hdrprm, adr+0, (data>> 8)&0xFF);
		hdlr(wb->hdrprm, adr+1, (data>> 0)&0xFF);
	}
WRITER_END

WRITER_START(Mem16_Write32BE8)
	if ( wb->ptr ) {
		UINT32 d;
		UINT8* ptr = wb->ptr+(a&~1);
		d = data>>16;    WRITEENDIANWORD(ptr, d); ptr += 2;
		d = data&0xffff; WRITEENDIANWORD(ptr, d);
	}
	if ( wb->handler ) {
		MEM16WHDLR hdlr = wb->handler;
		adr &= ~1;
		hdlr(wb->hdrprm, adr+0, (data>>24)&0xFF);
		hdlr(wb->hdrprm, adr+1, (data>>16)&0xFF);
		hdlr(wb->hdrprm, adr+2, (data>> 8)&0xFF);
		hdlr(wb->hdrprm, adr+3, (data>> 0)&0xFF);
	}
WRITER_END
