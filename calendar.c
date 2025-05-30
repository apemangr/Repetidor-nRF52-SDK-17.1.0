#include "calendar.h"
#include "nrf_drv_clock.h"
#include <string.h> // For memcpy


static volatile bool m_tick_flag = false;
static volatile bool m_initialized = false;
static datetime_t  m_time;                          

static inline bool is_leap_year(uint16_t year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static const uint8_t days_in_month[12] = {
    31, 28, 31, 30, 31, 30,
    31, 31, 30, 31, 30, 31
};

void calendar_rtc_handler(void)
{
    m_tick_flag = true;
	uint32_t cnt = nrfx_rtc_counter_get(&m_rtc);
	nrfx_rtc_cc_set(&m_rtc, RTC_CHANNEL, (cnt + 8) & RTC_COUNTER_COUNTER_Msk, true);
}

bool calendar_init(void)
{
    if (m_initialized) {
        return false; // Already initialized
    }

    nrfx_rtc_cc_set(&m_rtc, RTC_CHANNEL, 8, true);
    //nrfx_rtc_int_enable(&m_rtc, NRF_RTC_INT_COMPARE2_MASK);
    //nrfx_rtc_int_enable(&m_rtc, (uint32_t)NRF_RTC_INT_COMPARE2_MASK << RTC_CHANNEL);

    // Clear internal time
    memset(&m_time, 0, sizeof(m_time));
    m_initialized = true;

    return true;
}

bool calendar_set_time(const datetime_t * now)
{
    if (!m_initialized || now == NULL) {
        return false;
    }
    // Validate fields
    uint8_t max_day = (now->month == 2)
                      ? (is_leap_year(now->year) ? 29 : 28)
                      : days_in_month[now->month - 1];
    if (now->month < 1 || now->month > 12 ||
        now->day < 1   || now->day   > max_day ||
        now->hour > 23 || now->minute > 59 || now->second > 59) {
        return false;
    }

    // Disable RTC to safely update
    nrfx_rtc_disable(&m_rtc);
    memcpy(&m_time, now, sizeof(m_time));
    nrfx_rtc_counter_clear(&m_rtc);
    nrfx_rtc_cc_set(&m_rtc, RTC_CHANNEL, 1, true);
    nrfx_rtc_enable(&m_rtc);

    return true;
}

bool calendar_get_time(datetime_t * now)
{
    if (!m_initialized || now == NULL) {
        return false;
    }
    memcpy(now, &m_time, sizeof(m_time));
    return true;
}

void calendar_update(void)
{
    if (!m_initialized || !m_tick_flag) {
        return;
    }
    m_tick_flag = false;

    // Increment seconds
    if (++m_time.second > 59) {
        m_time.second = 0;
        if (++m_time.minute > 59) {
            m_time.minute = 0;
            if (++m_time.hour > 23) {
                m_time.hour = 0;
                // Day rollover
                uint8_t dim = (m_time.month == 2)
                              ? (is_leap_year(m_time.year) ? 29 : 28)
                              : days_in_month[m_time.month - 1];
                if (++m_time.day > dim) {
                    m_time.day = 1;
                    if (++m_time.month > 12) {
                        m_time.month = 1;
                        m_time.year++;
                    }
                }
            }
        }
    }
}

