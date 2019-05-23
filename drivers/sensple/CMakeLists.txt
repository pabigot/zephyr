# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_SENSPLE_SHT21	sht21)

zephyr_sources(sensple_common.c)
