#ifndef SD_H
#define SD_H

#include "License.h"
#include "SD_Misc.h"

/* SD commands                           type  argument     response */
  /* class 0 */
/* This is basically the same command as for MMC with some quirks. */
#define SD_SEND_RELATIVE_ADDR     3   /* bcr                     R6  */
#define SD_SEND_IF_COND           8   /* bcr  [11:0] See below   R7  */

  /* class 10 */
#define SD_SWITCH                 6   /* adtc [31:0] See below   R1  */

  /* Application commands */
#define SD_APP_SET_BUS_WIDTH      6   /* ac   [1:0] bus width    R1  */
#define SD_APP_SEND_NUM_WR_BLKS  22   /* adtc                    R1  */
#define SD_APP_SET_WR_BLK_ERASE_COUNT 23	/*		 R1 */
#define SD_APP_OP_COND           41   /* bcr  [31:0] OCR         R3  */
#define SD_APP_SEND_SCR          51   /* adtc                    R1  */

/* Standard MMC commands (4.1)           type  argument     response */
   /* class 1 */
#define SD_GO_IDLE_STATE         0   /* bc                          */
#define SD_SEND_OP_COND          1   /* bcr  [31:0] OCR         R3  */
#define SD_ALL_SEND_CID          2   /* bcr                     R2  */
#define SD_SET_RELATIVE_ADDR     3   /* ac   [31:16] RCA        R1  */
#define SD_SET_DSR               4   /* bc   [31:16] RCA            */
#define SD_SWITCH                6   /* ac   [31:0] See below   R1b */
#define SD_SELECT_CARD           7   /* ac   [31:16] RCA        R1  */
#define SD_SEND_EXT_CSD          8   /* adtc                    R1  */
#define SD_SEND_CSD              9   /* ac   [31:16] RCA        R2  */
#define SD_SEND_CID             10   /* ac   [31:16] RCA        R2  */
#define SD_READ_DAT_UNTIL_STOP  11   /* adtc [31:0] dadr        R1  */
#define SD_STOP_TRANSMISSION    12   /* ac                      R1b */
#define SD_SEND_STATUS          13   /* ac   [31:16] RCA        R1  */
#define SD_GO_INACTIVE_STATE    15   /* ac   [31:16] RCA            */
#define SD_SPI_READ_OCR         58   /* spi                  spi_R3 */
#define SD_SPI_CRC_ON_OFF       59   /* spi  [0:0] flag      spi_R1 */

  /* class 2 */
#define SD_SET_BLOCKLEN         16   /* ac   [31:0] block len   R1  */
#define SD_READ_SINGLE_BLOCK    17   /* adtc [31:0] data addr   R1  */
#define SD_READ_MULTIPLE_BLOCK  18   /* adtc [31:0] data addr   R1  */

  /* class 3 */
#define SD_WRITE_DAT_UNTIL_STOP 20   /* adtc [31:0] data addr   R1  */

  /* class 4 */
#define SD_SET_BLOCK_COUNT      23   /* adtc [31:0] data addr   R1  */
#define SD_WRITE_BLOCK          24   /* adtc [31:0] data addr   R1  */
#define SD_WRITE_MULTIPLE_BLOCK 25   /* adtc                    R1  */
#define SD_PROGRAM_CID          26   /* adtc                    R1  */
#define SD_PROGRAM_CSD          27   /* adtc                    R1  */

  /* class 6 */
#define SD_SET_WRITE_PROT       28   /* ac   [31:0] data addr   R1b */
#define SD_CLR_WRITE_PROT       29   /* ac   [31:0] data addr   R1b */
#define SD_SEND_WRITE_PROT      30   /* adtc [31:0] wpdata addr R1  */

  /* class 5 */
#define SD_ERASE_GROUP_START    35   /* ac   [31:0] data addr   R1  */
#define SD_ERASE_GROUP_END      36   /* ac   [31:0] data addr   R1  */
#define SD_ERASE                38   /* ac                      R1b */

  /* class 9 */
