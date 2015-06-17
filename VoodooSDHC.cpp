#include "License.h"
/*****************************************************************************/
/* Build Configuration Defines */

/* Debug - mostly turns on more logging */
//#define __DEBUG__	1

/*
 * Builds the driver to prohibit writes to the card.  Useful while building
 * confidence in changes without corrupting data.
 */
//#define READONLY_DRIVER	1

/*
 * Builds the driver with High Speed Card support.
 */
//#define HIGHSPEED_CARD_MODE	1

/*
 * Builds the driver with 4-bit Bus support.
 */
#define WIDE_BUS_MODE	1

/*
 * Use multiblock reads/writes.  Define to either 0 or 1
 */
#define USE_MULTIBLOCK	1

/*
 * Use SDMA reads/writes.  Define to either 0 or 1
 */
#define USE_SDMA 1

/*
 * The Linux driver for this device claimed that the card needs to be reset
 * after every command.  That doesn't seem to be necessary so we turn on
 * NO_RESET_WAR.
 */
#define NO_RESET_WAR	1

#define SDMA_BUFFER_SIZE 32768
#define SDMA_BUFFER_SIZE_IN_REG 0x3000 /* see def. of Block Size Register */
#define SDMA_RETRY_COUNT 5


/*****************************************************************************/
//#include <libkern/OSByteOrder.h>

#include "VoodooSDHC.h"
#include "SDHCI_Register_Map.h"
#include "SD_Commands.h"
#include "sdhci.h"

#define MAX(a, b)	((a) > (b) ? (a) : (b))
#define MIN(a, b)	((a) < (b) ? (a) : (b))

#define	super IOBlockStorageDevice

OSDefineMetaClassAndStructors ( VoodooSDHC, IOBlockStorageDevice );

/*****************************************************************************/
/* Helper Functions */
/*
 * read_block_pio:  Read a single 512 byte  block of data with no error
 *		    checking from a PIO address to memory.
 *	volatile UInt32 *reg_addr:  Address of card's PIO register
 *	UInt8 *buf:  Buffer in which to place data - m
 */
static inline void read_block_pio(volatile UInt32 *reg_addr, UInt32 *buf) {
	UInt32 *bufp = buf;

	for (int i = 0; i < 512 / sizeof(UInt32); i++) {
		*bufp++ =  *reg_addr;
	}
}

/*****************************************************************************/
/* Main Driver Code */

/*
 * init:  Initialize driver.  Returns true on success, false on failure
 *	OSDictionary *properties:  Driver properties passed in
 */
bool VoodooSDHC::init ( OSDictionary * properties )
{
	IOLog("VoodooSDHCI ::: an SDHCI driver for Ricoh, TI, and JMicron SD Host Controllers ::: rev 20091008\n");
	IOLog("VoodooSDHCI: initializing SD host controller\n");
	/* Run by our superclass */
	if ( super::init ( properties ) == false )
	{
		return false;
	}	
	
#ifdef USE_SDMA
	if ((workLoop = IOWorkLoop::workLoop()) == NULL)
		return false;
#endif
	
	return true;
}

void VoodooSDHC::free()
{
#ifdef USE_SDMA
	if (workLoop != NULL) {
		workLoop->release();
		workLoop = NULL;
	}
#endif
	super::free();
}

/*
 * start:  Starts driver.  Called upon insertion of card.  Returns true
 *	   on success, false on failure.
 *		IOService *provider:  Provider structure
 */
