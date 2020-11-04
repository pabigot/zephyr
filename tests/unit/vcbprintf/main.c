/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ztest.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <sys/vcbprintf.h>
#include <sys/util.h>
#include "../../../lib/os/vcbprintf.c"

#if __WORDSIZE == 64
#define M64_MODE 1
#endif

#define PFX_VAL (unsigned int)0x7b6b5b4b3b2b1b0b
#define SFX_VAL (unsigned int)0xe7e6e5e4e3e2e1e0

static const char pfx_str[] = COND_CODE_1(M64_MODE,("7b6b5b4b"),()) "3b2b1b0b";
static const char sfx_str[] = COND_CODE_1(M64_MODE,("e7e6e5e4"),()) "e3e2e1e0";

#define WRAP_FMT(_fmt) "%x" _fmt "%x"
#define PASS_ARG(_arg) PFX_VAL, _arg, SFX_VAL

static inline int match_str(const char **strp,
			    const char *expected,
			    size_t len)
{
	const char *str = *strp;
	int rv =  strncmp(str, expected, len);

	*strp += len;
	printf("%s vs %s for %zu: %d\n", expected, str, len, rv);
	return rv;
}

static inline int match_pfx(const char **ptr)
{
	return match_str(ptr, pfx_str, 2U * sizeof(PFX_VAL));
}

static inline int match_sfx(const char **ptr)
{
	return match_str(ptr, sfx_str, 2U * sizeof(SFX_VAL));
}

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
	if (false)  {
		rv = sys_vcbprintf(out, buf, format, ap);
		if (bp < (buf + ARRAY_SIZE(buf))) {
			*bp = 0;
		}
	} else {
		rv = vsnprintf(buf, sizeof(buf), format, ap);
	}
	va_end(ap);
	return rv;
}

#define TEST_PRF(_fmt, ...) prf(WRAP_FMT(_fmt), PASS_ARG(__VA_ARGS__))

static inline bool prf_check(const char *expected,
			     int rv)
{
	const char *str = buf;
	const char *sp = str;
	int rc = match_pfx(&str);
	int len;

	if (rc != 0) {
		printf("pfx failed at %s\n", sp);
		return false;
	}

	sp = str;
	rc = match_str(&str, expected, strlen(expected));
	if (rc != 0) {
		printf("str %s failed at %s\n", expected, sp);
		return false;
	}

	sp = str;
	rc = match_sfx(&str);
	if (rc != 0) {
		printf("sfx failed at %s\n", sp);
		return false;
	}

	rc = (*str != 0);
	if (rc != 0) {
		printf("eos failed at %s\n", str);
		return false;
	}

	len = (int)(str - buf);
	if (rv != len) {
		printf("rv failed %d != %d\n", rv, len);
		return false;
	}

	return true;
}

static void test_noarg(void)
{
	int rc;

	rc = prf("noparams");
	zassert_true(prf_check("noparams", rc), NULL);

	rc = prf("%%");
	zassert_true(prf_check("%", rc), NULL);
}

static void test_c(void)
{
	int rc;

	rc = TEST_PRF("%c", 'a');
	zassert_true(prf_check("a", rc), NULL);
}

static void test_d(void)
{
	int rc;

	rc = prf("%d/%d", -23, 45);
	zassert_true(prf_check("-23/45", rc), NULL);

	rc = prf("%ld/%ld", -23L, 45L);
	zassert_true(prf_check("-23/45", rc), NULL);

	if (IS_ENABLED(CONFIG_LIB_OS_PRF_LL_SUPPORT)) {
		rc = prf("%lld/%lld", -23LL, 45LL);
		zassert_true(prf_check("-23/45", rc), "got %s", buf);
	} else {
		rc = prf("%lld/%lld", -23LL, 45LL);
		zassert_true(prf_check("%ld/%ld", rc), "got %s", buf);
	}
}

static void test_x(void)
{
	int rc;

	rc = prf("%x/%X", 0x2c, 0x4a);
	zassert_true(prf_check("2c/4A", rc), NULL);

	rc = prf("%lx/%lX", 0x2cL, 0x4aL);
	zassert_true(prf_check("2c/4A", rc), NULL);

	if (IS_ENABLED(CONFIG_LIB_OS_PRF_LL_SUPPORT)) {
		rc = prf("%llx/%llX", 0x2cLL, 0x4aLL);
		zassert_true(prf_check("2c/4A", rc), "got %s", buf);
	} else {
		rc = prf("%llx/%llX", 0x2cLL, 0x4aLL);
		zassert_true(prf_check("%lx/%lX", rc), "got %s", buf);
	}
}

static void test_nop(void)
{
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
//			 ztest_unit_test(test_d),
//			 ztest_unit_test(test_x),
			 ztest_unit_test(test_nop)
			 );
	ztest_run_test_suite(test_prf);
}
