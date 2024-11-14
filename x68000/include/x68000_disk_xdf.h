/* -----------------------------------------------------------------------------------
  "SHARP X68000" FloppyDisk (XDF format)
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

typedef struct {
	UINT32  track;
	UINT32  sector;
} XDF_PRM;

#define XDF_FILE_SIZE    1261568  // XDFはファイルサイズ固定
#define XDF_MAX_TRACK    153
#define XDF_SECTOR_SIZE  1024

UINT32 XDF_GetPrmSize(void)
{
	return sizeof(XDF_PRM);
}

BOOL XDF_SetDisk(INFO_DISK* disk)
{
	XDF_PRM* prm = _MALLOC(sizeof(XDF_PRM), "");
	do {
		// メモリが確保できない場合エラー
		if ( !prm ) break;
		// prmが残っている場合エラー
		if ( disk->prm ) break;
		// ディスクサイズエラー
		if ( disk->size!=XDF_FILE_SIZE ) break;

		// ここまで来たら成功
		prm->track = 0;
		prm->sector = 0;
		disk->prm = (void*)prm;

		return TRUE;
	} while ( 0 );

	// エラー落ちした場合
	if ( prm ) _MFREE(prm);
	disk->prm = NULL;
	return FALSE;
}

BOOL XDF_EjectDisk(INFO_DISK* disk)
{
	XDF_PRM* prm = (XDF_PRM*)disk->prm;
	if ( prm ) _MFREE(prm);
	disk->prm = NULL;
	return FALSE;
}

BOOL XDF_Seek(INFO_DISK* disk, UINT32 track)
{
	XDF_PRM* prm = (XDF_PRM*)disk->prm;
	do {
		if ( track > XDF_MAX_TRACK ) break;
		if ( prm->track != track ) prm->sector = 0;
		prm->track = track;
		return TRUE;
	} while  ( 0 );
	return FALSE;
}

UINT32 XDF_Read(INFO_DISK* disk, const X68_SECTOR_ID* id,  UINT8* buf, BOOL is_diag)
{
	XDF_PRM* prm = (XDF_PRM*)disk->prm;
	UINT32 ofs;
	UINT32 ret = X68_SECTOR_STAT_NO_ADDR_MARK | X68_SECTOR_STAT_NO_DATA;
	do {
		UINT32 track = (id->c<<1) | id->h;
		if ( track > XDF_MAX_TRACK ) break;
		if ( prm->track != track ) break;
		if ( (id->r<1) || (id->r>8) ) break;
		if ( id->h > 1 ) break;
		if ( id->n != 3 ) break;
		ofs = ( (track<<3) + (id->r-1) ) << 10;
		memcpy(buf, disk->image+ofs, XDF_SECTOR_SIZE);
		prm->sector = id->r & 7;  // next sector
		return X68_SECTOR_STAT_OK;
	} while  ( 0 );

	if ( is_diag ) {
		// DiagReadならエラー落ちしても現在のセクタを読む
		ofs = ( (prm->track<<3) + (prm->sector) ) << 10;
		memcpy(buf, disk->image+ofs, XDF_SECTOR_SIZE);
		prm->sector = (prm->sector+1) & 7;  // next sector
		return X68_SECTOR_STAT_OK;
	}

	return ret;
}

BOOL XDF_ReadID(INFO_DISK* disk, X68_SECTOR_ID* retid, BOOL for_error)
{
	XDF_PRM* prm = (XDF_PRM*)disk->prm;
	retid->c = prm->track >> 1;
	retid->h = prm->track & 1;
	retid->r = prm->sector + 1;
	retid->n = 3;
	if ( !for_error ) {
		prm->sector = (prm->sector+1) & 7;  // next sector
	}
	return TRUE;
}

UINT32 XDF_Write(INFO_DISK* disk, const X68_SECTOR_ID* id, UINT8* buf, BOOL del)
{
	XDF_PRM* prm = (XDF_PRM*)disk->prm;
	UINT32 ret = X68_SECTOR_STAT_NO_ADDR_MARK;
	do {
		UINT32 ofs;
		UINT32 track = (id->c<<1) | id->h;
		if ( track > XDF_MAX_TRACK ) break;
		if ( prm->track != track ) break;
		if ( (id->r<1) || (id->r>8) ) break;
		if ( id->h > 1 ) break;
		if ( id->n != 3 ) break;
		ofs = ( (track<<3) + (id->r-1) ) << 10;
		memcpy(disk->image+ofs, buf, XDF_SECTOR_SIZE);
		prm->sector = id->r & 7;  // next sector
		return X68_SECTOR_STAT_OK;
	} while  ( 0 );
	return ret;
}

BOOL XDF_WriteID(INFO_DISK* disk, UINT32 track, UINT8* buf, UINT32 num)
{
	XDF_PRM* prm = (XDF_PRM*)disk->prm;
	do {
		UINT32 i;
		if ( track > XDF_MAX_TRACK ) break;
		if ( num!=8 ) break;
		for (i=0; i<8; i++, buf+=4) {
			if ( (((buf[0]<<1)+buf[1])!=track) || (buf[2]<1) || (buf[2]>8) || (buf[3]!=3) ) return FALSE;
		}
		prm->track = track;
		return TRUE;
	} while  ( 0 );
	return FALSE;
}

BOOL XDF_RebuildImage(INFO_DISK* disk)
{
	// XDF（他ベタイメージ）ではファイルサイズ固定なので、再構築の必要はない
	return TRUE;
}
