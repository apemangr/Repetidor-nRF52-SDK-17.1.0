# Firmaware para dispositivo BLE Repetidor

## Como usar
Extraer en `examples/ble_central_and_peripheral`.

## Soporta
- NRF52832
- NRF52840 (sin revisión)


## Comandos repetidor

Todos los comandos que vayan dirigidos al repetidor deben tener el prefijo `111`.

### 01 - Guardar MAC

El formato del comando debe ser:
`111` + `MAC a escribir`

Ej:
>11101A566CE57FF66
