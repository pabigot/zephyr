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

enum length_field {
	LENGTH_NONE,
	LENGTH_HH,
	LENGTH_H,
	LENGTH_L,
	LENGTH_LL,
	LENGTH_J,
	LENGTH_Z,
	LENGTH_T,
	LENGTH_UPPER_L,
};

struct conversion {
	bool invalid: 1;
	bool flag_dash: 1;
	bool flag_plus: 1;
	bool flag_space: 1;
	bool flag_hash: 1;
	bool flag_zero: 1;
	bool width_present: 1;
	bool width_star: 1;
	unsigned int width_val: 9;
	bool prec_present: 1;
	bool prec_star: 1;
	unsigned int prec_val: 6;
	unsigned int length_enum: 4;
	unsigned int conversion: 7;
};

static size_t extract_size(const char **sp)
{
	size_t val = 0;

	while (isdigit((int)*sp)) {
		val = 10U * val + *sp++ - '0';
	}
	return val;
}

static inline char *extract_flags(struct conversion *cp,
				  const char *sp)
{
	bool loop = true;

	do {
		switch (*sp) {
		case '-':
			cp->flag_dash = true;
			break;
		case '+':
			cp->flag_plus = true;
			break;
		case ' ':
			cp->flag_space = true;
			break;
		case '#':
			cp->flag_hash = true;
			break;
		case '0':
			cp->flag_zero = true;
			break;
		default:
			loop = false;
		}
		if (loop) {
			++sp;
		}
	} while (loop);

	return sp;
}

static inline char *extract_width(struct conversion *cp,
				  const char *sp)
{
	if (*sp == '*') {
		cp->width_present = true;
		cp->width_star = true;
		return ++sp;
	}

	const char *wp = sp;
	size_t width = extract_size(&sp);

	if (sp != wp) {
		cp->width_present = true;
		cp->width = width;
		if (width != cp->width) {
			cp->invalid = true;
		}
	}

	return sp;
}

static inline char *extract_prec(struct conversion *cp,
				 const char *sp)
{
	if (*sp != '.') {
		return sp;
	}
	++sp;

	if (*sp == '*') {
		cp->prec_present = true;
		cp->prec_star = true;
		return ++sp;
	}

	const char *wp = sp;
	size_t prec = extract_size(&sp);

	if (sp != wp) {
		cp->prec_present = true;
		cp->prec = prec;
		if (prec != cp->prec) {
			cp->invalid = true;
		}
	}

	return sp;
}

static inline char *extract_length(struct conversion *cp,
				   const char *sp)
{
	switch (*sp) {
	case 'h':
		if (++sp == 'h') {
			cp->length_enum = LENGTH_HH;
			++sp;
		} else {
			cp->length_enum = LENGTH_H;
		}
		break;
	case 'l':
		if (++sp == 'l') {
			cp->length_enum = LENGTH_LL;
			++sp;
		} else {
			cp->length_enum = LENGTH_L;
		}
		break;
	case 'j':
		cp->length_enum = LENGTH_H;
		++sp;
		break;
	case 'z':
		cp->length_enum = LENGTH_Z;
		++sp;
		break;
	case 't':
		cp->length_enum = LENGTH_T;
		++sp;
		break;
	case 'L':
		cp->length_enum = LENGTH_UPPER_L;
		++sp;
		break;
	default:
		cp->length_enum = LENGTH_NONE;
		break;
	}
	return sp;
}

static inline char *extract_convspec(struct conversion *cp,
				     const char *sp)
{
	cp->conversion = *sp++;

	switch (cp->conversion) {
	case 'd':
	case 'i':
	case 'o':
	case 'u':
	case 'x':
	case 'X':
	case 'n':
		/* L length specifier not acceptable */
		if (cp->length_enum == LENGTH_UPPER_L) {
			cp->invalid = true;
		}
		break;

	case 'c':
	case 's':
	case 'p':
		/* No support for wide characters (c, s),
		 * or pointer */
		if (cp->length_enum != LENGTH_NONE) {
			cp->invalid = true;
		}
		break;

	case 'a':
	case 'A':
	case 'e':
	case 'E':
	case 'f':
	case 'F':
	case 'g':
	case 'G':
		/* Only L and empty length specifiers accepted */
		if ((cp->length_enum != LENGTH_NONE)
		    && (cp->length_enum != LENGTH_UPPER_L)) {
			cp->invalid = true;
		}
		break;

	}
	return sp;
}

