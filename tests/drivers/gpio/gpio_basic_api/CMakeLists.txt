# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)

# This manually adds a DTC overlay file if such exists in
# tests/common/gpio-fixture/boards for the board.
# This breaks what is documented regarding inclusion of overlay files:
# https://docs.zephyrproject.org/latest/guides/dts/howtos.html#set-devicetree-overlays
# so in case this approach is taken, it should be documented properly.
# As long as this principle is only used under tests/ it is acceptable.
zephyr_file(CONF_FILES ${ZEPHYR_BASE}/tests/common/gpio-fixture/boards
            DTS        DTC_OVERLAY_FILE
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gpio_basic_api)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
