# SeeedMote v2 — dev shortcuts (mote only; gateway uses ESPHome CLI)
#
# Usage:
#   make build                  compile mote firmware (release; RTT logs only)
#   make build DEBUG=1          compile with USB CDC console (mote/debug.conf)
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

ifdef DEBUG
  EXTRA_BUILD_ARGS := -- -DEXTRA_CONF_FILE=debug.conf
else
  EXTRA_BUILD_ARGS :=
endif

.PHONY: all build flash monitor run clean app

all: run

build:
	cd $(WEST_DIR) && ZEPHYR_BASE=$(ZEPHYR_BASE) $(WEST) build --no-sysbuild -p always -b $(NRF_BOARD) -d $(NRF_BUILD) $(EXTRA_BUILD_ARGS)

# 1200-baud touch: a DEBUG=1 build's CDC handler reboots the chip into the
# UF2 bootloader when the host briefly opens the port at 1200 baud, so no
# physical double-tap is needed. Release builds (no USB CDC) silently
# ignore the touch — fall back to the manual double-tap.
flash: build
	@for port in $(MONITOR_PORT); do \
		[ -e "$$port" ] || continue; \
		echo "1200-baud touch -> $$port"; \
		python3 -c "import serial,time; s=serial.Serial('$$port', 1200); time.sleep(0.05); s.close()" 2>/dev/null || true; \
	done
	@echo "Waiting for $(UF2_VOLUME) (up to 6s)..."
	@for i in $$(seq 1 20); do \
		[ -d "$(UF2_VOLUME)" ] && break; sleep 0.3; \
	done
	@[ -d "$(UF2_VOLUME)" ] || { echo "$(UF2_VOLUME) not mounted — double-tap RESET and retry"; exit 1; }
	cp -X $(WEST_DIR)/$(NRF_BUILD)/zephyr/zephyr.uf2 $(UF2_VOLUME)/

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
