/*
 * Copyright 2020 Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/eeprom.h>
#include <zephyr.h>
#include <ztest.h>

static void test_size(void)
{
	struct device *eeprom;
	size_t size;

	eeprom = device_get_binding(DT_LABEL(DT_ALIAS(eeprom_0)));
	zassert_not_null(eeprom, "Expected EEPROM");

	size = eeprom_get_size(eeprom);
	zassert_not_equal(0, size, "Unexpected size of zero bytes");
}

static void test_write_and_verify(void)
{
	const uint8_t wr_buf1[4] = { 0xff, 0xee, 0xdd, 0xcc };
	const uint8_t wr_buf2[sizeof(wr_buf1)] = { 0xaa, 0xbb, 0xcc, 0xdd };
	uint8_t rd_buf[sizeof(wr_buf1)];
	struct device *eeprom;
	int rc;

	eeprom = device_get_binding(DT_LABEL(DT_ALIAS(eeprom_1)));

	rc = eeprom_write(eeprom, 0, wr_buf1, sizeof(wr_buf1));
	zassert_equal(0, rc, "Unexpected error code (%d)", rc);

	rc = eeprom_read(eeprom, 0, rd_buf, sizeof(rd_buf));
	zassert_equal(0, rc, "Unexpected error code (%d)", rc);

	rc = memcmp(wr_buf1, rd_buf, sizeof(wr_buf1));
	zassert_equal(0, rc, "Unexpected error code (%d)", rc);

	rc = eeprom_write(eeprom, 0, wr_buf2, sizeof(wr_buf2));
	zassert_equal(0, rc, "Unexpected error code (%d)", rc);

	rc = eeprom_read(eeprom, 0, rd_buf, sizeof(rd_buf));
	zassert_equal(0, rc, "Unexpected error code (%d)", rc);

	rc = memcmp(wr_buf2, rd_buf, sizeof(wr_buf2));
	zassert_equal(0, rc, "Unexpected error code (%d)", rc);
}

void test_main(void)
{
	ztest_test_suite(test_i2c_emul,
			 ztest_unit_test(test_size),
			 ztest_unit_test(test_write_and_verify));
	ztest_run_test_suite(test_i2c_emul);
}
