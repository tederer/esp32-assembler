#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"


   .global returnValue
returnValue:
   .long 0

   .global entry
entry:

nop                                                 
reg_wr     0, 15,  0, 0
reg_wr 0x3ff, 31,  0, 0x0f
reg_wr 0x3ff,  0, 31, 0xf0
nop                                                 

isReadyForWakeup:									   // Read RTC_CNTL_RDY_FOR_WAKEUP bit
	READ_RTC_FIELD(RTC_CNTL_LOW_POWER_ST_REG, RTC_CNTL_RDY_FOR_WAKEUP)
	and r0, r0, 1
	jump isReadyForWakeup, eq    					// Retry until the bit is set
	wake                          				// Trigger wake up

