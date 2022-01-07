
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>

#include "hc_lib.h"

uint64_t get_epoch() {
    return HC_FIXED_TIME_MIN_VALUE + (long)time(NULL);
}

// TODO: Function should return int result of settimeofday
void set_epoch(uint64_t epoch) {
    struct timeval now;
    now.tv_sec = epoch - HC_FIXED_TIME_MIN_VALUE;
    now.tv_usec = 0;
    settimeofday(&now, NULL);
}