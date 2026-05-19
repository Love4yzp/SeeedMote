# SeeedMote v2 — mote_motion_nrf52840 Zephyr app build.
#
# Standard Zephyr / NCS workspace app layout. Run from this directory:
#   west build -b xiao_ble

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mote_motion_nrf52840)

target_sources(app PRIVATE src/main.c)
