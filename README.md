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

On first boot, the device uses default WiFi credentials defined in the source.
Edit the defaults (`DEFAULT_SSID`, `DEFAULT_PASS`, `DEFAULT_PORT1`, `DEFAULT_BAUD1`)
in `WiFiDongle.ino` before flashing.

At runtime, send any character over the serial console to enter the configuration
editor. Changes are saved to EEPROM and persist across reboots.

The EEPROM stores: SSID, password, TCP port, and UART baud rate.

If WiFi fails to connect within 30 seconds, the device enters the configuration
editor automatically, then reboots.
