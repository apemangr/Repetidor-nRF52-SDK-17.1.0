# Firmaware para dispositivo BLE Repetidor

## Como usar
Extraer en `examples/ble_central_and_peripheral`.

## Soporta
- NRF52832
- NRF52840 (sin revisión)

## Comandos

Todos los comandos que vayan dirigidos al repetidor deben tener el prefijo `111`.

| Comando | Función                             | Descripción                                                                          | Ejemplo                                                  |
| :------ | :---------------------------------- | :----------------------------------------------------------------------------------- | :------------------------------------------------------- |
| 01      | Guardar MAC                         | Escribe en la memoria la MAC a conectarse                                            | 11101A566CE57FF66                                        |
| 02      | Leer MAC                            | Lee la MAC guardada en la memoria flash                                              | 11102                                                    |
| 03      | Reiniciar                           | Reinicia el repetidor                                                                | 11103                                                    |
| 04      | Guardar tiempo encendido            | Escribe en la memoria flash el tiempo de encendido en segundos (máximo 666 segundos) | 11104666                                                 |
| 05      | Leer tiempo encendido               | Lee el tiempo en que debe mantenerse despierto el dispositivo                        | 11105                                                    |
| 06      | Grabar tiempo dormido               | Escribe en la memoria flash el tiempo de dormido en segundos (máximo 6666 segundos)  | 111066666                                                |
| 07      | Leer tiempo dormido                 | Lee el tiempo en que debe mantenerse dormido el dispositivo                          | 11107                                                    |
| 08      | Guardar fecha y hora                | Escribe en la memoria flash la fecha (AAAAMMDD) y hora (HHMMSS), formato ISO8601     | 1110820250530130200 <br> (30 de mayo del 2025, 13:02:00) |
| 09      | Leer fecha y hora                   | Lee en la memoria flash la fecha y hora                                              | 11109                                                    |
| 10      | Solicitar el ultimo historial (WIP) | Lee del repetidor el último valor guardado en el historial                           | 11110                                                    |