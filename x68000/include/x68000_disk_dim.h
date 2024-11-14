/* -----------------------------------------------------------------------------------
  "SHARP X68000" FloppyDisk (DIM format)
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	取敢えず最低限 KnightArms（XDFではゲーム開始できない）が起動できる程度の実装
	→ まーた起動しなくなってる…（FDC側かもしんない）

	DIM（DIFC）ヘッダ情報
	+$00      フォーマットタイプ（$00:2HD, $01:2HS, $02:2HC, $03:2HDE, $09:2HQ, $11:N88）
	+$01-$AA  トラック情報保持フラグ（$00:トラック情報なし, $01:あり）
	+$AB-$B9  ヘッダ文字列（"DIFC HEADER  ",$00,$00）
	+$BA-$BD  ファイル作成日
	+$BE-$C1  ファイル作成時間
	+$C2-$FD  コメント文字列
	+$FE      作成に使用した DIFC.X のバージョン（$19 ならば Ver.1.19）
	+$FF      オーバートラック（$00:なし、$01～:トラック数）

	トラック情報「なし」のトラックは MAM(Deleted?) として扱うのが正しい？（KnightArms）
*/

// DIM Disk Type
enum {
	DIM_2HD = 0,
	DIM_2HS,
	DIM_2HC,
	DIM_2HDE,
	DIM_2HQ = 9,
	DIM_TYPE_MAX = 10  // N88 はサポートしない
};

typedef struct {
	UINT32  track;
	UINT32  sector;
	UINT32  type;
	UINT32  trk_sz;
	UINT8   enable[170];      // 各トラックが有効かどうか（）
	UINT8   raw[1024*9*170];  // ベタイメージデータ（最大値で確保）
} DIM_PRM;

#define DIM_HEADER_SIZE  0x100
#define DIM_MAX_TRACK    170

/*                                       2HD   2HS   2HC  2HDE   ---   ---   ---   ---   ---   2HQ */
static const UINT32 DIM_SECT_SIZE[] = { 1024, 1024,  512, 1024,    0,    0,    0,    0,    0,  512 };
static const UINT32 DIM_SECT_NUM [] = {    8,    9,   15,    9,    0,    0,    0,    0,    0,   18 };


// --------------------------------------------------------------------------
//   内部使用
// --------------------------------------------------------------------------
static SINT32 GetSectorOfs(const DIM_PRM* prm, const X68_SECTOR_ID* id)
{
	UINT32 type = prm->type;
	UINT32 c = id->c, h = id->h, r = id->r, n = id->n;
	UINT32 ret = 0;

	do {
		UINT32 track;
		/*
			9SCDRV では（恐らくフォーマット判別のために）2HS/2HDE で特殊なIDを使っている
			- 2HS  : セクタ1以外、R+0x09（例：CHRN=00/00/01/03、00/00/0B/03、00/00/0C/03 ... 00/00/12/03）
			- 2HDE : セクタ1以外、H+0x80（例：CHRN=00/00/01/03、00/80/02/03、00/80/03/03 ... 00/80/09/03）
		*/
		// 9SCDRV用
		if ( type==DIM_2HS && r >= 0x0B ) r -= 0x09;
		if ( type==DIM_2HDE ) h &= 0x01;

		// IDチェック
		if ( c >= (DIM_MAX_TRACK/2) ) break;
		if ( h > 1 ) break;
		if ( r < 1 || r > DIM_SECT_NUM[type] ) break;
		if ( ( DIM_SECT_SIZE[type] >> n ) != 128 ) break;

		track = ( c << 1 ) + h;
		if ( !prm->enable[track] ) break;
		ret = track * prm->trk_sz;
		ret += ( r - 1 ) << ( n + 7 );  // 10 or 9 bit shift = x1024 or x512

		return ret;
	} while ( 0 );

	return -1;
}

static UINT32 IncSector(const DIM_PRM* prm, UINT32 r)
{
	UINT32 type = prm->type;
	return ( r + 1 ) % DIM_SECT_NUM[prm->type];
}


