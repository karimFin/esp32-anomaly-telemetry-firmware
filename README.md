# Industrial Edge Node

Simulator-first embedded capstone for `macOS 15` on `Apple Silicon`
## nutshell
- `C++17` firmware structure
- `ESP32` + `FreeRTOS` tasks
- `queue-based task communication`
- `I2C driver + hardware abstraction layer`
- `sensor acquisition and filtering`
- `alarm state machines and latched faults`
- `watchdog-style task supervision`
- `UART diagnostics console`
- `MQTT telemetry publishing`
- `host-runnable unit tests`
- `Wokwi` simulation

## Project Layout

- `src/main.cpp`: ESP32 firmware entry point and task wiring
- `src/alarm_logic.cpp`: testable alarm and filtering logic
- `src/mpu6050_driver.cpp`: platform-independent I2C sensor driver
- `src/i2c_bus_arduino.cpp`: Arduino/Wire implementation of the I2C bus abstraction
- `src/mqtt_telemetry.cpp`: WiFi + MQTT telemetry client
- `src/console_parser.cpp`: testable UART command parsing
- `include/*.hpp`: shared interfaces
- `test/test_core.cpp`: host-side tests for macOS
- `diagram.json`: Wokwi hardware diagram
- `wokwi.toml`: Wokwi + PlatformIO mapping
- `platformio.ini`: build configuration

## Simulated Hardware

- `ESP32 DevKit V1`
- `DHT22` for temperature and humidity
- `slide potentiometer` as a gas/smoke analog sensor
- `MPU6050` over `I2C` for vibration/condition monitoring
- `green/yellow/red LEDs` for node state

## Firmware Behavior

The node is split into five concurrent responsibilities:

1. `sensor_task`: samples `DHT22`, gas input, and `MPU6050` every `500 ms`
2. `processing_task`: computes moving averages
3. `control_task`: evaluates thresholds and alarm state
4. `console_task`: handles UART commands
5. `supervisor_task`: detects stalled tasks and forces a fault state
6. `telemetry_task`: connects to WiFi/MQTT and publishes JSON telemetry

State behavior:

- `NORMAL`: green LED on
- `WARNING`: yellow LED blinks
- `ALARM`: red LED blinks fast
- `FAULT`: red and yellow alternate

The alarm is latched. Once an alarm occurs, the node stays in `ALARM` until readings return to safe values for the configured hold time.

The `MPU6050` is read through a small I2C abstraction. That separation is important in embedded jobs because application logic should not be tied directly to `Wire`, vendor HAL calls, or a specific board.

## macOS Setup

Install PlatformIO:

```bash
brew install platformio
```

Optional local editor support:

```bash
brew install --cask visual-studio-code
```

## Build Firmware

From this folder:

```bash
pio run -e esp32dev
```

## Run In Wokwi

### Option 1: Browser

1. Open [Wokwi](https://wokwi.com/).
2. Create a new `ESP32` project.
3. Create tabs for the files under `include/` and `src/`.
4. Copy this repository's `src/*.cpp` and `include/*.hpp` contents into those tabs.
5. Replace the project `diagram.json` with this repository's `diagram.json`.
6. Start the simulator and open the serial monitor at `115200`.

### Option 2: VS Code + Wokwi Extension

1. Install the `PlatformIO IDE` and `Wokwi Simulator` extensions in VS Code.
2. Build the firmware:

```bash
pio run -e esp32dev
```

3. Open the folder in VS Code.
4. Start Wokwi simulation using the extension. It will use `wokwi.toml`.

## UART Console Commands

Use the serial monitor at `115200` and send one command per line:

```text
help
status
thresholds
reset_alarm
set temp_warn 36
set temp_alarm 44
set hum_warn 72
set hum_alarm 85
set vib_warn 0.18
set vib_alarm 0.35
set gas_warn 1900
set gas_alarm 2900
set safe_ms 10000
```

## What To Observe

- Moving the potentiometer upward pushes the gas reading toward `WARNING` and `ALARM`
- Raising `DHT22` temperature can also trigger alarms
- Changing the `MPU6050` orientation changes the derived vibration magnitude and can trigger `WARNING` or `ALARM`
- Alarm state stays latched even after readings normalize until the safe hold timer expires
- The serial console prints structured telemetry that looks like production device logs
- Telemetry is published to MQTT when WiFi and broker are available
- Changing thresholds over UART immediately affects the live system
- If a task stops updating its heartbeat, the supervisor forces a `FAULT`

## MQTT Telemetry

The firmware publishes JSON payloads to:

- Broker: `broker.hivemq.com:1883`
- Topic: `karimFin/esp32-edge-monitor-firmware/telemetry`
- Publish cadence: about every `2 seconds` when new samples arrive
- WiFi for Wokwi: `Wokwi-GUEST` (empty password)

Sample payload:

```json
{
  "sample": 120,
  "temp_avg": 33.27,
  "hum_avg": 58.11,
  "vib_avg": 0.142,
  "gas": 1710,
  "state": "WARNING",
  "latched": 0,
  "safe_ms": 0,
  "fault": 0,
  "reason": "none"
}
```

## Run Host Tests On macOS

These tests validate the alarm logic, command parser, and I2C sensor-driver decoding without Arduino or Wokwi:

```bash
mkdir -p build
clang++ -std=c++17 -Iinclude test/test_core.cpp src/alarm_logic.cpp src/console_parser.cpp src/mpu6050_driver.cpp -o build/test_core
./build/test_core
```
