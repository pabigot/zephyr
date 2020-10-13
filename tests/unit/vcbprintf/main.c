/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ztest.h>
#include <limits.h>
#include <stdarg.h>
#include <sys/vcbprintf.h>
#include "../../../lib/os/vcbprintf.c"

#if __WORDSIZE == 64
#define M64_MODE 1
#endif

static char buf[128];
static char *bp;

static inline void reset_out(void)
{
	bp = buf;
	*bp = 0;
}

static int out(int c, void *dest)
{
	int rv = EOF;

	ARG_UNUSED(dest);
	if (bp < (buf + ARRAY_SIZE(buf))) {
		*bp++ = (char)(unsigned char)c;
		rv = (int)(unsigned char)c;
	}
	return rv;
}

static int prf(const char *format, ...)
{
	va_list ap;
	int rv;

	reset_out();
	va_start(ap, format);
	rv = sys_vcbprintf(out, buf, format, ap);
	va_end(ap);
	if (bp < (buf + ARRAY_SIZE(buf))) {
		*bp = 0;
	}
	return rv;
}

static inline bool prf_check(const char *s)
{
	return strcmp(s, buf) == 0;
}

static void test_noarg(void)
{
	int rc;

	rc = prf("noparams");
	zassert_true(prf_check("noparams"), NULL);
	zassert_equal(rc, sizeof("noparams") - 1U, "fail: %d", rc);

	rc = prf("%%");
	zassert_true(prf_check("%"), NULL);
	zassert_equal(rc, 1, "fail: %d", rc);
}

static void test_c(void)
{
	int rc;

	rc = prf("%c", 'a');
	zassert_true(prf_check("a"), NULL);
	zassert_equal(rc, 1, "fail: %d", rc);
}

static void test_d(void)
{
	int rc;

	rc = prf("%d/%d", -23, 45);
	zassert_true(prf_check("-23/45"), NULL);
	zassert_equal(rc, 6, "fail: %d", rc);

	rc = prf("%ld/%ld", -23L, 45L);
	zassert_true(prf_check("-23/45"), NULL);
	zassert_equal(rc, 6, "fail: %d", rc);

	if (IS_ENABLED(CONFIG_LIB_OS_PRF_LL_SUPPORT)) {
		rc = prf("%lld/%lld", -23LL, 45LL);
		zassert_true(prf_check("-23/45"), "got %s", buf);
		zassert_equal(rc, 6, "fail: %d", rc);
	} else {
		rc = prf("%lld/%lld", -23LL, 45LL);
		zassert_true(prf_check("%ld/%ld"), "got %s", buf);
		zassert_equal(rc, 7, "fail: %d", rc);
	}
}

static void test_x(void)
{
	int rc;

	rc = prf("%x/%X", 0x2c, 0x4a);
	zassert_true(prf_check("2c/4A"), NULL);
	zassert_equal(rc, 5, "fail: %d", rc);

	rc = prf("%lx/%lX", 0x2cL, 0x4aL);
	zassert_true(prf_check("2c/4A"), NULL);
	zassert_equal(rc, 5, "fail: %d", rc);

	if (IS_ENABLED(CONFIG_LIB_OS_PRF_LL_SUPPORT)) {
		rc = prf("%llx/%llX", 0x2cLL, 0x4aLL);
		zassert_true(prf_check("2c/4A"), "got %s", buf);
		zassert_equal(rc, 5, "fail: %d", rc);
	} else {
		rc = prf("%llx/%llX", 0x2cLL, 0x4aLL);
		zassert_true(prf_check("%lx/%lX"), "got %s", buf);
		zassert_equal(rc, 7, "fail: %d", rc);
	}
}

/*test case main entry*/
void test_main(void)
{
	TC_PRINT("Opts: "
		 COND_CODE_1(M64_MODE, ("m64"), ("m32"))
		 COND_CODE_1(CONFIG_LIB_OS_PRF_LL_SUPPORT, (" LL"), ())
		 "\n");
	ztest_test_suite(test_prf,
			 ztest_unit_test(test_noarg),
			 ztest_unit_test(test_c),
			 ztest_unit_test(test_d),
			 ztest_unit_test(test_x)
			 );
	ztest_run_test_suite(test_prf);
}