#define SD_FAST_IO              39   /* ac   <Complex>          R4  */
#define SD_GO_IRQ_STATE         40   /* bcr                     R5  */

  /* class 7 */
#define SD_LOCK_UNLOCK          42   /* adtc                    R1b */

  /* class 8 */
#define SD_APP_CMD              55   /* ac   [31:16] RCA        R1  */
#define SD_GEN_CMD              56   /* adtc [0] RD/WR          R1  */

/*
 * MMC_SWITCH argument format:
 *
 *	[31:26] Always 0
 *	[25:24] Access Mode
 *	[23:16] Location of target Byte in EXT_CSD
 *	[15:08] Value Byte
 *	[07:03] Always 0
 *	[02:00] Command Set
 */

/*
  MMC status in R1, for native mode (SPI bits are different)
  Type
	e : error bit
	s : status bit
	r : detected and set for the actual command response
	x : detected and set during command execution. the host must poll
            the card by sending status command in order to read these bits.
  Clear condition
	a : according to the card state
	b : always related to the previous command. Reception of
            a valid command will clear it (with a delay of one command)
	c : clear by read
 */

#define R1_OUT_OF_RANGE		(1 << 31)	/* er, c */
#define R1_ADDRESS_ERROR	(1 << 30)	/* erx, c */
#define R1_BLOCK_LEN_ERROR	(1 << 29)	/* er, c */
#define R1_ERASE_SEQ_ERROR      (1 << 28)	/* er, c */
#define R1_ERASE_PARAM		(1 << 27)	/* ex, c */
#define R1_WP_VIOLATION		(1 << 26)	/* erx, c */
#define R1_CARD_IS_LOCKED	(1 << 25)	/* sx, a */
#define R1_LOCK_UNLOCK_FAILED	(1 << 24)	/* erx, c */
#define R1_COM_CRC_ERROR	(1 << 23)	/* er, b */
#define R1_ILLEGAL_COMMAND	(1 << 22)	/* er, b */
#define R1_CARD_ECC_FAILED	(1 << 21)	/* ex, c */
#define R1_CC_ERROR		(1 << 20)	/* erx, c */
#define R1_ERROR		(1 << 19)	/* erx, c */
#define R1_UNDERRUN		(1 << 18)	/* ex, c */
#define R1_OVERRUN		(1 << 17)	/* ex, c */
#define R1_CID_CSD_OVERWRITE	(1 << 16)	/* erx, c, CID/CSD overwrite */
#define R1_WP_ERASE_SKIP	(1 << 15)	/* sx, c */
#define R1_CARD_ECC_DISABLED	(1 << 14)	/* sx, a */
#define R1_ERASE_RESET		(1 << 13)	/* sr, c */
#define R1_STATUS(x)            (x & 0xFFFFE000)
#define R1_CURRENT_STATE(x)	((x & 0x00001E00) >> 9)	/* sx, b (4 bits) */
#define R1_READY_FOR_DATA	(1 << 8)	/* sx, a */
#define R1_APP_CMD		(1 << 5)	/* sr, c */

/*
 * MMC/SD in SPI mode reports R1 status always, and R2 for SEND_STATUS
 * R1 is the low order byte; R2 is the next highest byte, when present.
 */
