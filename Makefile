# Compatibility shim. The human-friendly entrypoint is ./dev.

.PHONY: help doctor build flash log monitor run clean gateway app

help:
	./dev --help

doctor:
	./dev doctor

build:
	./dev mote build

flash:
	./dev mote flash

log monitor:
	./dev mote log

run:
	./dev mote run

clean:
	./dev mote clean

gateway:
	./dev gateway run

app:
	./dev app run $(if $(filter true,$(MOCK)),--mock,)
