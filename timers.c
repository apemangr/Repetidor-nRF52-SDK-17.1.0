#include "timers.h"

volatile bool device_on = true;  // Definición global, inicializada a false

// --- Definiciones de Timers (Internas a este módulo) ---
APP_TIMER_DEF(
    m_on_time_timer_id);  // Declara el timer para el tiempo de actividad
APP_TIMER_DEF(
    m_sleep_time_timer_id);  // Declara el timer para el tiempo de reposo

uint32_t device_on_time_ms =
    DEFAULT_DEVICE_ON_TIME_MS;  // Valor predeterminado (10 segundos)
uint32_t device_sleep_time_ms =
    DEFAULT_DEVICE_SLEEP_TIME_MS;  // Valor predeterminado (20 segundos)

/**
 * @brief Carga los tiempos de encendido y apagado desde la memoria flash.
 */
void load_timers_from_flash(void)
{
	// Leer tiempo de encendido desde la memoria flash
	device_on_time_ms =
	    read_time_from_flash(TIEMPO_ENCENDIDO, DEFAULT_DEVICE_ON_TIME_MS);

	// Leer tiempo de apagado desde la memoria flash
	device_sleep_time_ms =
	    read_time_from_flash(TIEMPO_SLEEP, DEFAULT_DEVICE_SLEEP_TIME_MS);
}

static void sleep_mode_enter(void)
{
	// NRF_LOG_RAW_INFO("\n\nEntrando en modo de ahorro de energia
	// (SYSTEM_ON).");

	// Asegurarse de que no haya eventos pendientes antes de entrar en modo de
	// ahorro
	while (NRF_LOG_PROCESS() == true);
	NRF_LOG_FLUSH();

	device_on = false;  // Indica que el dispositivo está en modo de reposo

	// NRF_LOG_RAW_INFO("\nSi ves este mensaje muy rapido no esta durmiendo");
}

/**
 * @brief Handler llamado cuando el timer de reposo (SLEEP) expira.
 */
static void sleep_timer_handler(void* p_context)
{
	NRF_LOG_RAW_INFO(
	    "\r\n\n** \x1b[2;33mEl dispositivo se encendera por %d ms\x1b[0m",
	    device_on_time_ms);

	device_on = true;  // Indica que el dispositivo está activo
	// Calcular los ticks dinámicamente
	uint32_t on_duration_ticks = APP_TIMER_TICKS(device_on_time_ms);

	// Iniciar el timer de actividad para reiniciar el ciclo
	ret_code_t err_code =
	    app_timer_start(m_on_time_timer_id, on_duration_ticks, NULL);
	APP_ERROR_CHECK(err_code);
}

/**
 * @brief Handler llamado cuando el timer de actividad (ON) expira.
 */
static void on_timer_handler(void* p_context)
{
	NRF_LOG_RAW_INFO(
	    "\r\n\n** \x1b[2;36mEl dispositivo se dormira por %d ms\x1b[0m",
	    device_sleep_time_ms);

	// Calcular los ticks dinámicamente
	uint32_t sleep_duration_ticks = APP_TIMER_TICKS(device_sleep_time_ms);

	// Iniciar el timer de reposo
	ret_code_t err_code =
	    app_timer_start(m_sleep_time_timer_id, sleep_duration_ticks, NULL);
	APP_ERROR_CHECK(err_code);

	// El sistema entrará en reposo vía nrf_pwr_mgmt_run() en main loop
	sleep_mode_enter();
}

/**
 * @brief Inicializa y crea los timers específicos de la aplicación (ON/SLEEP).
 */
ret_code_t timers_app_init(void)
{
	ret_code_t err_code;
	NRF_LOG_RAW_INFO("\n> Inicializando timers de encendido y reposo.");
	// Cargar tiempos desde la memoria flash
	load_timers_from_flash();

	// Crear el timer para el tiempo de actividad (modo single shot)
	err_code = app_timer_create(&m_on_time_timer_id, APP_TIMER_MODE_SINGLE_SHOT,
	                            on_timer_handler);
	VERIFY_SUCCESS(
	    err_code);  // Usa VERIFY_SUCCESS para retornar el error si falla
	NRF_LOG_DEBUG("Timer de encendido creado.");

	// Crear el timer para el tiempo de reposo (modo single shot)
	err_code =
	    app_timer_create(&m_sleep_time_timer_id, APP_TIMER_MODE_SINGLE_SHOT,
	                     sleep_timer_handler);
	VERIFY_SUCCESS(err_code);
	NRF_LOG_DEBUG("Timer de reposo creado.");

	NRF_LOG_RAW_INFO("\n\t>> \033[0;32mTimers creados correctamente.\033[0m\n");

	return NRF_SUCCESS;
}

/**
 * @brief Inicia el ciclo de timers, comenzando con el timer de actividad (ON
 * timer).
 */
ret_code_t timers_start_cycle(void)
{
	//"El tiempo es relativo... especialmente cuando estas esperando que se
	// acabe el lunes."

	NRF_LOG_RAW_INFO(
	    "\r\n** \x1b[2;33mEl dispositivo se encendera por %d ms\x1b[0m",
	    device_on_time_ms);

	// Calcular los ticks dinámicamente
	uint32_t on_duration_ticks = APP_TIMER_TICKS(device_on_time_ms);

	// Iniciar el timer de actividad
	ret_code_t err_code =
	    app_timer_start(m_on_time_timer_id, on_duration_ticks, NULL);
	APP_ERROR_CHECK(err_code);  // O podrías retornar err_code si prefieres
	                            // manejarlo en main

	if (err_code == NRF_SUCCESS)
	{
		device_on = true;  // ¡Importante! El dispositivo está ahora en fase ON
	}
	return err_code;  // Retorna el resultado de app_timer_start
}