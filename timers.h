#ifndef TIMERS_H
#define TIMERS_H

#include <stdint.h>
#include "sdk_errors.h"

ret_code_t timers_app_init(void);
ret_code_t timers_start_cycle(void);

#endif // TIMERS_H