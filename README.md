# WiFiDongle
WiFi to serial server for ESP32 with OLED display support.

See https://www.zen-room.org/the-zen-room/serial-to-wifi-plug

## Hardware

- ESP32 dev board
- Optional: SSD1306 or SH1107 128x32 or 128x64 OLED display (I2C, address 0x3C)

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
"ssid","password",port:baud:flowctl:rx,tx,rts,cts:displayht:displaydrv
```

| Field      | Description                          | Default |
|------------|--------------------------------------|---------|
| ssid       | WiFi network name                    | yourssid |
| password   | WiFi password                        | yourpassword |
| port       | TCP port for the telnet server       | 23      |
| baud       | UART baud rate for Serial2           | 38400   |
| flowctl    | Hardware flow control (0=off, 1=RTS/CTS) | 0   |
| rx         | Serial2 RX GPIO pin                  | 17      |
| tx         | Serial2 TX GPIO pin                  | 16      |
| rts        | Serial2 RTS GPIO pin                 | 5       |
| cts        | Serial2 CTS GPIO pin                 | 4       |
| displayht  | OLED display height (32 or 64)       | 32      |
| displaydrv | Display driver (0=SSD1306, 1=SH1107) | 0       |

The format is backward compatible: if older fields are present without the
newer ones (flowctl, pins), defaults are filled in and written back to EEPROM
automatically on boot.

## Custom font

`FreeSans8pt7b.h` was generated from the GNU FreeSans TrueType font using
the Adafruit GFX `fontconvert` tool:

```sh
cd /path/to/Adafruit_GFX_Library/fontconvert
make
./fontconvert /usr/share/fonts/truetype/freefont/FreeSans.ttf 8 > FreeSans8pt7b.h
```

The `fontconvert` source is included in the
[Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library/tree/master/fontconvert).
It requires the FreeType development library (`libfreetype-dev` on Debian/Ubuntu).

## RS-232 connector and signal direction

The PCB is designed to be stuffed with a **female DB connector**, making the
board a DCE (Data Communications Equipment). This is correct for a device
that is connected to by a DTE (e.g. a PC with a male connector).

A DCE drives two signals:

| Signal | Direction | Description |
|--------|-----------|-------------|
| TXD    | DCE → DTE | Data output (ESP32 TX → connector pin 2) |
| CTS    | DCE → DTE | Clear to send (ESP32 RTS → connector pin 8) |

And receives two signals:

| Signal | Direction | Description |
|--------|-----------|-------------|
| RXD    | DTE → DCE | Data input (connector pin 3 → ESP32 RX) |
| RTS    | DTE → DCE | Request to send (connector pin 7 → ESP32 CTS) |

Note that the ESP32's RTS output maps to the connector's CTS pin, and the
ESP32's CTS input maps to the connector's RTS pin — the signal names cross
over at the DTE/DCE boundary.

**Important:** If the board is stuffed with a male connector instead, the
TXD/RXD and RTS/CTS pairs will be reversed, since a male connector implies
DTE pinout. The board would then drive the wrong pins.

When hardware flow control is disabled, the ESP32 asserts (drives low) the
RTS pin so that a connected DTE always sees CTS active.

At boot, CTS is held inactive (disasserted) until WiFi connects, then
asserted. This prevents the DTE from sending data before the network is
ready, and also serves as a quick visual check with an RS-232 monitor that
the CTS signal path is working.

## PCB design

The `kicad/` directory contains the KiCad schematic and PCB layout for a
carrier board. It includes a custom ESP32 DevKit V1 footprint and symbol
library. Open `kicad/WiFiDongle.kicad_pro` in KiCad 8 to view or edit.

## Display pin order

Many displays are compatible with the 4-pin header, but some have a different
arrangement of the power pins. The PCB implements the most common pin order:
GND, VCC, SCLK, SDATA. However, one family of 128x64 boards uses VCC, GND,
SCLK, SDATA. This can be corrected by cutting the circuit board traces and
using magnet wire or wire wrap wire to reorder the pins.