static inline char *extract_conversion(struct conversion *cp,
				       const char *sp)
{
	*cp = (struct conversion) {
		invalid = 0,
	};

	/* Skip over the opening %.  If the conversion specifier is %,
	 * that's the only thing that should be there, so
	 * fast-exit.
	 */
	++sp;
	if (*sp == '%') {
		cp->convspec = *sp++;
		return sp;
	}

	sp = extract_flags(cp, sp);
	sp = extract_width(cp, sp);
	sp = extract_prec(cp, sp);
	sp = extract_length(cp, sp);
	sp = extract_convspec(cp, sp);

	return sp;
}

static const char *decode_conversion(const struct conversion *cp)
{
	static char buf[32];
	char *bp = buf;
	const char *const bpe = bp + sizeof(buf);

	if (cp->invalid) {
		*bp++ = '!';
	} else {
		*bp++ = '%';
	}
	if (cp->flag_dash) {
		*bp++ = '-';
	}
	if (cp->flag_plus) {
		*bp++ = '+';
	}
	if (cp->flag_space) {
		*bp++ = ' ';
	}
	if (cp->flag_hash) {
		*bp++ = '#';
	}
	if (cp->flag_0) {
		*bp++ = '0';
	}
	if (cp->width_present) {
		if (cp->width_star) {
			*bp++ = '*';
		} else {
			bp += snprintf(bp, bpe - bp, "%i", cp->width);
		}
	}
	if (cp->prec_present) {
		*bp++ = '.';
		if (cp->prec_star) {
			*bp++ = '*';
		} else {
			bp += snprintf(bp, bpe - bp, "%i", cp->prec);
		}
	}

	switch ((enum length_field)cp->length_enum) {
	default:
	case LENGTH_NONE:
		break;
	case LENGTH_HH:
		*bp++ = 'h';
		__fallthrough;
	case LENGTH_H:
		*bp++ = 'h';
		break;
	case LENGTH_LL:
		*bp++ = 'l';
		__fallthrough;
	case LENGTH_L:
		*bp++ = 'l';
		break;
	case LENGTH_J:
		*bp++ = 'j';
		break;
	case LENGTH_Z:
		*bp++ = 'z';
		break;
	case LENGTH_T:
		*bp++ = 't';
		break;
	case LENGTH_UPPER_L:
		*bp++ = 'L';
		break;
	}

	*bp++ = cp->conversion;
	*bp = 0;

	return buf;
}

static size_t conversion_word_length(const struct conversion *cp)
{
	if (cp->convspec == '%') {
		return 0;
	}

	switch ((enum length_field)cp->length_enum) {
	case LENGTH_UPPER_L:
		return sizeof(long double) / sizeof(int);
	case LENGTH_L:
		return sizeof(long) / sizeof(int);
	case LENGTH_LL:
		return sizeof(long long) / sizeof(int);
	case LENGTH_J:
		return sizeof(intmax_t) / sizeof(int);
	case LENGTH_Z:
		return sizeof(size_t) / sizeof(int);
	case LENGTH_T:
		return sizeof(ptrdiff_t) / sizeof(int);
	default:
		break;
	}

	return 1U;
}

static void process_string (const char *sp)
{
	const char *const ssp = sp;

	while (*sp) {
		if (*sp == '%') {
			struct conversion conv;
			const char *csp = sp;

			sp = extract_conversion(&conv, csp);
			printf("@%zu: %s\n", csp - ssp, decode_conversion(&conv));
		} else {
			sp++;
		}
	}
}


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