#define R1_SPI_IDLE		(1 << 0)
#define R1_SPI_ERASE_RESET	(1 << 1)
#define R1_SPI_ILLEGAL_COMMAND	(1 << 2)
#define R1_SPI_COM_CRC		(1 << 3)
#define R1_SPI_ERASE_SEQ	(1 << 4)
#define R1_SPI_ADDRESS		(1 << 5)
#define R1_SPI_PARAMETER	(1 << 6)
/* R1 bit 7 is always zero */
#define R2_SPI_CARD_LOCKED	(1 << 8)
#define R2_SPI_WP_ERASE_SKIP	(1 << 9)	/* or lock/unlock fail */
#define R2_SPI_LOCK_UNLOCK_FAIL	R2_SPI_WP_ERASE_SKIP
#define R2_SPI_ERROR		(1 << 10)
#define R2_SPI_CC_ERROR		(1 << 11)
#define R2_SPI_CARD_ECC_ERROR	(1 << 12)
#define R2_SPI_WP_VIOLATION	(1 << 13)
#define R2_SPI_ERASE_PARAM	(1 << 14)
#define R2_SPI_OUT_OF_RANGE	(1 << 15)	/* or CSD overwrite */
#define R2_SPI_CSD_OVERWRITE	R2_SPI_OUT_OF_RANGE

/* These are unpacked versions of the actual responses */

struct _mmc_csd {
	UInt8  csd_structure;
	UInt8  spec_vers;
	UInt8  taac;
	UInt8  nsac;
	UInt8  tran_speed;
	UInt16 ccc;
	UInt8  read_bl_len;
	UInt8  read_bl_partial;
	UInt8  write_blk_misalign;
	UInt8  read_blk_misalign;
	UInt8  dsr_imp;
	UInt16 c_size;
	UInt8  vdd_r_curr_min;
	UInt8  vdd_r_curr_max;
	UInt8  vdd_w_curr_min;
	UInt8  vdd_w_curr_max;
	UInt8  c_size_mult;
	union {
		struct { /* MMC system specification version 3.1 */
			UInt8  erase_grp_size;
			UInt8  erase_grp_mult;
		} v31;
		struct { /* MMC system specification version 2.2 */
			UInt8  sector_size;
			UInt8  erase_grp_size;
		} v22;
	} erase;
	UInt8  wp_grp_size;
	UInt8  wp_grp_enable;
	UInt8  default_ecc;
	UInt8  r2w_factor;
	UInt8  write_bl_len;
	UInt8  write_bl_partial;
	UInt8  file_format_grp;
	UInt8  copy;
	UInt8  perm_write_protect;
	UInt8  tmp_write_protect;
	UInt8  file_format;
	UInt8  ecc;
};

/*
 * OCR bits are mostly in host.h
 */
#define MMC_CARD_BUSY	0x80000000	/* Card Power up status bit */

/*
 * Card Command Classes (CCC)
 */
#define CCC_BASIC		(1<<0)	/* (0) Basic protocol functions */
					/* (CMD0,1,2,3,4,7,9,10,12,13,15) */
					/* (and for SPI, CMD58,59) */
#define CCC_STREAM_READ		(1<<1)	/* (1) Stream read commands */
					/* (CMD11) */
#define CCC_BLOCK_READ		(1<<2)	/* (2) Block read commands */
					/* (CMD16,17,18) */
#define CCC_STREAM_WRITE	(1<<3)	/* (3) Stream write commands */
					/* (CMD20) */
#define CCC_BLOCK_WRITE		(1<<4)	/* (4) Block write commands */
					/* (CMD16,24,25,26,27) */
#define CCC_ERASE		(1<<5)	/* (5) Ability to erase blocks */
					/* (CMD32,33,34,35,36,37,38,39) */
#define CCC_WRITE_PROT		(1<<6)	/* (6) Able to write protect blocks */
					/* (CMD28,29,30) */
#define CCC_LOCK_CARD		(1<<7)	/* (7) Able to lock down card */
					/* (CMD16,CMD42) */
#define CCC_APP_SPEC		(1<<8)	/* (8) Application specific */
					/* (CMD55,56,57,ACMD*) */
#define CCC_IO_MODE		(1<<9)	/* (9) I/O mode */
					/* (CMD5,39,40,52,53) */
#define CCC_SWITCH		(1<<10)	/* (10) High speed switch */
					/* (CMD6,34,35,36,37,50) */
					/* (11) Reserved */
					/* (CMD?) */

/*
 * CSD field definitions
 */

