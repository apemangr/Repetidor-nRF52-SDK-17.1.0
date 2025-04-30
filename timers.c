// timers.c
#include "timers.h"

#include "app_timer.h"

// --- Definiciones de Timers (Internas a este módulo) ---
APP_TIMER_DEF(
    m_on_time_timer_id);  // Declara el timer para el tiempo de actividad
APP_TIMER_DEF(
    m_sleep_time_timer_id);  // Declara el timer para el tiempo de reposo

static void sleep_mode_enter(void)
{
	NRF_LOG_INFO("\n\nEntrando en modo de ahorro de energia (SYSTEM_ON).");

    in_sleep_mode = true;  // Cambia el estado a modo de ahorro

    NRF_LOG_INFO("La variable in_sleep_mode es %d", in_sleep_mode);
	// Asegurarse de que no haya eventos pendientes antes de entrar en modo de
	// ahorro
	while (NRF_LOG_PROCESS());
	// bsp_indication_set(BSP_INDICATE_IDLE);

    NRF_LOG_FLUSH();

	// Entrar en modo SYSTEM_ON
    while(true)
    {
        // Si el timer de reposo expira, se despierta el dispositivo
        sd_app_evt_wait();  // Esperar evento (más eficiente que __WFE)
        break;
    }
		//sd_app_evt_wait();  // Esperar evento (más eficiente que __WFE)

    NRF_LOG_INFO("La variable in_sleep_mode es %d", in_sleep_mode);
    in_sleep_mode = false;  // Cambia el estado a modo de ahorro

    NRF_LOG_INFO("La variable in_sleep_mode es %d", in_sleep_mode);
	NRF_LOG_INFO("Si ves este mensaje muy rapido no esta durmiendo");
}

/**
 * @brief Handler llamado cuando el timer de reposo (SLEEP) expira.
 */
static void sleep_timer_handler(void* p_context)
{
    in_sleep_mode = false;
	NRF_LOG_INFO("\n\nEncendiendo dispositivo del reposo por %d ms.",
	             DEVICE_ON_TIME_MS);

	// Iniciar el timer de actividad para reiniciar el ciclo
	ret_code_t err_code =
	    app_timer_start(m_on_time_timer_id, ON_DURATION_TICKS, NULL);
	APP_ERROR_CHECK(err_code);
}

// --- Handlers de los Timers (Estáticos) ---
/**
 * @brief Handler llamado cuando el timer de actividad (ON) expira.
 */
static void on_timer_handler(void* p_context)
{
	NRF_LOG_INFO("Tiempo de encendido terminado. Se apagara por %d ms.",
	             DEVICE_SLEEP_TIME_MS);

	// Podrías añadir lógica específica de desactivación aquí

	// Iniciar el timer de reposo
	ret_code_t err_code =
	app_timer_start(m_sleep_time_timer_id, SLEEP_DURATION_TICKS, NULL);
	APP_ERROR_CHECK(err_code);
	// El sistema entrará en reposo vía nrf_pwr_mgmt_run() en main loop
	sleep_mode_enter();
}

// --- Implementación de Funciones Públicas ---

/**
 * @brief Inicializa y crea los timers específicos de la aplicación (ON/SLEEP).
 */
ret_code_t timers_app_init(void)
{
	ret_code_t err_code;

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

	NRF_LOG_INFO("Timers creados correctamente.");
	return NRF_SUCCESS;
}

/**
 * @brief Inicia el ciclo de timers, comenzando con el timer de actividad (ON
 * timer).
 */
ret_code_t timers_start_cycle(void)
{
	NRF_LOG_INFO("Starting timer cycle (ON -> SLEEP).");
	ret_code_t err_code =
	    app_timer_start(m_on_time_timer_id, ON_DURATION_TICKS, NULL);
	APP_ERROR_CHECK(err_code);  // O podrías retornar err_code si prefieres
	                            // manejarlo en main
	return err_code;            // Retorna el resultado de app_timer_start
}