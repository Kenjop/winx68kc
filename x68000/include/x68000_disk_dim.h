/* -----------------------------------------------------------------------------------
  "SHARP X68000" FloppyDisk (DIM format)
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

/*
	�抸�����Œ�� KnightArms�iXDF�ł̓Q�[���J�n�ł��Ȃ��j���N���ł�����x�̎���
	�� �܁[���N�����Ȃ��Ȃ��Ă�c�iFDC����������Ȃ��j

	DIM�iDIFC�j�w�b�_���
	+$00      �t�H�[�}�b�g�^�C�v�i$00:2HD, $01:2HS, $02:2HC, $03:2HDE, $09:2HQ, $11:N88�j
	+$01-$AA  �g���b�N���ێ��t���O�i$00:�g���b�N���Ȃ�, $01:����j
	+$AB-$B9  �w�b�_������i"DIFC HEADER  ",$00,$00�j
	+$BA-$BD  �t�@�C���쐬��
	+$BE-$C1  �t�@�C���쐬����
	+$C2-$FD  �R�����g������
	+$FE      �쐬�Ɏg�p���� DIFC.X �̃o�[�W�����i$19 �Ȃ�� Ver.1.19�j
	+$FF      �I�[�o�[�g���b�N�i$00:�Ȃ��A$01�`:�g���b�N���j

	�g���b�N���u�Ȃ��v�̃g���b�N�� MAM(Deleted?) �Ƃ��Ĉ����̂��������H�iKnightArms�j
*/

// DIM Disk Type
enum {
	DIM_2HD = 0,
	DIM_2HS,
	DIM_2HC,
	DIM_2HDE,
	DIM_2HQ = 9,
	DIM_TYPE_MAX = 10  // N88 �̓T�|�[�g���Ȃ�
};

typedef struct {
	UINT32  track;
	UINT32  sector;
	UINT32  type;
	UINT32  trk_sz;
	UINT8   enable[170];      // �e�g���b�N���L�����ǂ����i�j
	UINT8   raw[1024*9*170];  // �x�^�C���[�W�f�[�^�i�ő�l�Ŋm�ہj
} DIM_PRM;

#define DIM_HEADER_SIZE  0x100
#define DIM_MAX_TRACK    170

/*                                       2HD   2HS   2HC  2HDE   ---   ---   ---   ---   ---   2HQ */
static const UINT32 DIM_SECT_SIZE[] = { 1024, 1024,  512, 1024,    0,    0,    0,    0,    0,  512 };
static const UINT32 DIM_SECT_NUM [] = {    8,    9,   15,    9,    0,    0,    0,    0,    0,   18 };


// --------------------------------------------------------------------------
//   �����g�p
// --------------------------------------------------------------------------
static SINT32 GetSectorOfs(const DIM_PRM* prm, const X68_SECTOR_ID* id)
{
	UINT32 type = prm->type;
	UINT32 c = id->c, h = id->h, r = id->r, n = id->n;
	UINT32 ret = 0;

	do {
		UINT32 track;
		/*
			9SCDRV �ł́i���炭�t�H�[�}�b�g���ʂ̂��߂Ɂj2HS/2HDE �œ����ID���g���Ă���
			- 2HS  : �Z�N�^1�ȊO�AR+0x09�i��FCHRN=00/00/01/03�A00/00/0B/03�A00/00/0C/03 ... 00/00/12/03�j
			- 2HDE : �Z�N�^1�ȊO�AH+0x80�i��FCHRN=00/00/01/03�A00/80/02/03�A00/80/03/03 ... 00/80/09/03�j
		*/
		// 9SCDRV�p
		if ( type==DIM_2HS && r >= 0x0B ) r -= 0x09;
		if ( type==DIM_2HDE ) h &= 0x01;

		// ID�`�F�b�N
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
//   FDD�p�W���֐��Q
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

		// ���������m�ۂł��Ȃ��ꍇ�G���[
		if ( !prm ) break;
		// �f�B�X�N�T�C�Y�G���[
		if ( disk->size < DIM_HEADER_SIZE ) break;

		// �L���g���b�N�t���O
		memcpy(prm->enable, &disk->image[1], 170);
		// E5 �Ŗ��߂Ă���
		memset(prm->raw, 0xE5, sizeof(prm->raw));

		// �t�H�[�}�b�g�^�C�v�m�F
		type = disk->image[0x00];
		if ( type >= DIM_TYPE_MAX ) break;
		if ( DIM_SECT_SIZE[type] == 0 ) break;

		// �g���b�N�ʒu�̌v�Z
		ofs = DIM_HEADER_SIZE;
		trk_sz = DIM_SECT_SIZE[type] * DIM_SECT_NUM [type];
		for (track=0; track<DIM_MAX_TRACK; track++) {
			// �g���b�N��񂪂���ꍇ�̂� ofs ���i�s�A���݂��Ȃ��g���b�N���͑O�ɋl�߂���
			if ( disk->image[0x01+track] ) {
				memcpy(prm->raw+(track*trk_sz), disk->image+ofs, trk_sz);
				ofs += trk_sz;
			}
		}
		// �ŏI�ʒu���T�C�Y�𒴂��Ă���G���[
		if ( ofs > disk->size ) {
			LOG(("### DIM : disk size error (required=%d, size=%d)", ofs, disk->size));
			break;
		}

		// �����܂ŗ����琬��
		prm->track = 0;
		prm->sector = 0;
		prm->type = type;
		prm->trk_sz = trk_sz;
		disk->prm = (void*)prm;

		return TRUE;
	} while ( 0 );

	// �G���[���������ꍇ
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
		// DiagRead�Ȃ�G���[�������Ă����݂̃Z�N�^��ǂ�
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
		// XXX DIFC�̃��f�B�A�^�C�v�ƈ�v�����t�H�[�}�b�g�����ł��Ȃ� ������i�͂Ȃ����̂�
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

		// �č\�z��̃T�C�Y���v�Z
		for (i=0; i<DIM_MAX_TRACK; i++) {
			if ( prm->enable[i] ) sz += prm->trk_sz;
		}

		// �V�K�C���[�W�p�̃��������m��
		new_image = _MALLOC(sz, "");
		if ( !new_image ) break;

		// �w�b�_���č\�z
		memset(new_image, 0, sz);
		memcpy(new_image, disk->image, DIM_HEADER_SIZE);
		memcpy(new_image+1, prm->enable, 170);

		// �g���b�N�f�[�^�{�̂��R�s�[
		for (i=0; i<DIM_MAX_TRACK; i++) {
			if ( prm->enable[i] ) {
				memcpy(new_image+ofs, prm->raw+(i*prm->trk_sz), prm->trk_sz);
				ofs += prm->trk_sz;
			}
		}

		// �C���[�W�������ւ���
		_MFREE(disk->image);
		disk->image = new_image;
		disk->size = sz;

		return TRUE;
	} while  ( 0 );

	_MFREE(new_image);
	return FALSE;
}