#define CSD_STRUCT_VER_1_0  0           /* Valid for system specification 1.0 - 1.2 */
#define CSD_STRUCT_VER_1_1  1           /* Valid for system specification 1.4 - 2.2 */
#define CSD_STRUCT_VER_1_2  2           /* Valid for system specification 3.1 - 3.2 - 3.31 - 4.0 - 4.1 */
#define CSD_STRUCT_EXT_CSD  3           /* Version is coded in CSD_STRUCTURE in EXT_CSD */

#define CSD_SPEC_VER_0      0           /* Implements system specification 1.0 - 1.2 */
#define CSD_SPEC_VER_1      1           /* Implements system specification 1.4 */
#define CSD_SPEC_VER_2      2           /* Implements system specification 2.0 - 2.2 */
#define CSD_SPEC_VER_3      3           /* Implements system specification 3.1 - 3.2 - 3.31 */
#define CSD_SPEC_VER_4      4           /* Implements system specification 4.0 - 4.1 */

/*
 * EXT_CSD fields
 */

#define EXT_CSD_BUS_WIDTH	183	/* R/W */
#define EXT_CSD_HS_TIMING	185	/* R/W */
#define EXT_CSD_CARD_TYPE	196	/* RO */
#define EXT_CSD_REV		192	/* RO */
#define EXT_CSD_SEC_CNT		212	/* RO, 4 bytes */

/*
 * EXT_CSD field definitions
 */

#define EXT_CSD_CMD_SET_NORMAL		(1<<0)
#define EXT_CSD_CMD_SET_SECURE		(1<<1)
#define EXT_CSD_CMD_SET_CPSECURE	(1<<2)

#define EXT_CSD_CARD_TYPE_26	(1<<0)	/* Card can run at 26MHz */
#define EXT_CSD_CARD_TYPE_52	(1<<1)	/* Card can run at 52MHz */

#define EXT_CSD_BUS_WIDTH_1	0	/* Card is in 1 bit mode */
#define EXT_CSD_BUS_WIDTH_4	1	/* Card is in 4 bit mode */
#define EXT_CSD_BUS_WIDTH_8	2	/* Card is in 8 bit mode */

/*
 * MMC_SWITCH access modes
 */

#define MMC_SWITCH_MODE_CMD_SET		0x00	/* Change the command set */
#define MMC_SWITCH_MODE_SET_BITS	0x01	/* Set bits which are 1 in value */
#define MMC_SWITCH_MODE_CLEAR_BITS	0x02	/* Clear bits which are 1 in value */
#define MMC_SWITCH_MODE_WRITE_BYTE	0x03	/* Set target to value */

/*
 * SD_SWITCH argument format:
 *
 *      [31] Check (0) or switch (1)
 *      [30:24] Reserved (0)
 *      [23:20] Function group 6
 *      [19:16] Function group 5
 *      [15:12] Function group 4
 *      [11:8] Function group 3
 *      [7:4] Function group 2
 *      [3:0] Function group 1
 */

/*
 * SD_SEND_IF_COND argument format:
 *
 *	[31:12] Reserved (0)
 *	[11:8] Host Voltage Supply Flags
 *	[7:0] Check Pattern (0xAA)
 */

/*
 * SCR field definitions
 */

#define SCR_SPEC_VER_0		0	/* Implements system specification 1.0 - 1.01 */
#define SCR_SPEC_VER_1		1	/* Implements system specification 1.10 */
#define SCR_SPEC_VER_2		2	/* Implements system specification 2.00 */

/*
 * SD bus widths
 */
#define SD_BUS_WIDTH_1		0
#define SD_BUS_WIDTH_4		2

/*
 * SD_SWITCH mode
 */
#define SD_SWITCH_CHECK		0
#define SD_SWITCH_SET		1

/*
 * SD_SWITCH function groups
 */
#define SD_SWITCH_GRP_ACCESS	0

/*
 * SD_SWITCH access modes
 */
