# SeeedMote v2 — dev shortcuts
#
# Usage:
#   make                        flash + monitor (gateway, default)
#   make build                  compile only
#   make flash                  compile + upload
#   make monitor                open serial monitor
#   make clean                  clean build artifacts
#   make erase                  erase entire flash (ESP32 only)
#
#   make PROJECT=mote_motion_nrf52840 flash
#
# ESP32 PORT override (auto-detected by PIO when omitted):
#   make PORT=/dev/cu.usbmodem101 flash
#
# nRF52 overrides:
#   make PROJECT=mote_motion_nrf52840 NCS_VERSION=v2.9.2 build
#   make PROJECT=mote_motion_nrf52840 UF2_VOLUME=/Volumes/XIAO-SENSE flash
#   make PROJECT=mote_motion_nrf52840 PORT=/dev/cu.usbmodem101 monitor

PROJECT ?= gateway_basic_esp32s3
PIO_DIR := projects/$(PROJECT)

PIO     := pio
UPLOAD  := -t upload
MONITOR := -t monitor

WEST_DIR    := projects/$(PROJECT)
NCS_VERSION ?= v2.9.2
ZEPHYR_BASE ?= $(HOME)/ncs/v2.9.2/zephyr
WEST        ?= nrfutil toolchain-manager launch --ncs-version $(NCS_VERSION) -- west
NRF_BOARD   ?= xiao_ble/nrf52840/sense
NRF_BUILD   ?= build_uf2
UF2_VOLUME  ?= /Volumes/XIAO-SENSE

ifdef PORT
  UPLOAD  += --upload-port $(PORT)
  MONITOR += --monitor-port $(PORT)
endif

.PHONY: all build flash monitor run clean erase

all: run

ifneq (,$(findstring _nrf52,$(PROJECT)))
build:
	cd $(WEST_DIR) && ZEPHYR_BASE=$(ZEPHYR_BASE) $(WEST) build --no-sysbuild -p always -b $(NRF_BOARD) -d $(NRF_BUILD)

flash: build
	cp $(WEST_DIR)/$(NRF_BUILD)/zephyr/zephyr.uf2 $(UF2_VOLUME)/

monitor:
	tio $(if $(PORT),$(PORT),/dev/cu.usbmodem*)

run: flash monitor

clean:
	cd $(WEST_DIR) && ZEPHYR_BASE=$(ZEPHYR_BASE) $(WEST) build -d $(NRF_BUILD) -t pristine

erase:
	@echo "erase is not supported for nRF52 UF2 flow; use SWD-specific west flash options manually"
	@exit 2
else
build:
	$(PIO) run -d $(PIO_DIR)

flash:
	$(PIO) run -d $(PIO_DIR) $(UPLOAD)

monitor:
	$(PIO) run -d $(PIO_DIR) $(MONITOR)

run:
	$(PIO) run -d $(PIO_DIR) $(UPLOAD) $(MONITOR)

clean:
	$(PIO) run -d $(PIO_DIR) -t clean

erase:
	$(PIO) run -d $(PIO_DIR) -t erase
endif
