#
# Copyright (c) 2018 Peter Bigot Consulting, LLC
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_SHT21 sht21.c)
