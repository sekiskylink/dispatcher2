#ifndef __REQUEST_PROCESSOR_H
#define __REQUEST_PROCESSOR_H

#include "dispatcher2.h"

void start_request_processor(dispatcher2conf_t conf, List *server_req_list);
void stop_request_processor(void);
#endif
