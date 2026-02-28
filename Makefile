# WiFiDongle - ESP32 project
PORT ?= /dev/ttyUSB0
BAUD ?= 115200
FQBN ?= esp32:esp32:esp32

.PHONY: build upload monitor clean upload-monitor \
        ino ino-upload ino-clean ino-upload-monitor

# platformio targets (default, builds src/ files)
build:
	pio run

upload:
	pio run --target upload --upload-port $(PORT)

monitor:
	pio device monitor --port $(PORT) --baud $(BAUD)

upload-monitor: upload monitor

clean:
	pio run --target clean

# arduino-cli targets (builds .ino files)
ino:
	arduino-cli compile --fqbn $(FQBN) WiFiDongle.ino

ino-upload:
	arduino-cli upload --fqbn $(FQBN) --port $(PORT) WiFiDongle.ino

ino-upload-monitor: ino-upload monitor

ino-clean:
	rm -rf build/