// --------------------------------------------------------------------------
//   FDD用標準関数群
// --------------------------------------------------------------------------
UINT32 DIM_GetPrmSize(void)
{
	return sizeof(DIM_PRM);
}

BOOL DIM_SetDisk(INFO_DISK* disk)
{
	DIM_PRM* prm = _MALLOC(sizeof(DIM_PRM), "");
	do {
		UINT32 type;
		UINT32 trk_sz;
		UINT32 track;
		UINT32 ofs;

		// メモリが確保できない場合エラー
		if ( !prm ) break;
		// ディスクサイズエラー
		if ( disk->size < DIM_HEADER_SIZE ) break;

		// 有効トラックフラグ
		memcpy(prm->enable, &disk->image[1], 170);
		// E5 で埋めておく
		memset(prm->raw, 0xE5, sizeof(prm->raw));

		// フォーマットタイプ確認
		type = disk->image[0x00];
		if ( type >= DIM_TYPE_MAX ) break;
		if ( DIM_SECT_SIZE[type] == 0 ) break;

		// トラック位置の計算
		ofs = DIM_HEADER_SIZE;
		trk_sz = DIM_SECT_SIZE[type] * DIM_SECT_NUM [type];
		for (track=0; track<DIM_MAX_TRACK; track++) {
			// トラック情報がある場合のみ ofs が進行、存在しないトラック分は前に詰められる
			if ( disk->image[0x01+track] ) {
				memcpy(prm->raw+(track*trk_sz), disk->image+ofs, trk_sz);
				ofs += trk_sz;
			}
		}
		// 最終位置がサイズを超えてたらエラー
		if ( ofs > disk->size ) {
			LOG(("### DIM : disk size error (required=%d, size=%d)", ofs, disk->size));
			break;
		}

		// ここまで来たら成功
		prm->track = 0;
		prm->sector = 0;
		prm->type = type;
		prm->trk_sz = trk_sz;
		disk->prm = (void*)prm;

		return TRUE;
	} while ( 0 );

	// エラー落ちした場合
	if ( prm ) _MFREE(prm);
	disk->prm = NULL;
	return FALSE;
}

BOOL DIM_EjectDisk(INFO_DISK* disk)
{
	DIM_PRM* prm = (DIM_PRM*)disk->prm;
	if ( prm ) _MFREE(prm);
	disk->prm = NULL;
	return FALSE;
}

BOOL DIM_Seek(INFO_DISK* disk, UINT32 track)
{
	DIM_PRM* prm = (DIM_PRM*)disk->prm;
	do {
		if ( track > DIM_MAX_TRACK ) break;
		if ( prm->track != track ) prm->sector = 0;
		prm->track = track;
		return TRUE;
	} while  ( 0 );
	return FALSE;
}

UINT32 DIM_Read(INFO_DISK* disk, const X68_SECTOR_ID* id,  UINT8* buf, BOOL is_diag)
{
	DIM_PRM* prm = (DIM_PRM*)disk->prm;
	SINT32 ofs;
	UINT32 ret = X68_SECTOR_STAT_NO_ADDR_MARK | X68_SECTOR_STAT_NO_DATA;
	do {
		UINT32 track = (id->c<<1) | id->h;
		if ( track > DIM_MAX_TRACK ) break;
		if ( prm->track != track ) break;
		ofs = GetSectorOfs(prm, id);
		if ( ofs < 0 ) break;
		memcpy(buf, prm->raw+ofs, DIM_SECT_SIZE[prm->type]);
		prm->sector = id->r % DIM_SECT_NUM[prm->type];  // next sector
		return X68_SECTOR_STAT_OK;
	} while  ( 0 );

	if ( is_diag ) {
		// DiagReadならエラー落ちしても現在のセクタを読む
		X68_SECTOR_ID curid;
		curid.c = prm->track >> 1;
		curid.h = prm->track & 1;
		curid.r = prm->sector + 1;
		curid.n = ( DIM_SECT_SIZE[prm->type] >> 10 ) + 2;  // 3 or 2
		ofs = GetSectorOfs(prm, &curid);
		if ( ofs >= 0 ) {
			memcpy(buf, prm->raw+ofs, DIM_SECT_SIZE[prm->type]);
			prm->sector = curid.r % DIM_SECT_NUM[prm->type];  // next sector
			return X68_SECTOR_STAT_OK;
		}
	}

	return ret;
}

