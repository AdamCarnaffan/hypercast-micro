#ifndef __HC_LIBRARY_H__
#define __HC_LIBRARY_H__

#define HC_FIXED_TIME_MIN_VALUE (uint64_t)1640000000

uint64_t get_epoch();
int set_epoch(uint64_t);


#endif

#ifndef TAG
#define TAG "HC_LIB"
#endif