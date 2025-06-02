#ifndef CALENDAR_H
#define CALENDAR_H

#include <stdbool.h>
#include <stdint.h>

#include "app_error.h"
#include "nrfx_rtc.h"

#define RTC_CHANNEL 2

typedef struct
{
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
} datetime_t;

extern nrfx_rtc_t m_rtc;

bool calendar_init(void);

bool calendar_set_time(const datetime_t* now);

bool calendar_get_time(datetime_t* now);

void calendar_update(void);

void calendar_rtc_handler(void);

#endif // CALENDAR_H