BOOL DIM_ReadID(INFO_DISK* disk, X68_SECTOR_ID* retid, BOOL for_error)
{
	DIM_PRM* prm = (DIM_PRM*)disk->prm;
	retid->c = prm->track >> 1;
	retid->h = prm->track & 1;
	retid->r = prm->sector + 1;
	retid->n = ( DIM_SECT_SIZE[prm->type] >> 10 ) + 2;  // 3 or 2
	if ( !for_error ) {
		prm->sector = (prm->sector+1) % DIM_SECT_NUM[prm->type];  // next sector
	}
	return TRUE;
}

UINT32 DIM_Write(INFO_DISK* disk, const X68_SECTOR_ID* id, UINT8* buf, BOOL del)
{
	DIM_PRM* prm = (DIM_PRM*)disk->prm;
	UINT32 ret = X68_SECTOR_STAT_NO_ADDR_MARK;
	do {
		SINT32 ofs;
		UINT32 track = (id->c<<1) | id->h;
		if ( track > DIM_MAX_TRACK ) break;
		if ( prm->track != track ) break;
		ofs = GetSectorOfs(prm, id);
		if ( ofs < 0 ) break;
		memcpy(prm->raw+ofs, buf, DIM_SECT_SIZE[prm->type]);
		prm->sector = id->r % DIM_SECT_NUM[prm->type];  // next sector
		return X68_SECTOR_STAT_OK;
	} while  ( 0 );
	return ret;
}

BOOL DIM_WriteID(INFO_DISK* disk, UINT32 track, UINT8* buf, UINT32 num)
{
	DIM_PRM* prm = (DIM_PRM*)disk->prm;
	do {
		UINT32 i;
		UINT32 n = ( DIM_SECT_SIZE[prm->type] >> 10 ) + 2;  // 3 or 2;
		if ( track > DIM_MAX_TRACK ) break;
		// XXX DIFCのメディアタイプと一致したフォーマットしかできない いい手段はないものか
		if ( num!=DIM_SECT_NUM[prm->type] ) break;
		for (i=0; i<num; i++, buf+=4) {
			if ( (((buf[0]<<1)+buf[1])!=track) || (buf[2]<1) || (buf[2]>num) || (buf[3]!=n) ) return FALSE;
		}
		prm->enable[track] = 1;
		prm->track = track;
		return TRUE;
	} while  ( 0 );
	return FALSE;
}

BOOL DIM_RebuildImage(INFO_DISK* disk)
{
	DIM_PRM* prm = (DIM_PRM*)disk->prm;
	UINT8* new_image = NULL;
	do {
		UINT32 sz = DIM_HEADER_SIZE;
		UINT32 ofs = DIM_HEADER_SIZE;
		UINT32 i;

		// 再構築後のサイズを計算
		for (i=0; i<DIM_MAX_TRACK; i++) {
			if ( prm->enable[i] ) sz += prm->trk_sz;
		}

		// 新規イメージ用のメモリを確保
		new_image = _MALLOC(sz, "");
		if ( !new_image ) break;

		// ヘッダ部再構築
		memset(new_image, 0, sz);
		memcpy(new_image, disk->image, DIM_HEADER_SIZE);
		memcpy(new_image+1, prm->enable, 170);

		// トラックデータ本体をコピー
		for (i=0; i<DIM_MAX_TRACK; i++) {
			if ( prm->enable[i] ) {
				memcpy(new_image+ofs, prm->raw+(i*prm->trk_sz), prm->trk_sz);
				ofs += prm->trk_sz;
			}
		}

		// イメージを差し替える
		_MFREE(disk->image);
		disk->image = new_image;
		disk->size = sz;

		return TRUE;
	} while  ( 0 );

	_MFREE(new_image);
	return FALSE;
}