#define SD_SWITCH_ACCESS_DEF	0
#define SD_SWITCH_ACCESS_HS	1

/**********************************/
/* From original SDHCI OSX driver */
/**********************************/
#define R0	0
#define R1	1
#define R1b	2
#define R2	3
#define R3	4
#define R4	5
#define R5	6
#define R5b 7
#define R6	8
#define R7	9

#define SDCR0	R0
#define SDCR1	R0
#define SDCR2	R2
#define SDCR3	R6
#define SDCR4	R0
#define SDCR5	R0
#define SDCR6	R1
#define SDCR7	R1b
#define SDCR8	R7
#define SDCR9	R2
#define SDCR10	R2
#define SDCR11	R0
#define SDCR12	R1b
#define SDCR13	R1
#define SDCR14	R0
#define SDCR15	R0
#define SDCR16	R1
#define SDCR17	R1
#define SDCR18	R1
#define SDCR19	R0
#define SDCR20	R0
#define SDCR21	R0
#define SDCR22	R0
#define SDCR23	R0
#define SDCR24	R1
#define SDCR25	R1
#define SDCR26	R0
#define SDCR27	R1
#define SDCR28	R1b
#define SDCR29	R1b
#define SDCR30	R1
#define SDCR31	R0
#define SDCR32	R1
#define SDCR33	R1
#define SDCR34	R0
#define SDCR35	R0
#define SDCR36	R0
#define SDCR37	R0
#define SDCR38	R1b
#define SDCR39	R0
#define SDCR40	R0
#define SDCR41	R0
#define SDCR42	R1
#define SDCR43	R0
#define SDCR44	R0
#define SDCR45	R0
#define SDCR46	R0
#define SDCR47	R0
#define SDCR48	R0
#define SDCR49	R0
#define SDCR50	R0
#define SDCR51	R0
#define SDCR52	R0
#define SDCR53	R0
#define SDCR54	R0
#define SDCR55	R1
#define SDCR56	R1
#define SDCR57	R0
#define SDCR58	R0
#define SDCR59	R0
#define SDCR60	R0
#define SDCR61	R0
#define SDCR62	R0
#define SDCR63	R0

#define SDACR0	R0
#define SDACR1	R0
#define SDACR2	R0
#define SDACR3	R0
#define SDACR4	R0
#define SDACR5	R0
#define SDACR6	R1
#define SDACR7	R0
#define SDACR8	R0
#define SDACR9	R0
#define SDACR10	R0
#define SDACR11	R0
#define SDACR12	R0
#define SDACR13	R1
#define SDACR14	R0
#define SDACR15	R0
#define SDACR16	R0
#define SDACR17	R0
#define SDACR18	R0
#define SDACR19	R0
#define SDACR20	R0
#define SDACR21	R1
#define SDACR22	R1
#define SDACR23	R0
#define SDACR24	R0
#define SDACR25	R0
#define SDACR26	R0
#define SDACR27	R0
#define SDACR28	R0
#define SDACR29	R0
#define SDACR30	R0
#define SDACR31	R0
#define SDACR32	R0
#define SDACR33	R0
#define SDACR34	R0
#define SDACR35	R0
#define SDACR36	R0
#define SDACR37	R0
#define SDACR38	R0
#define SDACR39	R0
#define SDACR40	R0
#define SDACR41	R3
#define SDACR42	R1
#define SDACR43	R0
#define SDACR44	R0
#define SDACR45	R0
#define SDACR46	R0
#define SDACR47	R0
#define SDACR48	R0
#define SDACR49	R0
#define SDACR50	R0
#define SDACR51	R1
#define SDACR52	R0
#define SDACR53	R0
#define SDACR54	R0
#define SDACR55	R0
#define SDACR56	R0
#define SDACR57	R0
#define SDACR58	R0
#define SDACR59	R0
#define SDACR60	R0
#define SDACR61	R0
#define SDACR62	R0
#define SDACR63	R0

#endif  /* SD_H  */
