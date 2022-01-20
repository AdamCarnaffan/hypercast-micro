#ifndef __HC_MEASURE_H__
#define __HC_MEASURE_H__

#include "hypercast.h"


#define SEND_MEASURES 1
#define MAX_HTTP_OUTPUT_BUFFER 1024

void log_nodestate(hypercast_t*);

#endif

#ifndef TAG
#define TAG "HC_MEASURE"
#endif