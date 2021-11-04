#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"

               .global returnValue
returnValue:   .long 0

               .global entry
entry:

nop

reg_rd 0x30, 0x13, 0x13  // wait until RTC_CNTL_RDY_FOR_WAKEUP is set befor calling wake
and r0, r0, 1
jumpr -8, 1, lt
wake