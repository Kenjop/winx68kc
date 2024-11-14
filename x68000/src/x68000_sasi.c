/* -----------------------------------------------------------------------------------
  "SHARP X68000" SASI (Shugart Associates System Interface) HDD
                                                      (c) 2000-24 Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "x68000_sasi.h"

// 右の数字はけろぴー実装でのPHASE番号
enum {
	SASI_PHASE_BUS_FREE = 0, // 0
	SASI_PHASE_SELECTION,    // 1
	SASI_PHASE_COMMAND,      // 2
	SASI_PHASE_DATA_READ,    // 3
	SASI_PHASE_DATA_WRITE,   // 3
	SASI_PHASE_STATUS,       // 4
	SASI_PHASE_MESSAGE,      // 5
	SASI_PHASE_SENSE_STAT,   // 9
	SASI_PHASE_SPECIFY,      // 10
};

enum {
	SASI_STATUS_REQUEST = 0x01,
	SASI_STATUS_BUSY    = 0x02,
	SASI_STATUS_IO_READ = 0x04,
	SASI_STATUS_COMMAND = 0x08,
	SASI_STATUS_MESSAGE = 0x10,
};


// --------------------------------------------------------------------------
//   SASI構造体
// --------------------------------------------------------------------------
typedef struct {
	X68IOC      ioc;
	SASIFUNCCB  cb;
	void*       cbprm;
	UINT32      phase;
	UINT32      sel_dev;
	UINT32      sel_unit;
	UINT32      sector;
	UINT32      blocks;
	UINT32      err_code;
	UINT32      data_pos;
	UINT8       cmd[8];
	UINT8       result[4];
	UINT8       data_buf[256];
	X68FDD_LED_STATE led;
} INFO_SASI;


#define SASI_IS_READY(dev,sector)   sasi->cb(sasi->cbprm, SASIFUNC_IS_READY, dev, (sector)<<8, NULL, 0)
#define SASI_READ(dev,sector)       sasi->cb(sasi->cbprm, SASIFUNC_READ,     dev, (sector)<<8, sasi->data_buf, 256)
#define SASI_WRITE(dev,sector)      sasi->cb(sasi->cbprm, SASIFUNC_WRITE,    dev, (sector)<<8, sasi->data_buf, 256)

#define SASI_DEV_IDX             ( ( sasi->sel_dev << 1 ) | sasi->sel_unit )


// --------------------------------------------------------------------------
//   内部関数
// --------------------------------------------------------------------------
static void SasiExecCmd(INFO_SASI* sasi)
{
	sasi->sel_unit = ( sasi->cmd[1] >> 5 ) & 1;  // X68000の論理ユニット番号は0か1のみ

//LOG(("SASI cmd=$%02X", sasi->cmd[0]));
	switch ( sasi->cmd[0] )
	{
	case 0x00:  // Test Drive Ready
		if ( SASI_IS_READY(SASI_DEV_IDX, 0) ) {
			sasi->err_code = 0x00;
		} else {
			sasi->err_code = 0x7F;
		}
		sasi->phase = SASI_PHASE_STATUS;
		break;

	case 0x01:  // Recalibrate
		if ( SASI_IS_READY(SASI_DEV_IDX, 0) ) {
			sasi->sector = 0;
			sasi->err_code = 0x00;
		} else {
			sasi->err_code = 0x7F;
		}
		sasi->phase = SASI_PHASE_STATUS;
		break;

	case 0x03:  // Request Sense Status
		sasi->result[0] = (UINT8)( sasi->err_code );
		sasi->result[1] = (UINT8)( ( ( sasi->sector >> 16 ) &0x1F ) | ( sasi->sel_unit << 5 ) );
		sasi->result[2] = (UINT8)( ( ( sasi->sector >>  8 ) &0xFF ) );
		sasi->result[3] = (UINT8)( ( ( sasi->sector >>  0 ) &0xFF ) );
		sasi->data_pos = 0;
		sasi->err_code = 0x00;
		sasi->phase = SASI_PHASE_SENSE_STAT;
		break;

	case 0x04:  // Format Drive
		sasi->err_code = 0x00;
		sasi->phase = SASI_PHASE_STATUS;
		break;

	case 0x08:  // Read Data
		sasi->sector  = ( ((UINT32)sasi->cmd[1]) & 0x1F ) << 16;
		sasi->sector |=   ((UINT32)sasi->cmd[2])          <<  8;
		sasi->sector |=   ((UINT32)sasi->cmd[3])          <<  0;
		sasi->blocks  =   ((UINT32)sasi->cmd[4]);
		if ( SASI_READ(SASI_DEV_IDX, sasi->sector) ) {
			sasi->err_code = 0x00;
		} else {
			sasi->err_code = 0x0F;
		}
		sasi->data_pos = 0;
		sasi->phase = SASI_PHASE_DATA_READ;
		break;

	case 0x0A:  // Write Data
		sasi->sector  = ( ((UINT32)sasi->cmd[1]) & 0x1F ) << 16;
		sasi->sector |=   ((UINT32)sasi->cmd[2])          <<  8;
		sasi->sector |=   ((UINT32)sasi->cmd[3])          <<  0;
		sasi->blocks  =   ((UINT32)sasi->cmd[4]);
		memset(sasi->data_buf, 0, sizeof(sasi->data_buf));
		if ( SASI_IS_READY(SASI_DEV_IDX, sasi->sector) ) {
			sasi->err_code = 0x00;
		} else {
			sasi->err_code = 0x0F;
		}
		sasi->data_pos = 0;
		sasi->phase = SASI_PHASE_DATA_WRITE;
		break;

	case 0x0B:  // Seek
		if ( SASI_IS_READY(SASI_DEV_IDX, 0) ) {
			sasi->err_code = 0x00;
		} else {
			sasi->err_code = 0x7F;
		}
		sasi->phase = SASI_PHASE_STATUS;
		break;

	case 0xC2:  // Specify
		if ( SASI_IS_READY(SASI_DEV_IDX, 0) ) {
			sasi->err_code = 0x00;
		} else {
			sasi->err_code = 0x7F;
		}
		sasi->data_pos = 0;
		sasi->phase = SASI_PHASE_SPECIFY;
		break;

	default:
		sasi->phase = SASI_PHASE_STATUS;
		break;
	}
}

static BOOL CALLBACK SasiDummyCb(void* prm, SASI_FUNCTIONS func, UINT32 devid, UINT32 pos, UINT8* data, UINT32 size)
{
	return FALSE;
}


// --------------------------------------------------------------------------
//   公開関数
// --------------------------------------------------------------------------
X68SASI X68SASI_Init(X68IOC ioc)
{
	INFO_SASI* sasi = NULL;
	do {
		sasi = (INFO_SASI*)_MALLOC(sizeof(INFO_SASI), "SASI struct");
		if ( !sasi ) break;
		memset(sasi, 0, sizeof(INFO_SASI));
		sasi->ioc = ioc;
		X68SASI_Reset((X68SASI)sasi);
		LOG(("SASI : initialize OK"));
		return (X68SASI)sasi;
	} while ( 0 );

	X68SASI_Cleanup((X68SASI)sasi);
	return NULL;
}

void X68SASI_Cleanup(X68SASI hdl)
{
	INFO_SASI* sasi = (INFO_SASI*)hdl;
	if ( sasi ) {
		_MFREE(sasi);
	}
}

void X68SASI_Reset(X68SASI hdl)
{
	INFO_SASI* sasi = (INFO_SASI*)hdl;
	if ( sasi ) {
		sasi->phase = SASI_PHASE_BUS_FREE;
		sasi->sel_dev = 0;
		sasi->sel_unit = 0;
		sasi->sector = 0;
		sasi->blocks = 0;
		sasi->err_code = 0;
		sasi->data_pos = 0;
	}
}

MEM16W_HANDLER(X68SASI_Write)
{
	INFO_SASI* sasi = (INFO_SASI*)prm;

	if ( !( adr & 1 ) ) return;
	adr &= 0x07;

	switch ( adr )
	{
	default:
	case 1:  // $E96001 : DATA
		if ( sasi->phase == SASI_PHASE_COMMAND ) {
			// コマンド受付中
			sasi->cmd[sasi->data_pos++] = data;
			if ( sasi->data_pos >= 6 ) {
				sasi->err_code = 0x00;
				SasiExecCmd(sasi);
			}
		} else if ( sasi->phase == SASI_PHASE_DATA_WRITE ) {
			// データ書き込み中
			sasi->data_buf[sasi->data_pos++] = (UINT8)data;
			if ( sasi->data_pos >= 256 ) {
				// 1セクタ終了
				if ( SASI_WRITE(SASI_DEV_IDX, sasi->sector) ) {
					sasi->blocks--;
					if ( sasi->blocks > 0 ) {
						sasi->sector++;
						sasi->data_pos = 0;
						if ( !SASI_READ(SASI_DEV_IDX, sasi->sector) ) {
							// ドライブ末尾
							sasi->err_code = 0x0F;
							sasi->phase = SASI_PHASE_STATUS;
						}
					} else {
						// 残セクタなし
						sasi->phase = SASI_PHASE_STATUS;
					}
				} else {
					// 書き込み失敗
					sasi->err_code = 0x0F;
					sasi->phase = SASI_PHASE_STATUS;
				}
			}
		} else if ( sasi->phase == SASI_PHASE_SPECIFY ) {
			// SPECIFYデータ書き込み中
			sasi->data_pos++;
			if ( sasi->data_pos >= 10 ) {
				sasi->phase = SASI_PHASE_STATUS;
			}
		}
		// ステータスフェーズに落ちてたら割り込み
		if ( sasi->phase == SASI_PHASE_STATUS ) {
			X68IOC_SetIrq(sasi->ioc, IOCIRQ_HDD, TRUE);
		}
		break;

	case 3:  // $E96003 : DATA (SEL=0)
		if ( sasi->phase == SASI_PHASE_SELECTION ) {
			sasi->phase = SASI_PHASE_COMMAND;
		}
		break;

	case 5:  // $E96005 : RESET
		X68SASI_Reset((X68SASI)prm);
		break;

	case 7:  // $E96007 : DATA (SEL=1)
		if ( sasi->phase == SASI_PHASE_BUS_FREE ) {
			sasi->sel_dev = 0xFF;
			if ( data ) {
				UINT32 bits = data;
				UINT32 i;
				for (i=0; i<8; i++) {
					if ( bits & 1 ) {
						sasi->sel_dev = i;
						break;
					}
					bits >>= 1;
				}
			}
			sasi->data_pos = 0;
			sasi->phase = SASI_PHASE_SELECTION;
		}
		break;
	}

	// HDD LED処理（バスフリーでなければBUSY）
	sasi->led = ( sasi->phase != SASI_PHASE_BUS_FREE ) ? X68FDD_LED_RED : X68FDD_LED_GREEN;
}

MEM16R_HANDLER(X68SASI_Read)
{
	INFO_SASI* sasi = (INFO_SASI*)prm;
	UINT32 ret = 0xFF;

	if ( !( adr & 1 ) ) return 0xFF;
	adr &= 0x07;

	switch ( adr )
	{
	default:
	case 1:  // $E96001 : DATA
		if ( sasi->phase == SASI_PHASE_DATA_READ  ) {
			// データリード中
			ret = sasi->data_buf[sasi->data_pos++];
			if ( sasi->data_pos >= 256 ) {
				// 1セクタ終了
				sasi->blocks--;
				if ( sasi->blocks > 0 ) {
					sasi->sector++;
					sasi->data_pos = 0;
					if ( !SASI_READ(SASI_DEV_IDX, sasi->sector) ) {
						// リードエラー
						sasi->err_code = 0x0F;
						sasi->phase = SASI_PHASE_STATUS;
					}
				} else {
					// 残セクタなし
					sasi->phase = SASI_PHASE_STATUS;
				}
			}
		} else if ( sasi->phase == SASI_PHASE_STATUS  ) {
			// ステータスフェーズ
			ret = ( sasi->err_code ) ? 0x02 : 0x00;
			sasi->phase = SASI_PHASE_MESSAGE;
		} else if ( sasi->phase == SASI_PHASE_MESSAGE  ) {
			// メッセージフェーズ
			ret = 0x00;
			sasi->phase = SASI_PHASE_BUS_FREE;
		} else if ( sasi->phase == SASI_PHASE_SENSE_STAT  ) {
			// SENSE STATUS
			ret = sasi->result[sasi->data_pos++];
			if ( sasi->data_pos >= 4 ) {
				sasi->phase = SASI_PHASE_STATUS;
			}
		}
		// ステータスフェーズに落ちてたら割り込み
		if ( sasi->phase == SASI_PHASE_STATUS ) {
			X68IOC_SetIrq(sasi->ioc, IOCIRQ_HDD, TRUE);
		}
		break;

	case 3:  // $E96003 : STATUS
		ret = 0;
		if ( sasi->phase != SASI_PHASE_BUS_FREE   ) ret |= SASI_STATUS_BUSY;
		if ( sasi->phase >= SASI_PHASE_COMMAND    ) ret |= SASI_STATUS_REQUEST;
		if ( sasi->phase == SASI_PHASE_COMMAND    ) ret |= SASI_STATUS_COMMAND;
		if ( sasi->phase == SASI_PHASE_DATA_READ  ) ret |= SASI_STATUS_IO_READ;
		if ( sasi->phase == SASI_PHASE_SENSE_STAT ) ret |= SASI_STATUS_IO_READ;
		if ( sasi->phase == SASI_PHASE_STATUS     ) ret |= SASI_STATUS_COMMAND | SASI_STATUS_IO_READ;
		if ( sasi->phase == SASI_PHASE_MESSAGE    ) ret |= SASI_STATUS_COMMAND | SASI_STATUS_IO_READ | SASI_STATUS_MESSAGE;
		break;

	case 5:  // $E96005 : W/O
	case 7:  // $E96007 : W/O
		break;
	}

	// HDD LED処理（バスフリーでなければBUSY）
	sasi->led = ( sasi->phase != SASI_PHASE_BUS_FREE ) ? X68FDD_LED_RED : X68FDD_LED_GREEN;

	return ret;
}

int X68SASI_IsDataReady(X68SASI hdl)
{
	INFO_SASI* sasi = (INFO_SASI*)hdl;
	if ( sasi->phase == SASI_PHASE_DATA_READ ||
	     sasi->phase == SASI_PHASE_DATA_WRITE ||
		 sasi->phase == SASI_PHASE_SENSE_STAT ) return 1; 
	return 0;
}

void X68SASI_SetCallback(X68SASI hdl, SASIFUNCCB func, void* cbprm)
{
	INFO_SASI* sasi = (INFO_SASI*)hdl;
	if ( sasi ) {
		sasi->cb = ( func ) ? func : &SasiDummyCb;
		sasi->cbprm = cbprm;
	}
}

X68FDD_LED_STATE X68SASI_GetLedState(X68SASI hdl)
{
	INFO_SASI* sasi = (INFO_SASI*)hdl;
	if ( sasi ) {
		return sasi->led;
	}
	return X68FDD_LED_OFF;
}

void X68SASI_LoadState(X68SASI hdl, STATE* state, UINT32 id)
{
	INFO_SASI* sasi = (INFO_SASI*)hdl;
	if ( sasi && state ) {
		// XXX 項目数多いので手抜き
		size_t sz = ((size_t)&(((INFO_SASI*)0)->phase));  // offsetof(INFO_SASI,phase)
		ReadState(state, id, MAKESTATEID('I','N','F','O'), &sasi->phase, sizeof(INFO_SASI)-(UINT32)sz);
	}
}

void X68SASI_SaveState(X68SASI hdl, STATE* state, UINT32 id)
{
	INFO_SASI* sasi = (INFO_SASI*)hdl;
	if ( sasi && state ) {
		size_t sz = ((size_t)&(((INFO_SASI*)0)->phase));  // offsetof(INFO_SASI,phase)
		WriteState(state, id, MAKESTATEID('I','N','F','O'), &sasi->phase, sizeof(INFO_SASI)-(UINT32)sz);
	}
}
