# WiFiDongle
WiFi to serial server for ESP32 with OLED display support.

See https://www.zen-room.org/the-zen-room/serial-to-wifi-plug

## Hardware

- ESP32 dev board
- Optional: SSD1306 128x32 OLED display (I2C, address 0x3C)

## Prerequisites

### PlatformIO (recommended)

Install PlatformIO Core (CLI):

```sh
pip install platformio
```

Or install the VS Code extension: search for "PlatformIO IDE" in the extensions marketplace.

### arduino-cli (alternative)

Install arduino-cli:

```sh
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

Then install the ESP32 board support:

```sh
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

## Building

### With PlatformIO (default)

```sh
make              # compile
make upload       # compile and flash to ESP32
make monitor      # open serial monitor
make upload-monitor  # flash then monitor
make clean        # clean build artifacts
```

### With arduino-cli

```sh
make ino              # compile
make ino-upload       # compile and flash to ESP32
make ino-upload-monitor  # flash then monitor
make ino-clean        # clean build artifacts
```

### Options

Override the serial port or baud rate:

```sh
make upload PORT=/dev/ttyACM0
make monitor BAUD=38400
```

## Configuration

On first boot, the device uses default credentials defined in the source
(`DEFAULT_SSID`, `DEFAULT_PASS`, etc. in `WiFiDongle.ino`).

At runtime, send any character over the serial console to enter the
configuration editor. Changes are saved to EEPROM and persist across reboots.
If WiFi fails to connect within 30 seconds, the editor opens automatically.

### EEPROM format

The EEPROM stores a single ASCII string:

```
"ssid","password",port:baud:flowctl:rx,tx,rts,cts
```

| Field    | Description                          | Default |
|----------|--------------------------------------|---------|
| ssid     | WiFi network name                    | yourssid |
| password | WiFi password                        | yourpassword |
| port     | TCP port for the telnet server       | 23      |
| baud     | UART baud rate for Serial2           | 38400   |
| flowctl  | Hardware flow control (0=off, 1=RTS/CTS) | 0   |
| rx       | Serial2 RX GPIO pin                  | 16      |
| tx       | Serial2 TX GPIO pin                  | 17      |
| rts      | Serial2 RTS GPIO pin                 | 5       |
| cts      | Serial2 CTS GPIO pin                 | 4       |

The format is backward compatible: if older fields are present without the
newer ones (flowctl, pins), defaults are filled in and written back to EEPROM
automatically on boot.

## PCB design

The `kicad/` directory contains the KiCad schematic and PCB layout for a
carrier board. It includes a custom ESP32 DevKit V1 footprint and symbol
library. Open `kicad/WiFiDongle.kicad_pro` in KiCad 8 to view or edit.
