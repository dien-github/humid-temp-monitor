# Humid-Temp Monitor

An ESP32-based smart home humidity and temperature monitoring system built with ESP-IDF. This project provides real-time environmental sensing with remote control capabilities via MQTT.

## Overview

This system monitors room temperature and humidity using a DHT11/DHT22 sensor and provides control over output devices (relay and fan) through MQTT commands. It's designed for smart home automation, featuring asynchronous networking, configurable settings stored in non-volatile storage (NVS), and robust FreeRTOS task management.

## Features

- **Temperature & Humidity Monitoring**: Real-time sensor readings with data validation and caching
- **MQTT Integration**: Publish sensor data and receive control commands via MQTT broker
- **WiFi Connectivity**: Async WiFi connection with auto-reconnect
- **Output Control**: Relay switching and PWM fan speed control
- **Persistent Configuration**: Settings stored in NVS (survives reboots)
- **FreeRTOS Tasks**: Independent tasks for sensing, MQTT communication, output control, and system monitoring
- **Comprehensive Logging**: Debug and info level logging throughout

## Hardware Requirements

- **ESP32** development board
- **DHT11 or DHT22** temperature/humidity sensor
- **Relay module** (5V compatible)
- **DC Fan** with PWM control (optional)
- External 10kΩ pull-up resistor for DHT data line (recommended)

### Default GPIO Pin Assignments

| Component | GPIO Pin |
|-----------|----------|
| DHT Sensor | GPIO4 |
| Relay | GPIO5 |
| Fan (PWM) | GPIO18 |

## Software Architecture

```
├── main/
│   └── main.c              # Application entry point
├── components/
│   ├── app_config/         # Configuration management (NVS)
│   ├── network/            # WiFi and MQTT clients
│   ├── output/             # Relay and fan control
│   ├── sensor/             # DHT sensor driver
│   ├── system/             # FreeRTOS task management
│   └── utils/              # Utility functions
└── tests/
    ├── unit/               # Unit tests
    └── integration/        # Integration tests
```

### Component Overview

| Component | Description |
|-----------|-------------|
| `app_config` | Configuration loading, saving, and validation via NVS |
| `network` | WiFi connection and MQTT client with async callbacks |
| `output` | GPIO-based relay and PWM fan control |
| `sensor` | DHT11/DHT22 driver with error handling and caching |
| `system` | FreeRTOS task orchestration and inter-task communication |
| `utils` | Common utilities and helper functions |

## Initialization Sequence

The application follows a phased initialization:

1. **Configuration Load**: Initialize NVS and load settings from non-volatile storage
2. **Hardware Init**: Configure GPIO pins for sensor and outputs
3. **Task System**: Create FreeRTOS queues, events, and start all application tasks
4. **WiFi Connection**: Async WiFi initialization (non-blocking)
5. **MQTT Connection**: Async MQTT broker connection (non-blocking)
6. **Operational**: Tasks synchronize via events; system runs with periodic sensor reads and MQTT communication

## Building the Project

### Prerequisites

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) installed and configured
- Python 3.8+

### Build Commands

```bash
# Set up ESP-IDF environment
. $IDF_PATH/export.sh

# Configure project (optional)
idf.py menuconfig

# Build
idf.py build

# Flash to ESP32
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor
```

## Configuration

### Default Settings

Settings can be configured at compile-time via defaults or at runtime via NVS:

| Setting | Default Value |
|---------|---------------|
| MQTT Broker URI | `mqtt://192.168.1.40:8883` |
| MQTT Sensor Topic | `room_1/sensors` |
| MQTT Command Topic | `room_1/commands` |
| Sensor Read Interval | 5000 ms |
| MQTT QoS | 1 |

### Runtime Configuration API

```c
// Save WiFi credentials (persists to NVS)
app_config_save_wifi("MyNetwork", "MyPassword123");

// Save MQTT broker URI
app_config_save_mqtt_uri("mqtt://broker.example.com:1883");

// Save GPIO pins
app_config_save_gpio_pins(4, 5, 18);  // DHT, Relay, Fan

// Reset to defaults
app_config_reset_to_defaults();
```

## MQTT Topics

### Sensor Data (Published)

Topic: `room_1/sensors`

```json
{
  "temperature": 25.5,
  "humidity": 60.0,
  "timestamp": 1234567890
}
```

### Control Commands (Subscribed)

Topic: `room_1/commands`

| Command | Value | Description |
|---------|-------|-------------|
| `relay` | `0` or `1` | Turn relay OFF or ON |
| `fan` | `0-255` | Set fan PWM duty cycle |

## API Reference

### Sensor Module

```c
// Initialize DHT sensor on GPIO pin
app_err_t sensor_dht_init(uint8_t pin);

// Read current sensor data (blocking ~40ms)
app_err_t sensor_dht_read(sensor_data_t *sensor_data);

// Get cached reading (non-blocking)
app_err_t sensor_dht_get_last_reading(sensor_data_t *sensor_data);

// Check sensor health
bool sensor_dht_is_healthy(void);
```

### Output Module

```c
// Initialize output pins
app_err_t app_output_init(uint8_t relay_pin, uint8_t fan_pin);

// Relay control
app_err_t app_output_set_relay(relay_state_t state);
relay_state_t app_output_get_relay(void);

// Fan control (0-255 PWM)
app_err_t app_output_set_fan_speed(int speed);
uint8_t app_output_get_fan_speed(void);

// Emergency stop
app_err_t app_output_emergency_stop(void);
```

## Documentation

Generate API documentation using Doxygen:

```bash
doxygen Doxyfile
```

Documentation will be output to `docs/doxygen/html/`.

## Testing

```bash
# Run unit tests
cd tests/unit
idf.py build
idf.py flash monitor

# Run integration tests
cd tests/integration
idf.py build
idf.py flash monitor
```

## Error Handling

The application uses a unified error code system defined in `components/app_config/include/app_common.h`:

| Error Code | Description |
|------------|-------------|
| `APP_OK` | Success |
| `APP_ERR_INVALID_PARAM` | Invalid parameter |
| `APP_ERR_TIMEOUT` | Operation timeout |
| `APP_ERR_SENSOR_READ` | Sensor read failure |
| `APP_ERR_MQTT_PUBLISH` | MQTT publish failure |
| `APP_ERR_WIFI_CONNECT` | WiFi connection failure |
| `APP_ERR_MQTT_CONNECT` | MQTT connection failure |
| `APP_ERR_NO_MEMORY` | Memory allocation failure |
| `APP_ERR_INVALID_VALUE` | Invalid value provided |
| `APP_ERR_UNKNOWN` | Unknown error |

## License

This project is provided as-is for educational and personal use.

## Author

Developed for smart home automation applications using the ESP-IDF framework.