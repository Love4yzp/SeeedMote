# SeeedMote v2 — dev shortcuts (mote only; gateway uses ESPHome CLI)
#
# Usage:
#   make build                  compile mote firmware
#   make flash                  compile + copy UF2 to XIAO bootloader volume
#   make monitor                open serial monitor
#   make run                    flash + monitor
#   make clean                  clean build artifacts
#
# Overrides:
#   make NCS_VERSION=v2.9.2 build
#   make UF2_VOLUME=/Volumes/XIAO-SENSE flash
#   make PORT=/dev/cu.usbmodem101 monitor
#
# Gateway (ESPHome):
#   esphome run gateway/esphome.yaml
#
# App demo:
#   make app

WEST_DIR    := mote
NCS_VERSION ?= v2.9.2
ZEPHYR_BASE ?= $(HOME)/ncs/$(NCS_VERSION)/zephyr
WEST        ?= nrfutil toolchain-manager launch --ncs-version $(NCS_VERSION) -- west
NRF_BOARD   ?= xiao_ble/nrf52840/sense
NRF_BUILD   ?= build_uf2
UF2_VOLUME  ?= /Volumes/XIAO-SENSE

ifdef PORT
  MONITOR_PORT := $(PORT)
else
  MONITOR_PORT := /dev/cu.usbmodem*
endif

.PHONY: all build flash monitor run clean app

all: run

build:
	cd $(WEST_DIR) && ZEPHYR_BASE=$(ZEPHYR_BASE) $(WEST) build --no-sysbuild -p always -b $(NRF_BOARD) -d $(NRF_BUILD)

flash: build
	cp $(WEST_DIR)/$(NRF_BUILD)/zephyr/zephyr.uf2 $(UF2_VOLUME)/

monitor:
	tio $(MONITOR_PORT)

run: flash monitor

clean:
	cd $(WEST_DIR) && ZEPHYR_BASE=$(ZEPHYR_BASE) $(WEST) build -d $(NRF_BUILD) -t pristine

# App demo (app-solution/retail/)
#   make app             live mode (requires MQTT_BROKER env var)
#   make app MOCK=true   scripted mock events, no hardware needed
app:
	$(MAKE) -C app-solution/retail dev MOCK=$(MOCK)
