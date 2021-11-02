#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"


   .global returnValue
returnValue:
   .long 0

   .global entry
entry:

// same code as in line 19 - 23 but using relative jumps instead of absolute ones
reg_rd 0x30, 0x13, 0x13
jumpr -4, 0, lt        
jumpr -8, 1, ge        
wake                                                                 

isReadyForWakeup:									   // Read RTC_CNTL_RDY_FOR_WAKEUP bit
	READ_RTC_FIELD(RTC_CNTL_LOW_POWER_ST_REG, RTC_CNTL_RDY_FOR_WAKEUP)
	and r0, r0, 1
	jump isReadyForWakeup, eq    					// Retry until the bit is set
	wake                          				// Trigger wake up

