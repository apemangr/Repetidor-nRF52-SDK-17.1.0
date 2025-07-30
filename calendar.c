#include "calendar.h"
#include "filesystem.h"

static volatile bool m_tick_flag   = false;
static volatile bool m_initialized = false;
datetime_t           m_time        = {0};

void                 restart_sleep_rtc(void)
{
    uint32_t current_counter = nrfx_rtc_counter_get(&m_rtc);
    uint32_t sleep_time_from_flash =
        read_time_from_flash(TIEMPO_SLEEP, DEFAULT_DEVICE_SLEEP_TIME_MS);
    //NRF_LOG_RAW_INFO("\n\t>> Tiempo de sleep: %u ms", sleep_time_from_flash);
    uint32_t next_event =
        (current_counter + (sleep_time_from_flash / 1000) * 8) & 0xFFFFFF;
    nrfx_rtc_cc_set(&m_rtc, 1, next_event, true);
}

void restart_on_rtc(void)
{
    uint32_t current_counter = nrfx_rtc_counter_get(&m_rtc);
    uint32_t read_time =
        read_time_from_flash(TIEMPO_ENCENDIDO, DEFAULT_DEVICE_ON_TIME_MS);
    //NRF_LOG_RAW_INFO("\n\t>> Tiempo de encendido: %u ms", read_time);
    uint32_t next_event = (current_counter + (read_time / 1000) * 8) & 0xFFFFFF;
    nrfx_rtc_cc_set(&m_rtc, 0, next_event, true);
}

bool is_date_stored()
{
    ret_code_t        err_code;
    fds_record_desc_t record_desc;
    fds_find_token_t  ftok = {0};

    err_code               = fds_record_find(DATE_AND_TIME_FILE_ID, DATE_AND_TIME_RECORD_KEY,
                                             &record_desc, &ftok);

    if (err_code != NRF_SUCCESS)
    {
        return false;
    }

    return true;
}

static inline bool is_leap_year(uint16_t year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30,
                                          31, 31, 30, 31, 30, 31};

void                 calendar_rtc_handler(void)
{
    m_tick_flag  = true;
    uint32_t cnt = nrfx_rtc_counter_get(&m_rtc);
    nrfx_rtc_cc_set(&m_rtc, RTC_CHANNEL, (cnt + 8) & RTC_COUNTER_COUNTER_Msk,
                    true);
}

bool calendar_set_time(const datetime_t *now)
{
    if (!m_initialized || now == NULL)
    {
        return false;
    }
    // Validate fields
    uint8_t max_day = (now->month == 2) ? (is_leap_year(now->year) ? 29 : 28)
                                        : days_in_month[now->month - 1];
    if (now->month < 1 || now->month > 12 || now->day < 1 || now->day > max_day ||
        now->hour > 23 || now->minute > 59 || now->second > 59)
    {
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

bool calendar_init(void)
{

    NRF_LOG_RAW_INFO("\n\033[1;31m>\033[0m Iniciando modulo RTC...");
    ret_code_t err_code;

    if (m_initialized)
    {
        NRF_LOG_RAW_INFO("\n\t>> Error al inicializar modulo RTC");

        NRF_LOG_FLUSH();
        return false;
    }

    nrfx_rtc_cc_set(&m_rtc, RTC_CHANNEL, 8, true);

    memset(&m_time, 0, sizeof(m_time));

    m_initialized = true;

    NRF_LOG_RAW_INFO(
        "\n\t>> \033[0;32mModulo RTC inicializado correctamente.\033[0m");

    NRF_LOG_FLUSH();
    return true;
}

bool calendar_get_time(datetime_t *now)
{
    if (!m_initialized || now == NULL)
    {
        return false;
    }
    memcpy(now, &m_time, sizeof(m_time));
    return true;
}

void calendar_update(void)
{
    if (!m_initialized || !m_tick_flag)
    {
        return;
    }
    m_tick_flag = false;


    // Increment seconds
    if (++m_time.second > 59)
    {
        m_time.second = 0;

        if (++m_time.minute > 59)
        {
  
            m_time.minute = 0;

            if (++m_time.hour > 23)
            {
                m_time.hour = 0;
                // Day rollover
                uint8_t dim = (m_time.month == 2)
                                  ? (is_leap_year(m_time.year) ? 29 : 28)
                                  : days_in_month[m_time.month - 1];
                if (++m_time.day > dim)
                {
                    m_time.day = 1;
                    if (++m_time.month > 12)
                    {
                        m_time.month = 1;
                        m_time.year++;
                    }
                }
            }
        }
    }
}

bool calendar_set_datetime(void)
{
    NRF_LOG_RAW_INFO(
        "\n\t>> Tiempo de encendido\t: %d \t[segs]",
        read_time_from_flash(TIEMPO_ENCENDIDO, DEFAULT_DEVICE_ON_TIME_MS) / 1000);

    NRF_LOG_FLUSH();
    NRF_LOG_RAW_INFO(
        "\n\t>> Tiempo de dormido\t: %d \t[segs]",
        read_time_from_flash(TIEMPO_SLEEP, DEFAULT_DEVICE_SLEEP_TIME_MS) / 1000);

    NRF_LOG_FLUSH();

    if (is_date_stored() == true)
    {
        datetime_t dt       = read_date_from_flash();
        bool       err_code = calendar_set_time(&dt);

        if (err_code == true)
        {
            NRF_LOG_RAW_INFO("\n\t>> Fecha y hora cargada desde la memoria.");
            NRF_LOG_RAW_INFO("\n\t>> Fecha: %04u-%02u-%02u, Hora: %02u:%02u:%02u\n",
                             dt.year, dt.month, dt.day, dt.hour, dt.minute,
                             dt.second);

            NRF_LOG_FLUSH();
            return true;
        }
        else
        {
            NRF_LOG_RAW_INFO("\n\t>> Error al cargar fecha y hora.");
            NRF_LOG_FLUSH();

            NRF_LOG_RAW_INFO("\n\t>> Cargando valor predeterminado.");
            NRF_LOG_FLUSH();

            datetime_t now = {.year   = 2000,
                              .month  = 1,
                              .day    = 1,
                              .hour   = 0,
                              .minute = 0,
                              .second = 0};
            calendar_set_time(&now);
            NRF_LOG_INFO("\n\t>> Fecha: %04u-%02u-%02u, Hora: %02u:%02u:%02u\n",
                         now.year, now.month, now.day, now.hour, now.minute,
                         now.second);
            NRF_LOG_FLUSH();

            return false;
        }
    }
    else
    {
        NRF_LOG_RAW_INFO("\n\t>> No se encontro una fecha en la memoria.");
        NRF_LOG_FLUSH();

        NRF_LOG_RAW_INFO("\n\t>> Cargando valor predeterminado.");
        NRF_LOG_FLUSH();

        datetime_t now = {.year   = 2000,
                          .month  = 2,
                          .day    = 29,
                          .hour   = 23,
                          .minute = 59,
                          .second = 50};
        calendar_set_time(&now);
        NRF_LOG_RAW_INFO("\n\t>> Fecha: %04u-%02u-%02u, Hora: %02u:%02u:%02u\n",
                         now.year, now.month, now.day, now.hour, now.minute,
                         now.second);
        NRF_LOG_FLUSH();

        return true;
    }
}
