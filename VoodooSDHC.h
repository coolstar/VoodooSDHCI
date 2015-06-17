#ifndef _VoodooSDHC_H_
#define _VoodooSDHC_H_
#include "License.h"
#define SLOT1 0
#define SLOT2 1
#define SLOT3 2
#define SLOT4 3
#define SLOT5 4
#define SLOT6 5

/* General IOKit includes */
#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOService.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/locks.h>
#include "SD_DataTypes.h"

class VoodooSDHC : public IOBlockStorageDevice
{
	
	OSDeclareDefaultStructors ( VoodooSDHC )
	
protected:

public:
	
	// Overrides from IOService
	
	virtual bool	init 	( OSDictionary * propertyTable );
	virtual void	free	();
	virtual bool 	start 	( IOService * provider );
	virtual void 	stop 	( IOService * provider );

private:
	// This lock protects block I/O access to the card reader
	class Lock {
		IOLock	*mutex_;
		bool	locked_;
		int		waiting_;
	public:
		void init() {
			mutex_ = IOLockAlloc();
			locked_ = false;
			waiting_ = 0;
		}
		void free() {
			IOLockFree(mutex_);
		}
		void lock() {
			IOLockLock(mutex_);
			if (locked_) {
				waiting_++;
				do {
					IOLockSleep(mutex_, mutex_, THREAD_UNINT);
				} while (locked_);
				waiting_--;
			}
			locked_ = true;
			IOLockUnlock(mutex_);
		}
		void unlock() {
			IOLockLock(mutex_);
			locked_ = false;
			if (waiting_)
				IOLockWakeup(mutex_, mutex_, true);
			IOLockUnlock(mutex_);
		}
	} lock;
	
#ifdef USE_SDMA
	IOLock			*sdmaCond; // this lock handles I/O interrupt
	IOLock			*mediaStateLock; // this lock serializes reportMediaState
	IOBufferMemoryDescriptor *sdmaBuffDesc;
	UInt32			physSdmaBuff;
	void			*virtSdmaBuff;
	IOWorkLoop		*workLoop;
	IOFilterInterruptEventSource *interruptSrc;
	IOTimerEventSource	*timerSrc;
	virtual IOWorkLoop *getWorkLoop() const { return workLoop; }
#endif
	
	IOMemoryMap		*PCIRegMap;
	struct			SDHCIRegMap_t *PCIRegP[6];
	struct			SDCIDReg_t SDCIDReg[6];
	UInt32			RCA;
	UInt32			maxBlock;
	enum {
		kCardNotPresent,
		kCardIsPresent,
		kCardRemount
	} cardPresence;
	bool			isHighCapacity;
	
	bool			setup(IOService *provider);
	void			dumpRegs(UInt8 slot);
	bool			isCardPresent(UInt8 slot);
	bool			isCardWP(UInt8 slot);
	bool			cardInit( UInt8 slot );
	void			LEDControl(UInt8 slot, bool state);
	void			Reset( UInt8 slot, UInt8 type );
	bool			SDCommand( UInt8 slot, UInt8 command, UInt16 response, UInt32 arg);
	bool			calcClock(UInt8 slot, UInt32 clockspeed);
	bool			powerSD(UInt8 slot);
	void			parseCID(UInt8 slot);
	void			parseCSD(UInt8 slot);
	
//	IOReturn		requestIdle(void); /* 10.6.0 */
//	IOReturn		doDiscard(UInt64 block, UInt64 nblks); /* 10.6.0 */
	IOReturn		reportRemovability(bool *isRemovable);
	IOReturn		reportWriteProtection(bool *isWriteProtected);
	IOReturn		setWriteCacheState(bool enabled);
	IOReturn		reportPollRequirements(bool *pollRequired, bool *pollIsExpensive);
	IOReturn		reportMediaState(bool *mediaPresent, bool *changedState);
	IOReturn		reportMaxValidBlock(UInt64 *maxBlock);
	IOReturn		reportLockability(bool *isLockable);
	IOReturn		reportEjectability(bool *isEjectable);
	IOReturn		reportBlockSize(UInt64 *blockSize);
	IOReturn		getWriteCacheState(bool *enabled);
	IOReturn		setPowerState( unsigned long whichState, IOService * whatDevice );
	char *			getVendorString(void);
	char *			getRevisionString(void);
	char *			getProductString(void);
	char *			getAdditionalDeviceInfoString(void);
	IOReturn		doSynchronizeCache(void);
	IOReturn		doLockUnlockMedia(bool doLock);
	UInt32			doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const;
	IOReturn		doFormatMedia(UInt64 byteCapacity);
	IOReturn		doEjectMedia(void);
#ifdef __LP64__
    virtual IOReturn	doAsyncReadWrite(IOMemoryDescriptor *buffer,
										 UInt64 block, UInt64 nblks,
										 IOStorageAttributes *attributes,
										 IOStorageCompletion *completion);
#endif
#ifndef __LP64__
	IOReturn		doAsyncReadWrite(IOMemoryDescriptor *buffer, 
									 UInt32 block, UInt32 nblks,
									 IOStorageCompletion completion); //completion was start
	IOReturn		reportMaxWriteTransfer(UInt64 blockSize, UInt64 *max);
	IOReturn		reportMaxReadTransfer (UInt64 blockSize, UInt64 *max);
#endif
	IOReturn		sdma_access(IOMemoryDescriptor *buffer, UInt32 block, UInt32 nblks, bool read);
	IOReturn		readBlockMulti_pio(IOMemoryDescriptor *buffer, UInt32 block, UInt32 nblks,
							UInt32 offset);
	IOReturn		readBlockSingle_pio(UInt8 *buff, UInt32 block);
	IOReturn		writeBlockMulti_pio(IOMemoryDescriptor *buffer, UInt32 block, UInt32 nblks,
							UInt32 offset);
	IOReturn		writeBlockSingle_pio(IOMemoryDescriptor *buffer, UInt32 block,
							UInt32 offset);
	bool			waitIntStatus(UInt32 maskBits);
	void			handleInterrupt();
	void			handleTimer();
	
	static void interruptHandler(OSObject *owner, IOInterruptEventSource *source, int count);
	static bool interruptFilter(OSObject *owner, IOFilterInterruptEventSource *source);
	static void timerHandler(OSObject *owner, IOTimerEventSource *sender);
};

#endif /* _VoodooSDHC_H_ */
