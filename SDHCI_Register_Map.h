#include "License.h"
#include "SD_Misc.h"

struct __attribute__ ((__packed__)) SDHCIRegMap_t {
	volatile UInt32 SDMASysAddr;				//0x00
	volatile UInt16 BlockSize;					//0x04
	volatile UInt16 BlockCount;					//0x06
	volatile UInt32 Argument;					//0x08
	volatile UInt16 TransferMode;				//0x0C
	volatile UInt16 Command;					//0x0E
	volatile UInt32 Response[4];				//0x10
	volatile UInt32 BufferDataPort;				//0x20
	volatile UInt32 PresentState;				//0x24
	volatile UInt8  HostControl;				//0x28
	volatile UInt8  PowerControl;				//0x29
	volatile UInt8  BlockGapControl;			//0x2A
	volatile UInt8  WakeupControl;				//0x2B
	volatile UInt16 ClockControl;				//0x2C
	volatile UInt8  TimeoutControl;				//0x2E
	volatile UInt8  SoftwareReset;				//0x2F
	volatile UInt16 NormalIntStatus;			//0x30
	volatile UInt16 ErrorIntStatus;				//0x32
	volatile UInt16 NormalIntStatusEn;			//0x34
	volatile UInt16 ErrorIntStatusEn;			//0x36
	volatile UInt16 NormalIntSignalEn;			//0x38
	volatile UInt16 ErrorIntSignalEn;			//0x3A
	volatile UInt16 CMD12ErrorStatus;			//0x3C
	volatile UInt16 Reserved0;					//0x3E
	volatile UInt32 Capabilities[2];			//0x40
	volatile UInt32 MaxCurrentCap[2];			//0x48
	volatile UInt16 ForceEventCMD12ErrStatus;	//0x50
	volatile UInt16 ForceEventErrorIntStatus;	//0x52
	volatile UInt8  AMDAErrorStatus;			//0x54
	volatile UInt8  Reserved1;					//0x55
	volatile UInt16 Reserved2;					//0x56
	volatile UInt32 ADMASystemAddr[2];			//0x58
	volatile UInt16 ReservedArray[78];			//0x60
	volatile UInt16 SlotIntStatus;				//0xFC
	volatile UInt16 HostControllerVer;			//0xFE
};

//PresentState
#define CMDLineLevel	BIT24
#define DAT3Level		BIT23
#define DAT2Level		BIT22
#define DAT1Level		BIT21
#define DAT0Level		BIT20
#define WPSwitchLevel	BIT19
#define CardDetectLevel	BIT18
#define CardStateStable BIT17
#define CardInserted	BIT16
#define BuffReadEn		BIT11
#define BuffWriteEn		BIT10
#define ReadXferActive	BIT9
#define WriteXferActive	BIT8
#define DATLineActive	BIT2
#define ComInhibitDAT	BIT1
#define ComInhibitCMD	BIT0

//HostControl
#define CDSigSelection	BIT7
#define CDTestLevel		BIT6
#define DMASelSDMA		0
#define DMASel32ADMA2	BIT3
#define DMASel64ADMA2	BIT4|BIT3
#define HighSpeedEn		BIT2
#define DataXferWidth	BIT1
#define LedControl		BIT0

//PowerControl
#define HC3v3			0xE
#define HC3v0			0xD
#define HC1v8			0xA
#define SDPower			BIT0

//ClockControl
#define DIV256			BIT16
#define DIV128			BIT15
#define	DIV64			BIT14
#define DIV32			BIT13
#define DIV16			BIT12
#define DIV8			BIT11
#define DIV4			BIT10
#define	DIV2			BIT9
#define SDClockEn		BIT2
#define SDClockStable	BIT1
#define InternalClockEn BIT0

//Capabilities
#define CR64SysBus		BIT28
#define	CR1v8Support	BIT26
#define CR3v0Support	BIT25
#define CR3v3Support	BIT24
#define SusRsmSupport	BIT23
#define SDMASupport		BIT22
#define HighSpSupport	BIT21
#define ADMA2Support	BIT19
#define BlockLen512		0
#define BlockLen1024	BIT16
#define BlockLen2048	BIT17
#define BaseClockMask	BIT13|BIT12|BIT11|BIT10|BIT9|BIT8
#define TOutClockUnit	BIT7
#define TOutClockMask	BIT5|BIT4|BIT3|BIT2|BIT1|BIT0

//MaxCurrentCap
#define MaxCur1v8Mask	BIT23|BIT22|BIT21|BIT20|BIT19|BIT18|BIT17|BIT16
#define MaxCur3v0Mask	BIT15|BIT14|BIT13|BIT12|BIT11|BIT10|BIT9|BIT8
#define MaxCur3v3Mask	BIT7|BIT6|BIT5|BIT4|BIT3|BIT2|BIT1|BIT0

//HostControllerVer
#define VendorVerMask	BIT15|BIT14|BIT13|BIT12|BIT11|BIT10|BIT9|BIT8
#define SpecVerMask		BIT7|BIT6|BIT5|BIT4|BIT3|BIT2|BIT1|BIT0

//SoftwareReset
#define CMD_RESET		BIT1
#define DAT_RESET		BIT2
#define FULL_RESET		BIT0

//NormalIntStatus
#define ErrorInterrupt	BIT15
#define CardInterrupt	BIT8
#define CardRemoval		BIT7
#define CardInsertion	BIT6
#define BuffReadReady	BIT5
#define BuffWriteReady	BIT4
#define	DMAInterrupt	BIT3
#define BlockGapEvent	BIT2
#define	XferComplete	BIT1
#define	CmdComplete		BIT0

//ErrorIntStatus
#define ADMAError		BIT9
#define AutoCMD12Error	BIT8
#define CurLimitError	BIT7
#define DatEndBitError	BIT6
#define DatCRCError		BIT5
#define DatTimeoutError	BIT4
#define CmdIndexError	BIT3
#define CmdEndBitError	BIT2
#define CmdCRCError		BIT1
#define CmdTimeoutError	BIT0

