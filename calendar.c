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
    NRF_LOG_RAW_INFO("\n>> Tiempo de sleep: %u ms", sleep_time_from_flash);
    uint32_t next_event = (current_counter + (sleep_time_from_flash / 1000) * 8) & 0xFFFFFF;
    nrfx_rtc_cc_set(&m_rtc, 1, next_event, true);
}

void restart_on_rtc(void)
{
    uint32_t current_counter = nrfx_rtc_counter_get(&m_rtc);
    uint32_t read_time       = read_time_from_flash(TIEMPO_ENCENDIDO, DEFAULT_DEVICE_ON_TIME_MS);
    NRF_LOG_RAW_INFO("\n>> Tiempo de encendido: %u ms", read_time);
    uint32_t next_event = (current_counter + (read_time / 1000) * 8) & 0xFFFFFF;
    nrfx_rtc_cc_set(&m_rtc, 0, next_event, true);
}

void restart_extended_on_rtc(void)
{
    uint32_t current_counter = nrfx_rtc_counter_get(&m_rtc);
    uint32_t read_time =
        read_time_from_flash(TIEMPO_EXTENDED_ENCENDIDO, DEFAULT_DEVICE_EXTENDED_ON_TIME_MS);
    // NRF_LOG_RAW_INFO("\n\t>> Tiempo de encendido: %u ms", read_time);
    uint32_t next_event = (current_counter + (read_time / 1000) * 8) & 0xFFFFFF;
    nrfx_rtc_cc_set(&m_rtc, 0, next_event, true);
}

void restart_extended_sleep_rtc(void)
{
    uint32_t current_counter = nrfx_rtc_counter_get(&m_rtc);
    uint32_t extended_sleep_time_from_flash =
        read_time_from_flash(TIEMPO_EXTENDED_SLEEP, DEFAULT_DEVICE_EXTENDED_SLEEP_TIME_MS);
    // NRF_LOG_RAW_INFO("\n\t>> Tiempo de sleep: %u ms", sleep_time_from_flash);
    uint32_t next_event =
        (current_counter + (extended_sleep_time_from_flash / 1000) * 8) & 0xFFFFFF;
    nrfx_rtc_cc_set(&m_rtc, 1, next_event, true);
}

static inline bool is_leap_year(uint16_t year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static bool is_valid_datetime(const datetime_t *dt)
{
    if (dt == NULL) return false;
    
    // Verificar rangos básicos
    if (dt->year < 2000 || dt->year > 2099) return false;
    if (dt->month < 1 || dt->month > 12) return false;
    if (dt->hour > 23 || dt->minute > 59 || dt->second > 59) return false;
    
    // Verificar día válido para el mes
    uint8_t max_day = (dt->month == 2) ? (is_leap_year(dt->year) ? 29 : 28) : days_in_month[dt->month - 1];
    if (dt->day < 1 || dt->day > max_day) return false;
    
    return true;
}

void                 calendar_rtc_handler(void)
{
    m_tick_flag  = true;
    uint32_t cnt = nrfx_rtc_counter_get(&m_rtc);
    nrfx_rtc_cc_set(&m_rtc, RTC_CHANNEL, (cnt + 8) & RTC_COUNTER_COUNTER_Msk, true);
}

bool calendar_set_time(const datetime_t *now)
{
    if (!m_initialized || now == NULL)
    {
        return false;
    }
    // Validate fields
    uint8_t max_day =
        (now->month == 2) ? (is_leap_year(now->year) ? 29 : 28) : days_in_month[now->month - 1];
    if (now->month < 1 || now->month > 12 || now->day < 1 || now->day > max_day || now->hour > 23 ||
        now->minute > 59 || now->second > 59)
    {
        return false;
    }

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

    NRF_LOG_RAW_INFO("\n\t>> \033[0;32mModulo RTC inicializado correctamente.\033[0m");

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
                uint8_t dim = (m_time.month == 2) ? (is_leap_year(m_time.year) ? 29 : 28)
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
    datetime_t dt = {0};
    bool success = false;
    const char* source = "";

    // Prioridad 1: Intentar usar config_repeater.fecha si es válida
    if (is_valid_datetime(&config_repeater.fecha))
    {
        dt = config_repeater.fecha;
        source = "config_repeater";
        success = calendar_set_time(&dt);
        
        if (success)
        {
            NRF_LOG_RAW_INFO("\n\t>> Fecha y hora cargada desde %s.", source);
        }
        else
        {
            NRF_LOG_RAW_INFO("\n\t>> Error al cargar fecha desde %s.", source);
        }
    }
    
    // Prioridad 2: Si config_repeater no es válida o falló, intentar cargar desde flash
    if (!success)
    {
        dt = read_date_from_flash();
        source = "memoria flash";
        
        // read_date_from_flash() retorna una fecha válida si existe, o valores cero si no existe
        if (is_valid_datetime(&dt))
        {
            success = calendar_set_time(&dt);
            
            if (success)
            {
                NRF_LOG_RAW_INFO("\n\t>> Fecha y hora cargada desde %s.", source);
            }
            else
            {
                NRF_LOG_RAW_INFO("\n\t>> Error al cargar fecha desde %s.", source);
            }
        }
        else
        {
            NRF_LOG_RAW_INFO("\n\t>> No se encontró fecha válida en %s.", source);
        }
    }
    
    // Prioridad 3: Si todo falló, usar valores por defecto
    if (!success)
    {
        dt = (datetime_t){.year = 2000, .month = 1, .day = 1, .hour = 0, .minute = 0, .second = 0};
        source = "valores por defecto";
        success = calendar_set_time(&dt);
        
        if (success)
        {
            NRF_LOG_RAW_INFO("\n\t>> Fecha y hora cargada desde %s.", source);
        }
        else
        {
            NRF_LOG_RAW_INFO("\n\t>> Error crítico: no se pudo establecer fecha.");
        }
    }
    
    // Mostrar fecha establecida
    if (success)
    {
        NRF_LOG_RAW_INFO("\n\t>> Fecha: %04u-%02u-%02u, Hora: %02u:%02u:%02u",
                         dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        NRF_LOG_RAW_INFO("\n\t>> Fuente: %s\n", source);
    }
    
    NRF_LOG_FLUSH();
    return success;
}