bool VoodooSDHC::start ( IOService * provider )
{
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: running start()\n");
	IOLog("VoodooSDHCI: we have found %d SD Host Controllers\n",provider->getDeviceMemoryCount());
#endif
	super::start ( provider );
	lock.init();
#ifdef USE_SDMA
	sdmaCond = IOLockAlloc();
	mediaStateLock = IOLockAlloc();
#endif
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: starting card power management\n");
#endif
	// initialize superclass variables from IOService.h
	PMinit();
	static const IOPMPowerState powerStates[] = {
		{kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{kIOPMPowerStateVersion1, kIOPMDeviceUsable, IOPMPowerOn, IOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
	};
	registerPowerDriver(this, const_cast<IOPMPowerState*>(powerStates),
		sizeof(powerStates) / sizeof(powerStates[0]));
	// join the tree from IOService.h
	provider->joinPMtree(this);
	
#ifdef USE_SDMA
	sdmaBuffDesc = IOBufferMemoryDescriptor::withCapacity(SDMA_BUFFER_SIZE * 2, kIODirectionInOut, true);
	physSdmaBuff = sdmaBuffDesc->getPhysicalAddress();
	virtSdmaBuff = (char*)sdmaBuffDesc->getBytesNoCopy() + SDMA_BUFFER_SIZE - physSdmaBuff % SDMA_BUFFER_SIZE;
	physSdmaBuff += SDMA_BUFFER_SIZE - physSdmaBuff % SDMA_BUFFER_SIZE;
#endif
	
	cardPresence = kCardNotPresent;
	if (! setup(provider)) {
		return false;
	}

#ifdef USE_SDMA
	IOWorkLoop *workLoop;
	if ((workLoop = getWorkLoop()) == NULL) {
		IOLog("VoodooSDHCI: unable to get a workloop; getWorkLoop() == NULL\n");
		return false;
	}
	if ((interruptSrc = IOFilterInterruptEventSource::filterInterruptEventSource(
			this, interruptHandler, interruptFilter, provider
		)) == NULL) {
		IOLog("VoodooSDHCI: failed to create an interrupt source\n");
		return false;
	}
	if (workLoop->addEventSource(interruptSrc) != kIOReturnSuccess) {
		IOLog("VoodooSDHCI: failed to add FIES to work loop\n");
		return false;
	}
	if ((timerSrc = IOTimerEventSource::timerEventSource(this, timerHandler)) == NULL) {
		IOLog("VoodooSDHCI: failed to create a timer event source\n");
		return false;
	}
	if (workLoop->addEventSource(timerSrc) != kIOReturnSuccess) {
		IOLog("VoodooSDHCI: failed ot add TES to work loop\n");
		return false;
	}
#endif
	
	this->attach(this);
	registerService();

#ifdef USE_SDMA
	interruptSrc->enable();
	timerSrc->enable();
	timerSrc->setTimeoutMS(50); // intial timeout is small, to detect card insertion ASAP
#endif
	
	// The controller is now initialized and ready for operation
	return true;
}

/*
 * setup:  Initializes I/O, called upon start and resume, returns true on success.
 *		IOService *provider:  Provider structure
 */
bool VoodooSDHC::setup(IOService * provider)
{
	IODeviceMemory *	pMem;
	UInt8 slot = 0;

	for (slot=0; slot<provider->getDeviceMemoryCount(); slot++) {
		pMem = provider->getDeviceMemoryWithIndex(0);
		this->PCIRegMap = provider->mapDeviceMemoryWithIndex(0);
		if (!this->PCIRegMap) {
			IOLog("VoodooSDHCI: PCI Register Mapping for Device %d Failed!\n", (int)slot);
			return false;
		}

		this->PCIRegP[slot] =
				(SDHCIRegMap_t *)PCIRegMap->getVirtualAddress();
#ifdef __DEBUG__
		IOLog("VoodooSDHCI: controller slot == %d\n", slot);
		IOLog("VoodooSDHCI: unit memory (pMem) == %d\n", pMem->getLength());
#endif
		this->PCIRegP[slot]->PowerControl = 0;
		Reset(slot, FULL_RESET);
		IODelay(10000);
		if (cardPresence == kCardIsPresent && isCardPresent(slot)) {
			SDCIDReg_t oldCID = SDCIDReg[slot];
			cardInit(slot);
			if (memcmp(&oldCID, SDCIDReg + slot, sizeof(oldCID)) != 0) {
				IOLog("VoodooSDHCI: oops! we found a different card :: remount?\n");
				cardPresence = kCardRemount;
			}
		}
	}
	
	return true;
}

/*
 * stop:  Called on completion of service, such as ejection of card's
 *	  filesystem.
 *	IOService *provider:  Our provider structure
 */
void VoodooSDHC::stop(IOService *provider)
{
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: card is in stop() function\n");
#endif
#ifdef USE_SDMA
	if (timerSrc != NULL) {
		timerSrc->disable();
		timerSrc->cancelTimeout();
		getWorkLoop()->removeEventSource(timerSrc);
		timerSrc->release();
		timerSrc = NULL;
	}
	if (interruptSrc != NULL) {
		interruptSrc->disable();
		getWorkLoop()->removeEventSource(interruptSrc);
		interruptSrc->release();
		interruptSrc = NULL;
	}
	sdmaBuffDesc->release();
#endif
	
	PMstop();
#ifdef USE_SDMA
	IOLockFree(sdmaCond);
	IOLockFree(mediaStateLock);
#endif
	lock.free();
	
	// Call our superclass
	super::stop ( provider );
	
}
/*
 * dumpRegs:  Dump selected Host registers.  Only used for driver
 *	      debugging.
 *	UInt8 slot:  Which card slot to dump
 */
void VoodooSDHC::dumpRegs(UInt8 slot) {
	IOLog("VoodooSDHCI: Register Dump ******************************************************\n");
	IOLog("VoodooSDHCI: SDMASysAddr:          0x%08X PresentState:         0x%08X\n", 
		this->PCIRegP[slot]->SDMASysAddr, this->PCIRegP[slot]->PresentState);
	IOLog("VoodooSDHCI: BlockSize:                0x%04X BlockCount:               0x%04X\n", 
		this->PCIRegP[slot]->BlockSize, this->PCIRegP[slot]->BlockCount);
	IOLog("VoodooSDHCI: TransferMode:             0x%04X Command:                  0x%04X\n", 
		this->PCIRegP[slot]->TransferMode, this->PCIRegP[slot]->Command);
	IOLog("VoodooSDHCI: HostControl:                0x%02X PowerControl:               0x%02X\n", 
		this->PCIRegP[slot]->HostControl, this->PCIRegP[slot]->PowerControl);
	IOLog("VoodooSDHCI: BlockGapControl:            0x%02X WakeupControl:              0x%02X\n", 
		this->PCIRegP[slot]->BlockGapControl, this->PCIRegP[slot]->WakeupControl);
	IOLog("VoodooSDHCI: ClockControl:             0x%04X TimeoutControl:             0x%02X\n", 
		this->PCIRegP[slot]->ClockControl, this->PCIRegP[slot]->TimeoutControl);
	IOLog("VoodooSDHCI: SoftwareReset:              0x%02X NormalIntStatus:          0x%04X\n", 
		this->PCIRegP[slot]->SoftwareReset, this->PCIRegP[slot]->NormalIntStatus);
	IOLog("VoodooSDHCI: ErrorIntStatus:           0x%04X NormalIntStatusEn:        0x%04X\n", 
		this->PCIRegP[slot]->ErrorIntStatus, this->PCIRegP[slot]->NormalIntStatusEn);
	IOLog("VoodooSDHCI: ErrorIntStatusEn:         0x%04X NormalIntSignalEn:        0x%04X\n", 
		this->PCIRegP[slot]->ErrorIntStatusEn, this->PCIRegP[slot]->NormalIntSignalEn);
	IOLog("VoodooSDHCI: ErrorIntSignalEn:         0x%04X CMD12ErrorStatus:         0x%04X\n", 
		this->PCIRegP[slot]->ErrorIntSignalEn, this->PCIRegP[slot]->CMD12ErrorStatus);
	IOLog("VoodooSDHCI: Capabilities[1]:      0x%08X Capabilities[0]:      0x%08X\n", 
		this->PCIRegP[slot]->Capabilities[1], this->PCIRegP[slot]->Capabilities[0]);
	IOLog("VoodooSDHCI: MaxCurrentCap[1]:     0x%08X MaxCurrentCap[0]:     0x%08X\n", 
		this->PCIRegP[slot]->MaxCurrentCap[1], this->PCIRegP[slot]->MaxCurrentCap[0]);
	IOLog("VoodooSDHCI: ForceEventCMD12ErrStatus: 0x%04X ForceEventErrorIntStatus: 0x%04X\n", 
		this->PCIRegP[slot]->ForceEventCMD12ErrStatus, this->PCIRegP[slot]->ForceEventErrorIntStatus);
	IOLog("VoodooSDHCI: AMDAErrorStatus:            0x%02X Argument              0x%08X\n", 
		this->PCIRegP[slot]->AMDAErrorStatus, PCIRegP[slot]->Argument);
	IOLog("VoodooSDHCI: ADMASystemAddr[1]:    0x%08X ADMASystemAddr[0]:    0x%08lX\n", 
		this->PCIRegP[slot]->ADMASystemAddr[1], this->PCIRegP[slot]->ADMASystemAddr[0]);
	IOLog("VoodooSDHCI: SlotIntStatus:            0x%04X HostControllerVer:        0x%04X\n", 
		this->PCIRegP[slot]->SlotIntStatus, this->PCIRegP[slot]->HostControllerVer);
	IOLog("VoodooSDHCI: Response[1]:          0x%08X Response[0]:          0x%08X\n", 
		this->PCIRegP[slot]->Response[1], this->PCIRegP[slot]->Response[0]);
		IOLog("VoodooSDHCI: Response[3]:          0x%08X Response[2]:          0x%08X\n", 
		this->PCIRegP[slot]->Response[3], this->PCIRegP[slot]->Response[2]);	
	IOLog("VoodooSDHCI: End of Register Dump************************************************\n");
}

/*
 * cardInit:  Initialize a card.  Called upon insertion.  When complete,
 *	      card should be running at full speed and card data
 *	      populated.
 *	UInt8 slot:  Which slot the card is in.
 */
bool VoodooSDHC::cardInit(UInt8 slot)
{
	isHighCapacity = false;
	calcClock(slot, 400000);
	powerSD(slot);
	SDCommand(slot, SD_GO_IDLE_STATE, SDCR0, 0);
	IODelay(30000);
	SDCommand(slot, SD_SEND_IF_COND, SDCR8, 0x000001AA);
	for (int i = 0; i < 100; i++) {
		IODelay(10000);
		if (!(PCIRegP[slot]->PresentState & ComInhibitCMD)) {
			break;
		}
	}
	
	if(this->PCIRegP[slot]->PresentState & ComInhibitCMD) {
		IOLog("VoodooSDHCI: no response from CMD_8 -- ComInhibitCMD\n");
		Reset(slot, CMD_RESET);
		Reset(slot, DAT_RESET);
		SDCommand(slot, SD_GO_IDLE_STATE, SDCR0, 0);
		do {
			SDCommand(slot, SD_APP_CMD, SDCR55, 0);
			SDCommand(slot, SD_APP_OP_COND, SDACR41, 0x00FF8000);
			IODelay(1000);
		} while (!(this->PCIRegP[slot]->Response[0] & BIT31));
	} else {
		// check and init SDHC (wait for 2 secs; spec requires 1 sec)
		IOLog("VoodooSDHCI: initializing spec 2.0 SD card\n");
		for (int i = 0; i < 80; i++) {
#ifdef __DEBUG__
			IOLog("VoodooSDHCI: sending CMD_55\n");
#endif //me
			SDCommand(slot, SD_APP_CMD, SDCR55, 0);
#ifdef __DEBUG__
			IOLog("VoodooSDHCI: sending APP_CMD_41\n");
#endif //me
			SDCommand(slot, SD_APP_OP_COND, SDACR41, 0x40FF8000);
			IODelay(25000);
			if (this->PCIRegP[slot]->Response[0] & BIT31) {
				goto OP_COND_COMPLETE;
			}
		}
#ifdef __DEBUG__
		IOLog("VoodooSDHCI: no response to APP_CMD_41: 0x%08x\n", PCIRegP[slot]->Response[0]);
#endif
		return false;
	OP_COND_COMPLETE:
#ifdef __DEBUG__
		IOLog("VoodooSDHCI: got response to APP_CMD_41: 0x%08x\n", PCIRegP[slot]->Response[0]);
#endif
		if (this->PCIRegP[slot]->Response[0] & BIT30) {
			IOLog("VoodooSDHCI: we have HC card\n");
			isHighCapacity = true;
		} else {
			IOLog("VoodooSDHCI: standard SD (without HC)\n");
		}
	}
	
	
	SDCommand(slot, SD_ALL_SEND_CID, SDCR2, 0);
	IODelay(1000);
	parseCID(slot);
	SDCommand(slot, SD_SET_RELATIVE_ADDR, SDCR3, 0);
	IODelay(1000);
	calcClock(slot, 25000000);
	this->RCA = this->PCIRegP[slot]->Response[0] >> 16;
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: RCA == 0x%08X\n", this->RCA);
#endif//me
	SDCommand(slot, SD_SEND_CSD, SDCR9, this->RCA << 16);
	IODelay(2000000);
	parseCSD(slot);
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: PCIRegP response order (3,2,1,0) :: 0x%08X 0x%08X 0x%08X 0x%08X\n", 
			this->PCIRegP[slot]->Response[3],
			this->PCIRegP[slot]->Response[2],
			this->PCIRegP[slot]->Response[1],
			this->PCIRegP[slot]->Response[0]);
#endif//me
	SDCommand(slot, SD_SELECT_CARD, SDCR7, this->RCA << 16);
	IODelay(10000);

#ifdef WIDE_BUS_MODE
	/* XXX - Need to check whether the card is capable before enabiling this */
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: WIDE_BUS_MODE :: setting 4 bit mode\n");
#endif //me
	SDCommand(0, SD_APP_CMD, SDCR55, this->RCA << 16);
	SDCommand(slot, SD_APP_SET_BUS_WIDTH, SDCR6, 2);
	IODelay(30000);
#ifdef	__DEBUG__
	IOLog("VoodooSDHCI: PCIRegP response order (3,2,1,0) :: 0x%08X 0x%08X 0x%08X 0x%08X\n", 
			this->PCIRegP[slot]->Response[3],
			this->PCIRegP[slot]->Response[2],
			this->PCIRegP[slot]->Response[1],
			this->PCIRegP[slot]->Response[0]);
#endif//me
	if (!(this->PCIRegP[slot]->Response[0] & 0x480000)) { /* check ERROR and ILLEGAL COMMAND */
		this->PCIRegP[slot]->HostControl |= SDHCI_CTRL_4BITBUS;
#ifdef __DEBUG__
		IOLog("VoodooSDHCI: properly switched to 4 bit mode\n");
#endif//me
	} else {
#ifdef __DEBUG__
		IOLog("VoodooSDHCI: unable to switch to 4 bit mode -- calling Reset(slot, {CMD,DAT}_RESET)\n");
#endif//me
		Reset(slot, CMD_RESET);
		Reset(slot, DAT_RESET);
	}
	IODelay(30000);
#endif /* WIDE_BUS_MODE */
#ifdef HIGHSPEED_CARD_MODE
	/* XXX - Need to check whether the card is capable before enabiling this */
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: HIGHSPEED_CARD_MODE \n");
#endif //me
	SDCommand(slot, SD_SWITCH, SDCR6, 0x01fffff1);
	IODelay(10000);
	calcClock(slot, 50000000);
	this->PCIRegP[slot]->HostControl |= SDHCI_CTRL_HISPD;
#endif /* HIGHSPEED_CARD_MODE */

	this->PCIRegP[slot]->BlockSize = 512;
	this->PCIRegP[slot]->BlockCount = 1;
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: Card Init:  Host Control = 0x%x\n", this->PCIRegP[slot]->HostControl);
#endif
	this->PCIRegP[slot]->HostControl |= 0x1;
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: Card Init:  Host Control = 0x%x\n", this->PCIRegP[slot]->HostControl);
#endif
	return true;
}

/*
 * isCardPresent:  Return true if card is present, false otherwise
 *	UInt8 slot:  Which slot the card is in
 */
bool VoodooSDHC::isCardPresent(UInt8 slot) {
	return PCIRegP[slot]->PresentState & CardInserted;
}

/*
 * isCardWP:  Return true if card is write protected, false otherwise
 *	UInt8 slot:  Which slot the card is in.
 */
bool VoodooSDHC::isCardWP(UInt8 slot) {
#ifndef READONLY_DRIVER
	return !(this->PCIRegP[slot]->PresentState & WPSwitchLevel);
#else
	return true;
#endif	
}

IOReturn VoodooSDHC::setPowerState ( unsigned long state, IOService * provider )
// Note that it is safe to ignore the whatDevice parameter.
{
	switch (state) {
	case 0: // sleep
#ifdef __DEBUG__
		IOLog("VoodooSDHCI: sleep requested by thread: 0x%08x\n", (int)IOThreadSelf());
#endif //me
		lock.lock();
		break;
	case 1: // wakeup
#ifdef __DEBUG__
		IOLog("VoodooSDHCI: wakeup requested by thread: 0x%08x\n", (int)IOThreadSelf());
#endif //me
		setup(getProvider());
		lock.unlock();
		break;
	} 
	return kIOPMAckImplied;
}

/*
 * LEDControl:  Turns on/off LED on card slot.  Not present on Dell Mini 9.
 *	UInt8 slot:  Which slot the card is in.
 *	bool state:  True for on, false for off
 */
void VoodooSDHC::LEDControl(UInt8 slot, bool state) {
	if (state) {
		this->PCIRegP[slot]->HostControl |= LedControl;
	} else {
		this->PCIRegP[slot]->HostControl &= ~LedControl;
	}
}

/*
 * Reset:  Reset SDHCI host controller.  Reset levels are defined by SDHCI
 *		   standard.
 *	UInt8 slot:  Which host controller to reset
 *	UInt8 type:  Reset type (Command, Data, or Full)
 */
void VoodooSDHC::Reset(UInt8 slot, UInt8 type)
{
	switch(type) {
		case CMD_RESET:
			this->PCIRegP[slot]->SoftwareReset = CMD_RESET;
			break;
		case DAT_RESET:
			this->PCIRegP[slot]->SoftwareReset = DAT_RESET;
			break;
		default:
			this->PCIRegP[slot]->SoftwareReset = FULL_RESET;
			break;
	}
	while(this->PCIRegP[slot]->SoftwareReset);
}

/*
 * SDCommand:  Send a single command to the SDHCI Host controller.  Return true on
 *			   success, false on failure.  Will spin wait indefinitely if device is
 *			   busy.
 *		UInt8 slot:  Which slot the card to send to is in
 *		UInt8 command:  SDHC command as defined in SDHC Physical Interface
 *		UInt16 response:  Response type to expect for command passed in
 *		UInt32 arg:  Command argument as defined in SDHC Physical Interface
 */
bool VoodooSDHC::SDCommand(UInt8 slot, UInt8 command, UInt16 response,
								UInt32 arg) {
	if (command != 0) {
		while(this->PCIRegP[slot]->PresentState & ComInhibitCMD);
	}

	//if(command 1= COMMANDS?)
	//{
		//Wait for DAT to free up
		//while(this->PCIRegP[slot]->PresentState & ComInhibitDAT);
	//}
	
	switch(response) { //See SD Host Controller Spec Version 2.00 Page 30
		case R0: 
			response = 0;
			break;
		case R1: 
			response = BIT4|BIT3|BIT1;
			break;
		case R1b: 
			response = BIT4|BIT3|BIT1|BIT0;
			break;
		case R2: 
			response = BIT3|BIT0;
			break;
		case R3: 
			response = BIT1;
			break;
		case R4: 
			response = BIT1;
			break;
		case R5: 
			response = BIT4|BIT3|BIT1;
			break;
		case R5b: 
			response = BIT4|BIT3|BIT1|BIT0;
			break;
		case R6: 
			response = BIT4|BIT3|BIT1;
			break;
		case R7: 
			response = BIT4|BIT3|BIT1;
			break;
	}
	this->PCIRegP[slot]->Argument = arg;

	if (command == 17 || command == 24)
		response |= BIT5;

	if (command == SD_READ_MULTIPLE_BLOCK) {
		response |= BIT5;
		this->PCIRegP[0]->TransferMode =
			SDHCI_TRNS_READ | SDHCI_TRNS_MULTI |
			SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_ACMD12
#ifdef USE_SDMA
			| SDHCI_TRNS_DMA
#endif
		;
	}

	if (command == SD_WRITE_MULTIPLE_BLOCK) {
		response |= BIT5;
		this->PCIRegP[0]->TransferMode = SDHCI_TRNS_MULTI |
			SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_ACMD12
#ifdef USE_SDMA
			| SDHCI_TRNS_DMA
#endif
		;
	}

	this->PCIRegP[slot]->Command = (command << 8) | response;

//	IOLog("Command: %d", (command << 8) | response);
	return true;
}

/*
 * calcClock:  Calculate card clock rate.  See SDHCI Host Controller spec
 *	       for details on calculation.  Must be called after cardInit.
 *	UInt8 slot:  Host controller/slot number
 *	UInt32 clockspeed:  Maximum desired clock speed
 */
bool VoodooSDHC::calcClock(UInt8 slot, UInt32 clockspeed) {
	UInt32 baseClock;
	UInt32 div;

	this->PCIRegP[slot]->ClockControl = 0;
	this->PCIRegP[slot]->ClockControl = 0;
	this->PCIRegP[slot]->ClockControl |= BIT0;
	while(!this->PCIRegP[slot]->ClockControl & BIT1);
	baseClock = ((this->PCIRegP[slot]->Capabilities[0] & 0x3F00) >> 8);
	baseClock *= 1000000;

#ifdef __DEBUG__
	IOLog("VoodooSDHCI: BaseClock :: %dMHz\n", baseClock/1000000);
#endif //me

	for(div=1;(baseClock / div) > clockspeed;div <<= 1);

#ifdef __DEBUG__
	IOLog("VoodooSDHCI: SD Clock :: %dKHz\n", (baseClock/div)/1000);
#endif //me

	div = (div<<7) & 0xFF000;
	this->PCIRegP[slot]->ClockControl |= div;
	this->PCIRegP[slot]->ClockControl |= BIT2;
	return true;
}

/*
 * powerSD:  Turn on power to SD Card.  Must pay attention to voltage
 *	     level supported.  This is determined from the Host capability
 *	     register.
 *	UInt8 slot:  Slot the card is in.
 */
bool VoodooSDHC::powerSD(UInt8 slot) {
	this->PCIRegP[slot]->PowerControl = 0;
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: in power_sd(slot) function ::  0x%x\n", this->PCIRegP[slot]->Capabilities[0]);
#endif
	if(this->PCIRegP[slot]->Capabilities[0] & CR3v3Support) {
		this->PCIRegP[slot]->PowerControl |= HC3v3;
	} else if(this->PCIRegP[slot]->Capabilities[0] & CR3v0Support) {
		this->PCIRegP[slot]->PowerControl |= HC3v0;
	} else if(this->PCIRegP[slot]->Capabilities[0] & CR1v8Support) {
		this->PCIRegP[slot]->PowerControl |= HC1v8;
	}
	this->PCIRegP[slot]->PowerControl |= SDPower;
		
	return true;
}

/*
 * parseCID:  Parse Card Idenitification information
 *		UInt8 slot:  slot the card is in
 */
void VoodooSDHC::parseCID(UInt8 slot) {
	this->SDCIDReg[slot].MID = (UInt8)(this->PCIRegP[slot]->Response[3] & 0xFF000000) >> 24;
	this->SDCIDReg[slot].OID = (UInt16)(this->PCIRegP[slot]->Response[3] & 0xFFFF00) >> 8;
	this->SDCIDReg[slot].PNM[0] = (UInt8)(this->PCIRegP[slot]->Response[3] & 0xFF);
	this->SDCIDReg[slot].PNM[1] = (UInt8)((this->PCIRegP[slot]->Response[2] & 0xFF000000) >> 24);
	this->SDCIDReg[slot].PNM[2] = (UInt8)((this->PCIRegP[slot]->Response[2] & 0xFF0000) >> 16);
	this->SDCIDReg[slot].PNM[3] = (UInt8)((this->PCIRegP[slot]->Response[2] & 0xFF00) >> 8);
	this->SDCIDReg[slot].PNM[4] = (UInt8)(this->PCIRegP[slot]->Response[2] & 0xFF);
	this->SDCIDReg[slot].PNM[5] = 0;
	this->SDCIDReg[slot].PRV[0] = (UInt8)(this->PCIRegP[slot]->Response[1] & 0xF0000000) >> 28;
	this->SDCIDReg[slot].PRV[1] = (UInt8)(this->PCIRegP[slot]->Response[1] & 0xF000000) >> 24;
	this->SDCIDReg[slot].PSN = (this->PCIRegP[slot]->Response[1] & 0xFFFFFF) << 8;
	this->SDCIDReg[slot].PSN |= (this->PCIRegP[slot]->Response[0] & 0xFF000000) >> 24;
	this->SDCIDReg[slot].MDT[0] = (UInt8)(this->PCIRegP[slot]->Response[0] & 0xFF000) >> 12;
	this->SDCIDReg[slot].MDT[1] = (UInt8)(this->PCIRegP[slot]->Response[1] & 0xF00) >> 8;
}

/*
 * parseCSD:  Parse CSD information
 *		UInt8 slot:  slot the card is in
 */
void VoodooSDHC::parseCSD(UInt8 slot) {
	switch ((UInt8)((this->PCIRegP[slot]->Response[3] & 0xC00000) >> 22)) {
		case 0: // version 1
		{
			UInt8 blLen = (UInt8)((PCIRegP[slot]->Response[2] & 0xF00) >> 8);
			UInt16 cSize = (UInt16)((PCIRegP[slot]->Response[2] & 0x3) << 10);
			cSize |= (UInt16)((PCIRegP[slot]->Response[1] & 0xFFC00000) >> 22);
			UInt8 cSizeMult = (UInt8)((PCIRegP[slot]->Response[1] & 0x000380) >> 7);
			int large_to_small = (1 << (blLen)) / 512;
			maxBlock = (cSize+1) * large_to_small *
			(1 << (cSizeMult+2)) - 1;
		}
			break;
		case 1: // version 2
		{
			int units = (UInt16)(PCIRegP[slot]->Response[1] >> 24);
			units |= (PCIRegP[slot]->Response[1] & 0x00ff0000) >> 8;
			units |= (PCIRegP[slot]->Response[2] & 0x3f) << 16;
			maxBlock = (units + 1) * 1024;
		}
			break;
		default:
			// donno how to bail out...
			;
	}
}

/*
 * reportRemovability:  Apple API function.  An SD Card is a removeable
 *		        device.  Returns an I/O success.
 *	bool *isRemovable:  Passed back.  Always return true.
 */
IOReturn VoodooSDHC::reportRemovability(bool *isRemovable) {
	*isRemovable = true;
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: reportRemovability\n");
#endif
	return kIOReturnSuccess;
}

/*
 * reportWriteProtection:  Apple API function.  Returns whether the card is
 *		write protected.  Returns an I/O success.
 *	bool *isWriteProtected:  Passed back.  Always return true.
 */
IOReturn VoodooSDHC::reportWriteProtection(bool *isWriteProtected) {
// XXX - how is this supposed to work for multi-slot???
	*isWriteProtected = isCardWP( 0 );
	//*isWriteProtected = true;
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: reportWriteProtection\n");
#endif
	return kIOReturnSuccess;
}

/*
 * setWriteCacheState:  Apple API function.  This driver does not support
 *			write cacheing.	 Returns an I/O success.
 *		bool enabled:  Enable/disable cache
 */
IOReturn VoodooSDHC::setWriteCacheState(bool enabled) {
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: setWriteCacheState\n");
#endif
	return kIOReturnSuccess;
}

/*
 * reportPollRequirements:  Apple API function.  Report back requirements for
 *                          polling.  This driver must be polled, but the poll
 *			    is cheap.
 *	bool *pollRequired:  This is a return value from this function
 *	bool *pollIsExpensive:  This is a return value from this function
 */
IOReturn VoodooSDHC::reportPollRequirements(bool *pollRequired, bool *pollIsExpensive)
{
	*pollRequired = false;
	*pollIsExpensive = false;
	return kIOReturnSuccess;
}

IOReturn VoodooSDHC::reportMediaState(bool *mediaPresent, bool *changedState)
{
	IOLockLock(mediaStateLock);
	
	bool presence = isCardPresent(0);
	if (cardPresence == kCardRemount) {
		*changedState = true;
		cardPresence = kCardNotPresent;
	} else if ((cardPresence == kCardIsPresent) == presence) {
		*changedState = false;
	} else {
		*changedState = true;
		if (presence) {
			Reset(0, FULL_RESET);
			cardInit(0);
			::OSSynchronizeIO();
			cardPresence = kCardIsPresent;
		} else {
			cardPresence = kCardNotPresent;
		}
	}
	*mediaPresent = cardPresence == kCardIsPresent;

	IOLockUnlock(mediaStateLock);
	return kIOReturnSuccess;
}

#ifndef __LP64__
IOReturn VoodooSDHC::reportMaxWriteTransfer(UInt64 blockSize,
								UInt64 *max) {
	//Max blocks we can read at once (see Block Count Register)
	*max = 64 * blockSize;

#ifdef __DEBUG__
	IOLog("VoodooSDHCI: reportMaxWriteTransfer\n");
#endif /* end DEBUG */

	return kIOReturnSuccess;
}

IOReturn VoodooSDHC::reportMaxReadTransfer (UInt64 blockSize,
											UInt64 *max) {
	//Max blocks we can read at once (see Block Count Register)
	*max = 65536 * blockSize;
	
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: reportMaxReadTransfer\n");
#endif /* end DEBUG */
	
	return kIOReturnSuccess;
}
#endif /* !__LP64__ */

IOReturn VoodooSDHC::reportMaxValidBlock(UInt64 *maxBlock) {
	*maxBlock = this->maxBlock;

#ifdef __DEBUG__
	IOLog("VoodooSDHCI: reportMaxValidBlock\n");
#endif

	return kIOReturnSuccess;
}

IOReturn VoodooSDHC::reportLockability(bool *isLockable) {
	*isLockable = false;
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: reportLockability\n");
#endif
	return kIOReturnSuccess;
}

IOReturn VoodooSDHC::reportEjectability(bool *isEjectable) {
	*isEjectable = false;
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: reportEjectability\n");
#endif
	return kIOReturnSuccess;
}


IOReturn VoodooSDHC::reportBlockSize(UInt64 *blockSize) {
	// Read/write block size is always 512 bytes.  Card's block size
	// is reported but only used in computations of size, not access.
	*blockSize = 512;
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: reportBlockSize:  %d\n", *blockSize);
#endif
	return kIOReturnSuccess;
}

IOReturn VoodooSDHC::getWriteCacheState(bool *enabled) {
	*enabled = false;
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: getWriteCacheState\n");
#endif
	return kIOReturnSuccess;
}

char * VoodooSDHC::getVendorString(void) {
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: getVendorString\n");
#endif
	return("Generic");
}

char * VoodooSDHC::getRevisionString(void) {
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: getRevisionString\n");
#endif
	return("2");
}

char * VoodooSDHC::getProductString(void) {
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: getProductString\n");
#endif
	return("SDHCI Controller");
}

char * VoodooSDHC::getAdditionalDeviceInfoString(void) {
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: getAdditionalDeviceInfoString\n");
#endif
	return("VoodooSDHCI:getAdditionalDeviceInfoString");
}

IOReturn VoodooSDHC::doSynchronizeCache(void) {
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: doSynchronizeCache\n");
#endif
	return kIOReturnSuccess;
}

IOReturn VoodooSDHC::doLockUnlockMedia(bool doLock) {
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: doLockUnlockMedia\n");
#endif
	return kIOReturnUnsupported;
}

UInt32 VoodooSDHC::doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const {
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: doGetFormatCapacities\n");
#endif
	return kIOReturnSuccess;
}

IOReturn VoodooSDHC::doFormatMedia(UInt64 byteCapacity) {
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: doFormatMedia\n");
#endif
	return kIOReturnUnsupported;
}

IOReturn VoodooSDHC::doEjectMedia(void) {
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: doEjectMedia\n");
#endif
	return kIOReturnUnsupported;
}

bool VoodooSDHC::waitIntStatus(UInt32 maskBits)
{
	// roughly 5 seconds before timeout
	for (int cnt = 0; cnt < 500000; cnt++) {
		while (1) {
			UInt32 nis = PCIRegP[0]->NormalIntStatus;
			if (nis & ErrorInterrupt) {
				return false;
			}
			if (nis & maskBits) {
				PCIRegP[0]->NormalIntStatus = nis | maskBits;
				return true;
			}
		}
		::IODelay(10);
	}
	return false;
}

/*
 * readBlockMulti_pio:  Read a set of blocks using multi-block reads and PIO
 *                      not DMA mode.  The host controller must be locked when
 *			this function is called.
 *		IOMemoryDescriptor *buffer:  Buffer operation class.  Defines
 *				read/write, address of operation, etc.
 *		UInt32 block:  Block offset to read/write
 *		UInt32 nblks:  Block count to read/write
 *		UInt32 offset:  Offset from beginning of transfer - where in
 *				final buffer we should begin placing data
 */
IOReturn VoodooSDHC::readBlockMulti_pio(IOMemoryDescriptor *buffer,
					UInt32 block, UInt32 nblks,
					UInt32 offset) {
	UInt8 buff[512];	// Temporary storage for one block
	UInt32 *pBuff;
	IOReturn ret = kIOReturnError;

#ifdef __DEBUG__
	IOLog("VoodooSDHCI: readBlockMulti_pio:  block = %d, nblks = %d\n", block, nblks);
#endif /* __DEBUG__ */

	pBuff = (UInt32*)buff;

#ifndef NO_RESET_WAR	
	// Reset card before every operation.  The Linux driver
	// does this for this host controller. Not sure why.
	Reset(0, CMD_RESET);
	Reset(0, DAT_RESET);
#endif

	/* Enable all interrupts */
	this->PCIRegP[0]->NormalIntStatusEn = -1;
	this->PCIRegP[0]->ErrorIntStatusEn = -1;

	/* Clear pending interrupts */
	this->PCIRegP[0]->NormalIntStatus = 
			(BuffReadReady | XferComplete | CmdComplete);

	/* Set maximum timeout value */
	this->PCIRegP[0]->TimeoutControl = 0xe;

	*(volatile UInt32 *)&(this->PCIRegP[0]->NormalIntStatus) =
		*(volatile UInt32 *)&(this->PCIRegP[0]->NormalIntStatus);

	this->PCIRegP[0]->BlockSize = 512;
	this->PCIRegP[0]->BlockCount = nblks;

	this->PCIRegP[0]->TransferMode = SDHCI_TRNS_READ | SDHCI_TRNS_MULTI |
	  		SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_ACMD12;

	
	// Issue read command to host controller
	SDCommand(0, SD_READ_MULTIPLE_BLOCK, SDCR18, isHighCapacity ? block : block * 512);
	
	// wait for CmdComplete
	if (! waitIntStatus(CmdComplete)) {
		IOLog("VoodooSDHCI: I/O error after command 18: It Status: 0x%x\n", PCIRegP[0]->NormalIntStatus);
		goto out;
	}
	
	for (int i = 0; i < nblks; i++) {
		// wait for BufferReadReady
		if (! waitIntStatus(BuffReadReady)) {
			IOLog("VoodooSDHCI: I/O timeout while waiting for data, Status: 0x%0x\n", PCIRegP[0]->NormalIntStatus);
			goto out;
			
		}
		/* Read block from card */
		read_block_pio(&this->PCIRegP[0]->BufferDataPort, pBuff);

		/* Copy buffer to final location */
		buffer->writeBytes((i + offset) * 512, buff, 1 * 512);
	}
	
	// wait for transfer complete
	if (! waitIntStatus(XferComplete)) {
		IOLog("VoodooSDHCI: I/O timeout during completion... status == 0x%x\n", PCIRegP[0]->NormalIntStatus);
	}
	ret = kIOReturnSuccess;
out:
	return ret;
}

/*
 * sdma_access:  Read / write a set of blocks using multi-block reads in SDMA mode.
 *                       The host controller must be locked when this function is called.
 *		IOMemoryDescriptor *buffer:  Buffer operation class.  Defines
 *				read/write, address of operation, etc.
 *		UInt32 block:  Block offset to read/write
 *		UInt32 nblks:  Block count to read/write
 *      bool   read: true if read, false if write
 */
IOReturn VoodooSDHC::sdma_access(IOMemoryDescriptor *buffer,
					UInt32 block, UInt32 nblks, bool read) {
	IOReturn ret = kIOReturnError;
	UInt32 nis, offset = 0;
	AbsoluteTime deadline;

#ifdef __DEBUG__
IOLog("VoodooSDHCI readBlockMulti_sdma:  block = %d, nblks = %d\n", block, nblks);
#endif /* __DEBUG__ */
#ifndef NO_RESET_WAR	
	// Reset card before every operation.  The Linux driver
	// does this for this host controller. Not sure why.
	Reset(0, CMD_RESET);
	Reset(0, DAT_RESET);
#endif

	/* write: fill in data */
	if (! read) {
		buffer->readBytes(offset * 512, virtSdmaBuff, min(SDMA_BUFFER_SIZE, nblks * 512));
		offset += min(SDMA_BUFFER_SIZE / 512, nblks);
	}
	
	/* Set maximum timeout value */
	this->PCIRegP[0]->TimeoutControl = 0xe; // 2^27clks / 50MHz = 2.7 seconds

	/* Enable all interrupts */
	this->PCIRegP[0]->NormalIntSignalEn = 0x01ff;
	this->PCIRegP[0]->NormalIntStatusEn = -1;
	this->PCIRegP[0]->ErrorIntSignalEn = 0x01ff;
	this->PCIRegP[0]->ErrorIntStatusEn = -1;

	/* Clear pending interrupts */
	this->PCIRegP[0]->NormalIntStatus = 
			(BuffReadReady | XferComplete | CmdComplete);
	this->PCIRegP[0]->ErrorIntStatus = 0xf3ff;

	::OSSynchronizeIO();
	PCIRegP[0]->SDMASysAddr = physSdmaBuff;
	::OSSynchronizeIO();
	this->PCIRegP[0]->BlockSize = 512 | SDMA_BUFFER_SIZE_IN_REG;
	this->PCIRegP[0]->BlockCount = nblks;
	::OSSynchronizeIO();
	
	// Issue read command to host controller
	SDCommand(0,
		read ? SD_READ_MULTIPLE_BLOCK : SD_WRITE_MULTIPLE_BLOCK,
		SDCR18,
		isHighCapacity ? block : block * 512);
	::OSSynchronizeIO();
	
	// wait for CmdComplete
	if (! waitIntStatus(CmdComplete)) {
		IOLog("VoodooSDHCI: I/O error after command %d (SDMA): Status: 0x%x, Error: 0x%x\n",
			read ? SD_READ_MULTIPLE_BLOCK : SD_WRITE_MULTIPLE_BLOCK, PCIRegP[0]->NormalIntStatus, PCIRegP[0]->ErrorIntStatus);
		Reset(0, FULL_RESET);
		if (! cardInit(0)) {
			IOLog("VoodooSDHCI: reset failed, disabling access\n");
			cardPresence = kCardRemount;
		}
		ret = kIOReturnTimeout;
		goto out;
	}
	// check response
	if (PCIRegP[0]->Response[0] & (read ? 0xcff80000 : 0xeff80000)) {
		IOLog("VoodooSDHCI: Unexpected response from command %d (SDMA): Response: 0x%x\n",
			read ? SD_READ_MULTIPLE_BLOCK : SD_WRITE_MULTIPLE_BLOCK, PCIRegP[0]->Response[0]);
		goto out;
	}
	
	clock_interval_to_deadline(5000, kMillisecondScale, (uint64_t*)&deadline);
	IOLockLock(sdmaCond);
	while ((PCIRegP[0]->NormalIntStatus & ErrorInterrupt) == 0) {
		if (IOLockSleepDeadline(sdmaCond, sdmaCond, deadline, THREAD_UNINT) == THREAD_TIMED_OUT) {
			IOLockUnlock(sdmaCond);
			// timeout
			IOLog("VoodooSDHCI: I/O timeout during SDMA transfer: Status: 0x%x, Error: 0x%x, Block: %d, Offset: %d, Blocks: %d\n",
				PCIRegP[0]->NormalIntStatus, PCIRegP[0]->ErrorIntStatus, (int)block, (int)offset, (int)nblks);
			ret = kIOReturnTimeout;
			goto out;
		}
		nis = PCIRegP[0]->NormalIntStatus;
		if (nis & XferComplete) {
			IOLockUnlock(sdmaCond);
			if (read) {
				buffer->writeBytes(offset * 512, virtSdmaBuff, nblks * 512);
			}
			PCIRegP[0]->NormalIntStatus = XferComplete | DMAInterrupt;
			ret = kIOReturnSuccess;
			goto out;
		} else if (nis & DMAInterrupt) {
			IOLockUnlock(sdmaCond);
			if (read) {
				buffer->writeBytes(offset * 512, virtSdmaBuff, SDMA_BUFFER_SIZE);
				offset += SDMA_BUFFER_SIZE / 512;
				nblks -= SDMA_BUFFER_SIZE / 512;
			} else {
				buffer->readBytes(offset * 512, virtSdmaBuff, min(SDMA_BUFFER_SIZE, (nblks - offset) * 512));
				offset += min(SDMA_BUFFER_SIZE / 512, nblks - offset);
			}
			IOLockLock(sdmaCond);
			PCIRegP[0]->NormalIntStatus = DMAInterrupt;
			::OSSynchronizeIO();
			PCIRegP[0]->SDMASysAddr = physSdmaBuff;
		}
	}
	IOLockUnlock(sdmaCond);
	// error
	IOLog("VoodooSDHCI: I/O error during SDMA transfer: Status: 0x%x, Error: 0x%x, Block: %d, Offset: %d, Blocks: %d\n",
		PCIRegP[0]->NormalIntStatus, PCIRegP[0]->ErrorIntStatus, (int)block, (int)offset, (int)nblks);
	goto out;

	ret = kIOReturnSuccess;
out:
	PCIRegP[0]->NormalIntSignalEn = 0;
	PCIRegP[0]->ErrorIntSignalEn = 0;
	return ret;
}		
	

	
/*
 * readBlockSingle_pio:  Read a single block from the card using PIO not DMA
 *			 mode.  The host controller must be locked when this
 *			 function is called.
 *		UInt8 *buff:  Temporary buffer for data
 *		UInt32 block:  Block offset to read/write
 */
IOReturn VoodooSDHC::readBlockSingle_pio(UInt8 *buff, UInt32 block) {
	UInt32 *pBuff;
	int cnt, pass;
	IOReturn ret;

#ifndef NO_RESET_WAR	
	// Reset card before every operation.  The Linux driver does this for
	// this host controller. Not sure why
	Reset(0, CMD_RESET);
	Reset(0, DAT_RESET);
#endif /* NO_RESET_WAR */

	pBuff = (UInt32*)buff;

	/* Set transfer mode to single block */
	this->PCIRegP[0]->TransferMode = BIT4;

	/* Enable interrupt flags */
	this->PCIRegP[0]->NormalIntStatusEn = -1;
	this->PCIRegP[0]->ErrorIntStatusEn = -1;

	/* Clear pending interrupts */
	this->PCIRegP[0]->NormalIntStatus = 
			(BuffReadReady | XferComplete | CmdComplete);

	/* Set maximum timeout value */
	this->PCIRegP[0]->TimeoutControl = 0xe;

#ifdef __DEBUG__
	IOLog("VoodooSDHCI Int Status 0x%x Timeout = 0x%x\n",
		*(volatile UInt32 *)&(this->PCIRegP[0]->NormalIntStatus), 
		this->PCIRegP[0]->TimeoutControl);

	//IOLog("VoodooSDHCI:  state1 = 0x%x response = 0x%x\n",
	//	this->PCIRegP[0]->PresentState, this->PCIRegP[0]->Response[0]);
#endif /* __DEBUG__ */

	*(volatile UInt32 *)&(this->PCIRegP[0]->NormalIntStatus) =
		*(volatile UInt32 *)&(this->PCIRegP[0]->NormalIntStatus);

	SDCommand(0, SD_READ_SINGLE_BLOCK, SDCR17, isHighCapacity ? block : block * 512);

	cnt = pass = 0;

	//IOLog("VoodooSDHCI:  state2 = 0x%x response = 0x%x\n",
	//	this->PCIRegP[0]->PresentState, this->PCIRegP[0]->Response[0]);

	while((this->PCIRegP[0]->NormalIntStatus & BuffReadReady) !=
							BuffReadReady) {
		if (this->PCIRegP[0]->NormalIntStatus & 0x8000) {
			IOLog("VoodooSDHCI: S Returning error:  0x%x\n", *(volatile UInt32 *) & (this->PCIRegP[0]->NormalIntStatus));
			ret = kIOReturnError;
			goto out;
		}
		cnt++;
		if (cnt > 100000) {
			cnt = 0;
			pass++;
			IOLog("VoodooSDHCI:  Stuck in while loop 1: pass = %d"
				" status = 0x%x state = 0x%x\n", pass, 
				*(volatile UInt32 *)&(this->PCIRegP[0]->
					NormalIntStatus),
				this->PCIRegP[0]->PresentState);
			if (pass > 10) {
				ret = kIOReturnError;
				goto out;
			}
		}
			::IODelay(10);
	}

	/* Read block from card */
	read_block_pio(&this->PCIRegP[0]->BufferDataPort, pBuff);

	ret = kIOReturnSuccess;

out:
	this->PCIRegP[0]->NormalIntStatus =
				(BuffReadReady|XferComplete|CmdComplete);
	return ret;
}

/*
 * writeBlockMulti_pio:  Write multiple blocks to the card using PIO not DMA
 *			 mode.  The host controller must be locked when this
 *			 function is called.
 *		IOMemoryDescriptor *buffer:  Buffer operation class.  Defines
 *				read/write, address of operation, etc.
 *		UInt32 block:  Block offset to read/write
 *		UInt32 nblks:  Number of blocks to read/write
 *		UInt32 offset:  Offset from beginning of transfer - where in
 *				final buffer we should begin placing data
 */
IOReturn VoodooSDHC::writeBlockMulti_pio(IOMemoryDescriptor *buffer,
				UInt32 block, UInt32 nblks, UInt32 offset) {
	UInt8 buff[512];	// Temporary storage for data block
	int cnt, pass;
	UInt32 *pBuff;
	IOReturn ret;

#ifndef NO_RESET_WAR
	// Reset card before every operation.  The Linux driver does this
	// for this host controller.  Not sure why.
	Reset(0, CMD_RESET);
	Reset(0, DAT_RESET);
#endif
	
	this->PCIRegP[0]->NormalIntStatusEn = -1;
	this->PCIRegP[0]->ErrorIntStatusEn = -1;
	this->PCIRegP[0]->NormalIntStatus = 
			BuffWriteReady | XferComplete | CmdComplete;
	this->PCIRegP[0]->TimeoutControl = 0xe;

	this->PCIRegP[0]->BlockSize = 512;
	this->PCIRegP[0]->BlockCount = nblks;

	SDCommand(0, SD_APP_CMD, SDCR55, this->RCA << 16);
	SDCommand(0, SD_APP_SET_WR_BLK_ERASE_COUNT, SDCR23, nblks);
	SDCommand(0, SD_WRITE_MULTIPLE_BLOCK, SDCR24, isHighCapacity ? block : block * 512);

	for (int i = 0; i < nblks; i++) {
		buffer->readBytes((offset + i) * 512, buff, 1 * 512);

		pBuff = (UInt32*)buff;

		cnt = pass = 0;
                while(!(this->PCIRegP[0]->PresentState &
                                                      SDHCI_SPACE_AVAILABLE)) {
			cnt++;

			if (this->PCIRegP[0]->NormalIntStatus & 0x8000) {
				IOLog("VoodooSDHCI 2 Returning error:  0x%x\n",
			      		*(volatile UInt32 *)
			      		&(this->PCIRegP[0]->NormalIntStatus));
				ret = kIOReturnError;
				goto out;
			}
			if (cnt > 100000) {
				cnt = 0;
				pass++;
				IOLog("VoodooSDHCI:  Stuck in while loop 2: pass = %d"
			      		" status = 0x%x\n", pass,
			      		this->PCIRegP[0]->NormalIntStatus);
				if (pass > 10) {
					ret = kIOReturnError;
					goto out;
				}
			}	
			::IODelay(10);
		}

                this->PCIRegP[0]->NormalIntStatus =
                                this->PCIRegP[0]->NormalIntStatus;

		for (int j = 0; j < 128; j++) {
			this->PCIRegP[0]->BufferDataPort = *pBuff;
			pBuff++;			
		}

	}

	cnt = pass = 0;
	while ((this->PCIRegP[0]->NormalIntStatus & XferComplete) !=
	       					XferComplete) {
		cnt++;
		if (this->PCIRegP[0]->NormalIntStatus & 0x8000) {
			IOLog("VoodooSDHCI 3 Returning error:  0x%x\n",
		      		*(volatile UInt32 *)
		      		&(this->PCIRegP[0]->NormalIntStatus));
			ret = kIOReturnError;
			goto out;
		}

		if (cnt > 100000) {
			cnt = 0;
			pass++;
			IOLog("VoodooSDHCI:  Stuck in while loop 3: pass = %d"
		      		" status = 0x%x\n", pass,
		      		this->PCIRegP[0]->NormalIntStatus);
			if (pass > 10) {
				ret = kIOReturnError;
				goto out;
			}
		}
	}
	this->PCIRegP[0]->NormalIntStatus =
				BuffWriteReady | XferComplete | CmdComplete;	
	ret = kIOReturnSuccess;

out:
	return ret;
}

/*
 * writeBlockSingle_pio:  Read a single block from the card using PIO not DMA
 *			  mode.  The host controller must be locked when this
 *			  function is called.
 *		IOMemoryDescriptor *buffer:  Buffer operation class.  Defines
 *				read/write, address of operation, etc.
 *		UInt32 block:  Block offset to read/write
 *		UInt32 offset:  Offset from beginning of transfer - where in
 *				final buffer we should begin placing data
 */
IOReturn VoodooSDHC::writeBlockSingle_pio(IOMemoryDescriptor *buffer,
			UInt32 block, UInt32 offset) {
	UInt8 buff[512];	// Temporary storage for data block
	int cnt, pass;
	UInt32 *pBuff;
	IOReturn ret;

	pBuff = (UInt32*)buff;

	buffer->readBytes(offset * 512, buff, 1 * 512);

#ifndef NO_RESET_WAR
	// Reset card before every operation.  The Linux driver does this
	// for this host controller.  Not sure why.
	Reset(0, CMD_RESET);
	Reset(0, DAT_RESET);
#endif

	this->PCIRegP[0]->TransferMode = 0;
	this->PCIRegP[0]->NormalIntStatusEn = -1;
	this->PCIRegP[0]->ErrorIntStatusEn = -1;
	this->PCIRegP[0]->NormalIntStatus = 
				BuffWriteReady | XferComplete | CmdComplete;
	this->PCIRegP[0]->TimeoutControl = 0xe;

	SDCommand(0, SD_WRITE_BLOCK, SDCR24, isHighCapacity ? block : block * 512);

	cnt = pass = 0;
	while ((this->PCIRegP[0]->NormalIntStatus & BuffWriteReady) !=
							BuffWriteReady) {
		cnt++;

		if (this->PCIRegP[0]->NormalIntStatus & 0x8000) {
			IOLog("VoodooSDHCI: 2 Returning error:  0x%x\n",
				*(volatile UInt32 *)
					&(this->PCIRegP[0]->NormalIntStatus));
			ret = kIOReturnError;
			goto out;
		}

		if (cnt > 100000) {
			cnt = 0;
			pass++;
			IOLog("VoodooSDHCI:  Stuck in while loop 2: pass = %d"
				" status = 0x%x\n", pass,
				this->PCIRegP[0]->NormalIntStatus);
			if (pass > 10) {
				ret = kIOReturnError;
				goto out;
			}
		}				
	}

	for (int j = 0; j < 128; j++) {
		this->PCIRegP[0]->BufferDataPort = *pBuff;
		pBuff++;			
	}

	cnt = pass = 0;
	while ((this->PCIRegP[0]->NormalIntStatus & XferComplete) !=
							XferComplete) {
		cnt++;

		if (this->PCIRegP[0]->NormalIntStatus & 0x8000) {
			IOLog("VoodooSDHCI 3 Returning error:  0x%x\n",
				*(volatile UInt32 *)
					&(this->PCIRegP[0]->NormalIntStatus));
			ret = kIOReturnError;
			goto out;
		}

		if (cnt > 100000) {
			cnt = 0;
			pass++;
			IOLog("VoodooSDHCI:  stuck in while loop #3: pass = %d"
				" status = 0x%x\n", pass,
				this->PCIRegP[0]->NormalIntStatus);
			if (pass > 10) {
				ret = kIOReturnError;
				goto out;
			}
		}
	}
	this->PCIRegP[0]->NormalIntStatus =
			BuffWriteReady | XferComplete | CmdComplete;	
	ret = kIOReturnSuccess;

out:
	return ret;
}

/*
 * doAsyncReadWrite:  Guts of the driver.  Perform reads and writes.  This
 *		      function must be reentrant.  Further, the completion
 *		      action may result in another call to this function.
 *		      This function may block.
 *		      Returns success or failure.
 *		IOMemoryDescriptor *buffer:  Buffer operation class.  Defines
 *				read/write, address of operation, etc.
 *		UInt32 block:  Block offset to read/write
 *
 *		IOStorageCompletion completion:  Action to perform upon
 *				completion of operation
 */
#ifdef __LP64__
IOReturn VoodooSDHC::doAsyncReadWrite(IOMemoryDescriptor *buffer,
									  UInt64 block, UInt64 nblks,
									  IOStorageAttributes *attributes,
									  IOStorageCompletion *completion) {
	UInt8 buff[512];	// Temporary storage for data block
	int ret = 0;
	int locked;
	UInt64 blk, n;
	
	// All access to the card must be done while this lock is held
	lock.lock();
	locked = 1;
	
	if (cardPresence != kCardIsPresent || ! isCardPresent(0)) {
		ret = kIOReturnNoMedia;
		goto out;
	}
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: in doAsyncReadWrite function :: block == %d, nblks == %d, ", block, nblks);
#endif
	if (buffer->getDirection() == kIODirectionIn) {
		/* Read from Card */
#ifdef __DEBUG__
		IOLog("READING FROM CARD!\n");
#endif
		blk = block;
		n = nblks;
		while (n) {
			if (USE_SDMA) {
				int i;
				for (i = 0; i < SDMA_RETRY_COUNT; i++)
					if ((ret = sdma_access(buffer, block, nblks, true)) != kIOReturnTimeout)
						break;
				if (i != 0)
					IOLog("VoodooSDHCI: retry succeeded\n");
				n = 0;
			} else if ((nblks > 1) && USE_MULTIBLOCK) {
				int b = MIN(2048 /* should fit in sdma buff */, n);
				
				ret = readBlockMulti_pio(buffer, blk, b,
										 blk - block);
				n -= b;
				blk += b;
			} else {
				ret = readBlockSingle_pio(buff, blk);
				buffer->writeBytes((blk - block) * 512,
								   buff, 1 * 512);
				n--;
				blk++;
			}
			if (ret != kIOReturnSuccess) goto out;
#ifdef __DEBUG__
			IOLog("VoodooSDHCI:  ret = 0x%x block = %d\n", ret, blk);;
#endif /* __DEBUG__ */
		}
	} else {
		/* Write to Card */
#ifdef __DEBUG__
		IOLog("WRITING TO CARD!\n");
#endif
		
#ifdef READONLY_DRIVER
		// When compiled in this mode, the driver fails all write
		// operations.  Useful for testing to gain confidence in
		// code without trashing data.  Define must be set at top
		// of file.
		ret = kIOReturnError;
		goto out;
#endif
		blk = block;
		n = nblks;
		while (n) {
			if (USE_SDMA) {
				int i;
				for (i = 0; i < SDMA_RETRY_COUNT; i++)
					if ((ret = sdma_access(buffer, block, nblks, false)) != kIOReturnTimeout)
						break;
				if (i != 0)
					IOLog("VoodooSDHCI: retry succeeded\n");
				n = 0;
			} 
			else if ((nblks > 1) && USE_MULTIBLOCK) {
				int b = MIN(2048, n);
				ret = writeBlockMulti_pio(buffer, blk, b,
										  blk - block);
				n -= b;
				blk += b;
			} else {
				ret = writeBlockSingle_pio(buffer, blk,
										   blk - block);
				n--;
				blk++;
			}
			if (ret != kIOReturnSuccess) goto out;
		}
		
	}
	
	locked = 0;
	lock.unlock();
	
	if(completion->action) {
		(completion->action)(completion->target, completion->parameter, kIOReturnSuccess, nblks * 512);
	} else {
		IOLog("VoodooSDHCI ERROR!\n");
		ret = kIOReturnError;
		goto out;
	}
	ret = kIOReturnSuccess;
	
out:
	switch (ret) {
		case kIOReturnSuccess:
			break;
		case kIOReturnNoMedia:
			/* require remount */
			cardPresence = kCardRemount;
			IOLog("VoodooSDHCI: media not present, require remount\n");
			break;
	}
	if (locked)
		lock.unlock();
	//	in_read_write = 0;
	return ret;
}
#else /* !__LP64__ */
IOReturn VoodooSDHC::doAsyncReadWrite(IOMemoryDescriptor *buffer,
		UInt32 block, UInt32 nblks, IOStorageCompletion completion) {
	UInt8 buff[512];	// Temporary storage for data block
	int ret = 0;
	int locked;
	UInt32 blk, n;
	
	// All access to the card must be done while this lock is held
	lock.lock();
	locked = 1;

	if (cardPresence != kCardIsPresent || ! isCardPresent(0)) {
		ret = kIOReturnNoMedia;
		goto out;
	}
#ifdef __DEBUG__
	IOLog("VoodooSDHCI: in doAsyncReadWrite function :: block == %d, nblks == %d, ", block, nblks);
#endif
	if (buffer->getDirection() == kIODirectionIn) {
		/* Read from Card */
#ifdef __DEBUG__
		IOLog("READING FROM CARD!\n");
#endif
		blk = block;
		n = nblks;
		while (n) {
			if (USE_SDMA) {
				int i;
				for (i = 0; i < SDMA_RETRY_COUNT; i++)
					if ((ret = sdma_access(buffer, block, nblks, true)) != kIOReturnTimeout)
						break;
				if (i != 0)
					IOLog("VoodooSDHCI: retry succeeded\n");
				n = 0;
			} else if ((nblks > 1) && USE_MULTIBLOCK) {
				int b = MIN(2048 /* should fit in sdma buff */, n);
				
				ret = readBlockMulti_pio(buffer, blk, b,
					blk - block);
				n -= b;
				blk += b;
			} else {
				ret = readBlockSingle_pio(buff, blk);
				buffer->writeBytes((blk - block) * 512,
								buff, 1 * 512);
				n--;
				blk++;
			}
			if (ret != kIOReturnSuccess) goto out;
#ifdef __DEBUG__
			IOLog("VoodooSDHCI:  ret = 0x%x block = %d\n", ret, blk);;
#endif /* __DEBUG__ */
		}
	} else {
		/* Write to Card */
#ifdef __DEBUG__
		IOLog("WRITING TO CARD!\n");
#endif
		
#ifdef READONLY_DRIVER
		// When compiled in this mode, the driver fails all write
		// operations.  Useful for testing to gain confidence in
		// code without trashing data.  Define must be set at top
		// of file.
		ret = kIOReturnError;
		goto out;
#endif
		blk = block;
		n = nblks;
		while (n) {
			if (USE_SDMA) {
				int i;
				for (i = 0; i < SDMA_RETRY_COUNT; i++)
					if ((ret = sdma_access(buffer, block, nblks, false)) != kIOReturnTimeout)
						break;
				if (i != 0)
					IOLog("VoodooSDHCI: retry succeeded\n");
				n = 0;
			} 
			else if ((nblks > 1) && USE_MULTIBLOCK) {
				int b = MIN(2048, n);
				ret = writeBlockMulti_pio(buffer, blk, b,
						blk - block);
				n -= b;
				blk += b;
			} else {
				ret = writeBlockSingle_pio(buffer, blk,
						blk - block);
				n--;
				blk++;
			}
			if (ret != kIOReturnSuccess) goto out;
		}

	}
	
	locked = 0;
	lock.unlock();

	if(completion.action) {
		(*completion.action)(completion.target, completion.parameter, kIOReturnSuccess, nblks * 512);
	} else {
		IOLog("VoodooSDHCI ERROR!\n");
		ret = kIOReturnError;
		goto out;
	}
	ret = kIOReturnSuccess;
	
out:
	switch (ret) {
	case kIOReturnSuccess:
		break;
	case kIOReturnNoMedia:
		/* require remount */
		cardPresence = kCardRemount;
		IOLog("VoodooSDHCI: media not present, require remount\n");
		break;
	}
	if (locked)
		lock.unlock();
//	in_read_write = 0;
	return ret;
}
#endif /* !__LP64__ */


void VoodooSDHC::handleInterrupt()
{
	IOLockLock(sdmaCond);
	IOLockWakeup(sdmaCond, sdmaCond, true);
	IOLockUnlock(sdmaCond);
}

void VoodooSDHC::handleTimer()
{
	bool mediaPresent, changedState;
	reportMediaState(&mediaPresent, &changedState);
	if (changedState)
		messageClients(
			kIOMessageMediaStateHasChanged,
			(void*)(mediaPresent ? kIOMediaStateOnline: kIOMediaStateOffline),
			0);
	timerSrc->setTimeoutMS(1000);
}

void VoodooSDHC::interruptHandler(OSObject *owner, IOInterruptEventSource *, int)
{
	VoodooSDHC *self = static_cast<VoodooSDHC*>(owner);
	self->handleInterrupt();
}

bool VoodooSDHC::interruptFilter(OSObject *owner, IOFilterInterruptEventSource *)
{
	if (OSDynamicCast(VoodooSDHC, owner)) {
		return true;
	}
	return false;
}

void VoodooSDHC::timerHandler(OSObject *owner, IOTimerEventSource *)
{
	VoodooSDHC *self = static_cast<VoodooSDHC*>(owner);
	self->handleTimer();
}
