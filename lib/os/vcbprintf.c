/*
 * Copyright (c) 1997-2010, 2012-2015 Wind River Systems, Inc.
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <toolchain.h>
#include <sys/byteorder.h>
#include <sys/types.h>
#include <sys/util.h>
#include <sys/vcbprintf.h>
#include <sys/printk.h>

#define OLD_VERSION 0

#ifndef EOF
#define EOF  -1
#endif

/* The maximum number of characters required to emit an integer value.
 *
 * The maximum is for octal formatting, which is unsigned but may have
 * a single byte alternative form prefix.  The buffer span does not
 * include EOS.
 */
#ifdef CONFIG_VCBPRINTF_64BIT_SUPPORT
/* The maximum defined standard integer type is 64 bits, so we assume
 * that's the number of bits in uintmax_t.  Some toolchains might
 * allow more, in which case the inconsistency needs to be
 * resolved.
 */
typedef intmax_t sint_value_type;
typedef uintmax_t uint_value_type;
#define CONVERTED_INT_BUFLEN ((64U + 2U) / 3U)
#else
typedef int32_t sint_value_type;
typedef uint32_t uint_value_type;
#define CONVERTED_INT_BUFLEN ((32U + 2U) / 3U)
#endif

/* The float code may extract up to 16 digits, plus a prefix, a
 * leading 0, a dot, and an exponent in the form e+xxx for a total of
 * 24. Add a trailing NULL so the buffer length required is 25.
 */
#define CONVERTED_FP_BUFLEN 25U

#ifdef CONFIG_VCBPRINTF_FP_SUPPORT
#define CONVERTED_BUFLEN CONVERTED_FP_BUFLEN
#else
#define CONVERTED_BUFLEN CONVERTED_INT_BUFLEN
#endif

/* The type used to store valid width and precision values.  255
 * characters is enough for reasonable padding, but some applications
 * may want the full range of an int.
 */
#ifdef CONFIG_VCBPRINTF_FULL_WIDTH
typedef unsigned int widthprec_type;
#else /* CONFIG_VCBPRINTF_FULL_WIDTH */
typedef uint8_t widthprec_type;
#endif /* CONFIG_VCBPRINTF_FULL_WIDTH */

/* The allowed types of length modifier. */
enum length_mod_enum {
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

/* Categories of conversion specifiers. */
enum convspec_cat_enum {
	/* unrecognized */
	CONVSPEC_INVALID,
	/* d, i */
	CONVSPEC_SINT,
	/* c, o, u, x, X */
	CONVSPEC_UINT,
	/* n, p, s */
	CONVSPEC_PTR,
	/* a, A, e, E, f, F, g, G */
	CONVSPEC_FP,
};

/* Case label to identify conversions for signed integral values.  The
 * corresponding argument_value tag is sint and category is
 * CONVSPEC_SINT.
 */
#define SINT_CONV_CASES				\
	'd':					\
	case 'i'

/* Case label to identify conversions for signed integral arguments.
 * The corresponding argument_value tag is uint and category is
 * CONVSPEC_UINT.
 */
#define UINT_CONV_CASES				\
	'c':					\
	case 'o':				\
	case 'u':				\
	case 'x':				\
	case 'X'

/* Case label to identify conversions for floating point arguments.
 * The corresponding argument_value tag is either dbl or ldbl,
 * depending on length modifier, and the category is CONVSPEC_FP.
 */
#define FP_CONV_CASES				\
	'a':					\
	case 'A':				\
	case 'e':				\
	case 'E':				\
	case 'f':				\
	case 'F':				\
	case 'g':				\
	case 'G'

/* Case label to identify conversions for pointer arguments.  The
 * corresponding argument_value tag is ptr and the category is
 * CONVSPEC_PTR.
 */
#define PTR_CONV_CASES				\
	'n':					\
	case 'p':				\
	case 's'

/* Storage for an argument value. */
union argument_value {
	/* For SINT conversions */
	sint_value_type sint;
	/* For UINT conversions */
	uint_value_type uint;
	/* For FP conversions without L length */
	double dbl;
	/* For FP conversions with L length */
	long double ldbl;
	/* For PTR conversions */
	void* ptr;
};

/* Structure capturing all attributes of a conversion
 * specification.
 *
 * Initial values come from the specification, but are updated during
 * the conversion.
 */
struct conversion {
	/** Indicates flags are inconsistent */
	bool invalid: 1;

	/** Indicates flags are valid but not supported */
	bool unsupported: 1;

	/** Left-justify value in width */
	bool flag_dash: 1;

	/** Explicit sign */
	bool flag_plus: 1;

	/** Space for non-negative sign */
	bool flag_space: 1;

	/** Alternative form */
	bool flag_hash: 1;

	/** Pad with leading zeroes */
	bool flag_zero: 1;

	/** Width field present */
	bool width_present: 1;

	/** Width value from int argument
	 *
	 * width_value is set to the absolute value of the argument.
	 * If the argument is negative flag_dash is also set.
	 */
	bool width_star: 1;

	/** Precision field present */
	bool prec_present: 1;

	/** Precision from int argument
	 *
	 * prec_value is set to the value of a non-negative argument.
	 * If the argument is negative prec_present is cleared.
	 */
	bool prec_star: 1;

	/** Length modifier (value from length_mod_enum) */
	unsigned int length_mod: 4;

	/** Conversion specifier category (value from convspec_cat_enum) */
	unsigned int convspec_cat: 3;

	/** Conversion specifier character */
	unsigned int convspec: 7;

	/** If set alternate form requires 0 before octal. */
	bool altform_0: 1;

	/** If set alternate form requires 0x before hex. */
	bool altform_0c: 1;

	/** Set for floating point values that have a non-zero
	 * pad0_pre_dp, pad0_post_dp, or pad0_pre_exp.
	 */
	bool pad_fp: 1;

	/** Width value from specification */
	widthprec_type width_value;

	/** Precision from specification */
	widthprec_type prec_value;

	/** Number of extra zeroes to be inserted before a formatted
	 * integer value due to precision and flag_zero.
	 *
	 * For example for zero-padded hexadecimal integers this would
	 * insert where the angle brackets are in: 0x<>hhhh
	 *
	 * Value is zero for non-integer conversions.
	 */
	widthprec_type pad0_prefix;

	/** Number of extra zeros to be inserted before a decimal
	 * point due to precision.
	 *
	 * Inserts at <> in: VVVV<>.FFFFeEE
	 */
	widthprec_type pad0_pre_dp;

	/** Number of extra zeros to be inserted after a decimal
	 * point due to precision.
	 *
	 * Inserts at <> in: VVVV.<>FFFFeEE
	 */
	widthprec_type pad0_post_dp;

	/** Number of extra zeros to be inserted after a decimal
	 * point due to precision.
	 *
	 * Inserts at <> in: VVVV.FFFF<>eEE
	 */
	widthprec_type pad0_pre_exp;
};

static size_t extract_decimal(const char **str)
{
	const char *sp = *str;
	size_t val = 0;

	while (isdigit((int)(unsigned char)*sp)) {
		val = 10U * val + *sp++ - '0';
	}
	*str = sp;
	return val;
}

static inline const char *extract_flags(struct conversion *conv,
					const char *sp)
{
	bool loop = true;

	do {
		switch (*sp) {
			case '-':
				conv->flag_dash = true;
				break;
			case '+':
				conv->flag_plus = true;
				break;
			case ' ':
				conv->flag_space = true;
				break;
			case '#':
				conv->flag_hash = true;
				break;
			case '0':
				conv->flag_zero = true;
				break;
			default:
				loop = false;
		}
		if (loop) {
			++sp;
		}
	} while (loop);

	/* zero && dash => !zero */
	if (conv->flag_zero && conv->flag_dash) {
		conv->flag_zero = false;
	}

	/* space && plus => !plus, handled in emitter code */

	return sp;
}

static inline const char *extract_width(struct conversion *conv,
					const char *sp)
{
	if (*sp == '*') {
		conv->width_present = true;
		conv->width_star = true;
		return ++sp;
	}

	const char *wp = sp;
	size_t width = extract_decimal(&sp);

	if (sp != wp) {
		conv->width_present = true;
		conv->width_value = width;
		if (width != conv->width_value) {
			/* Lost width data */
			conv->unsupported = true;
		}
	}

	return sp;
}

static inline const char *extract_prec(struct conversion *conv,
				       const char *sp)
{
	if (*sp != '.') {
		return sp;
	}
	++sp;

	if (*sp == '*') {
		conv->prec_present = true;
		conv->prec_star = true;
		return ++sp;
	}

	const char *wp = sp;
	size_t prec = extract_decimal(&sp);

	if (sp != wp) {
		conv->prec_present = true;
		conv->prec_value = prec;
		if (prec != conv->prec_value) {
			/* Lost precision data */
			conv->unsupported = true;
		}
	}

	return sp;
}

static inline const char *extract_length(struct conversion *conv,
					 const char *sp)
{
	switch (*sp) {
		case 'h':
			if (*++sp == 'h') {
				conv->length_mod = LENGTH_HH;
				++sp;
			} else {
				conv->length_mod = LENGTH_H;
			}
			break;
		case 'l':
			if (*++sp == 'l') {
				conv->length_mod = LENGTH_LL;
				++sp;
			} else {
				conv->length_mod = LENGTH_L;
			}
			break;
		case 'j':
			conv->length_mod = LENGTH_H;
			++sp;
			break;
		case 'z':
			conv->length_mod = LENGTH_Z;
			++sp;
			break;
		case 't':
			conv->length_mod = LENGTH_T;
			++sp;
			break;
		case 'L':
			conv->length_mod = LENGTH_UPPER_L;
			++sp;
			break;
		default:
			conv->length_mod = LENGTH_NONE;
			break;
	}
	return sp;
}

static inline const char *extract_convspec(struct conversion *conv,
					   const char *sp)
{
	conv->convspec = *sp++;

	switch (conv->convspec) {
		case SINT_CONV_CASES:
			conv->convspec_cat = CONVSPEC_SINT;
			goto int_conv;
		case UINT_CONV_CASES:
			conv->convspec_cat = CONVSPEC_UINT;
		int_conv:
			/* L length specifier not acceptable */
			if (conv->length_mod == LENGTH_UPPER_L) {
				conv->invalid = true;
			}

			/* For c LENGTH_NONE and LENGTH_L would be ok,
			 * but we don't support wide characters.
			 */
			if (conv->convspec == 'c') {
				conv->unsupported |= (conv->length_mod != LENGTH_NONE);
			}

			/* Disable conversion of j and ll length if we
			 * don't support 64-bit conversions.
			 */
			if (!IS_ENABLED(CONFIG_VCBPRINTF_64BIT_SUPPORT)
				&& ((conv->length_mod == LENGTH_LL)
				    || (conv->length_mod == LENGTH_J))) {
				conv->unsupported = true;
			}
			break;

		case FP_CONV_CASES:
			conv->convspec_cat = CONVSPEC_FP;

			/* Don't support if disabled, or if invalid
			 * length modifiers are present.
			 */
			if (!IS_ENABLED(CONFIG_VCBPRINTF_FP_SUPPORT)) {
				conv->unsupported = true;
			} else if ((conv->length_mod != LENGTH_NONE)
				   && (conv->length_mod != LENGTH_UPPER_L)) {
				conv->invalid = true;
			}

			break;

		/* PTR cases are distinct */
		case 'n':
			conv->convspec_cat = CONVSPEC_PTR;
			/* Anything except L */
			if (conv->length_mod == LENGTH_UPPER_L) {
				conv->unsupported = true;
			}
			break;

		case 's':
		case 'p':
			conv->convspec_cat = CONVSPEC_PTR;

			/* p: only LENGTH_NONE
			 *
			 * s: LENGTH_NONE or LENGTH_L but wide
			 * characters not supported.
			 */
			if (conv->length_mod != LENGTH_NONE) {
				conv->unsupported = true;
			}
			break;


		default:
			conv->invalid = true;
			break;
	}
	return sp;
}

static inline const char *extract_conversion(struct conversion *conv,
					     const char *sp)
{
	*conv = (struct conversion) {
	   .invalid = false,
	};

	/* Skip over the opening %.  If the conversion specifier is %,
	 * that's the only thing that should be there, so
	 * fast-exit.
	 */
	++sp;
	if (*sp == '%') {
		conv->convspec = *sp++;
		return sp;
	}

	sp = extract_flags(conv, sp);
	sp = extract_width(conv, sp);
	sp = extract_prec(conv, sp);
	sp = extract_length(conv, sp);
	sp = extract_convspec(conv, sp);

	return sp;
}


/** Get the number of int-sized objects required to provide the
 * arguments for the conversion.
 *
 * This has a role in the logging subsystem where the arguments must
 * be captured for formatting in another thread.
 *
 * If the conversion specifier is invalid the calculated length may
 * not match what was actually passed as arguments.
 */
static size_t conversion_args_words(const struct conversion *conv)
{
	enum convspec_cat_enum convspec_cat
		= (enum convspec_cat_enum)conv->convspec_cat;
	enum length_mod_enum length_mod
		= (enum length_mod_enum)conv->length_mod;
	size_t words = 0;

	/* If the conversion is invalid behavior is undefined.  What
	 * this does is try to consume the argument anyway, in hopes
	 * that subsequent valid arguments will format correctly.
	 */

	/* Percent has no arguments */
	if (conv->convspec == '%') {
		return words;
	}

	if (conv->width_star) {
		words += sizeof(int) / sizeof(int);
	}

	if (conv->prec_star) {
		words += sizeof(int) / sizeof(int);
	}

	if ((convspec_cat == CONVSPEC_SINT)
	    || (convspec_cat == CONVSPEC_UINT)) {
		/* The size of integral values is the same regardless
		 * of signedness.
		 */
		switch (length_mod) {
			case LENGTH_NONE:
			case LENGTH_HH:
			case LENGTH_H:
				words += sizeof(int) / sizeof(int);
				break;
			case LENGTH_L:
				words += sizeof(long) / sizeof(int);
				break;
			case LENGTH_LL:
				words += sizeof(long long) / sizeof(int);
				break;
			case LENGTH_J:
				words += sizeof(intmax_t) / sizeof(int);
				break;
			case LENGTH_Z:
				words += sizeof(size_t) / sizeof(int);
				break;
			case LENGTH_T:
				words += sizeof(ptrdiff_t) / sizeof(int);
				break;
			default:
				break;
		}
	} else if (convspec_cat == CONVSPEC_FP) {
		if (length_mod == LENGTH_UPPER_L) {
			words += sizeof(long double) / sizeof(int);
		} else {
			words += sizeof(double) / sizeof(int);
		}
	} else if (convspec_cat == CONVSPEC_PTR) {
		words += sizeof(void *) / sizeof(int);
	}

	return words;
}

/** Extract the conversion argument value.
 */
static void get_argument(const struct conversion *conv,
			 union argument_value *value,
			 va_list *app)
{
	enum convspec_cat_enum convspec_cat
		= (enum convspec_cat_enum)conv->convspec_cat;
	enum length_mod_enum length_mod
		= (enum length_mod_enum)conv->length_mod;

	*(value) = (union argument_value){
		.uint = 0,
	};

	/* The length modifier doesn't affect the value of a pointer
	 * argument.
	 */
	if (convspec_cat == CONVSPEC_SINT) {
		switch (length_mod) {
			default:
			case LENGTH_NONE:
			case LENGTH_HH:
			case LENGTH_H:
				value->sint = va_arg(*app, int);
				break;
			case LENGTH_L:
				value->sint = va_arg(*app, long);
				break;
			case LENGTH_LL:
				value->sint = (sint_value_type)va_arg(*app, long long);
				break;
			case LENGTH_J:
				value->sint = (sint_value_type)va_arg(*app, intmax_t);
				break;
			case LENGTH_Z:		/* size_t */
			case LENGTH_T:		/* ptrdiff_t */
				/* Though ssize_t is the signed equivalent of size_t
				 * for POSIX, there is no uptrdiff_t.  Assume that
				 * size_t and ptrdiff_t are the unsigned and signed
				 * equivalents of each other.  This can be checked in
				 * a platform test.
				 */
				value->sint = (sint_value_type)va_arg(*app, ptrdiff_t);
				break;
		}
		if (length_mod == LENGTH_HH) {
			value->sint = (char)value->sint;
		} else if (length_mod == LENGTH_H) {
			value->sint = (short)value->sint;
		}
	} else if (convspec_cat == CONVSPEC_UINT) {
		switch (length_mod) {
			default:
			case LENGTH_NONE:
			case LENGTH_HH:
			case LENGTH_H:
				value->uint = va_arg(*app, unsigned int);
				break;
			case LENGTH_L:
				value->uint = va_arg(*app, unsigned long);
				break;
			case LENGTH_LL:
				value->uint = (uint_value_type)va_arg(*app, unsigned long long);
				break;
			case LENGTH_J:
				value->uint = (uint_value_type)va_arg(*app, uintmax_t);
				break;
			case LENGTH_Z:		/* size_t */
			case LENGTH_T:		/* ptrdiff_t */
				value->uint = (uint_value_type)va_arg(*app, size_t);
				break;
		}
		if (length_mod == LENGTH_HH) {
			value->uint = (unsigned char)value->uint;
		} else if (length_mod == LENGTH_H) {
			value->uint = (unsigned short)value->uint;
		}
	} else if (convspec_cat == CONVSPEC_FP) {
		if (length_mod == LENGTH_UPPER_L) {
			value->ldbl = va_arg(*app, long double);
		} else {
			value->dbl = va_arg(*app, double);
		}
	} else if (convspec_cat == CONVSPEC_PTR) {
		value->ptr = va_arg(*app, void *);
	}
}

#ifdef CONFIG_VCBPRINTF_64BIT_SUPPORT
#define VALTYPE long long
#else
#define VALTYPE long
#endif

/*
 * Writes the specified number into the buffer in the given base,
 * using the digit characters 0-9a-z (i.e. base>36 will start writing
 * odd bytes).  Base larger than 16 will overrun buffer.
 */
static int _to_x(bool uc, char *buf, unsigned VALTYPE n, unsigned int base)
{
	static const char lclut[] = "0123456789abcdef";
	static const char uclut[] = "0123456789ABCDEF";
	const char *lut = uc ? uclut : lclut;
	char *start = buf;
	int len;

	do {
		unsigned int d = n % base;

		n /= base;
		*buf++ = lut[d];
	} while (n);

	*buf = 0;
	len = buf - start;

	for (buf--; buf > start; buf--, start++) {
		char tmp = *buf;
		*buf = *start;
		*start = tmp;
	}

	return len;
}

static int _to_hex(char *buf, unsigned VALTYPE value, bool alt_form, char prefix)
{
	int len;
	char *buf0 = buf;

	if (alt_form) {
		*buf++ = '0';
		*buf++ = 'x';
	}

	len = _to_x(prefix == 'X', buf, value, 16);

	return len + (buf - buf0);
}

static int _to_octal(char *buf, unsigned VALTYPE value, bool alt_form)
{
	char *buf0 = buf;

	if (alt_form) {
		*buf++ = '0';
		if (!value) {
			/* So we don't return "00" for a value == 0. */
			*buf++ = 0;
			return 1;
		}
	}
	return (buf - buf0) + _to_x(false, buf, value, 8);
}

static int _to_udec(char *buf, unsigned VALTYPE value)
{
	return _to_x(false, buf, value, 10);
}

static int _to_dec(char *buf, VALTYPE value, bool fplus, bool fspace)
{
	char *start = buf;

	if (value < 0) {
		*buf++ = '-';
		value = -value;
	} else if (fplus) {
		*buf++ = '+';
	} else if (fspace) {
		*buf++ = ' ';
	}

	return (buf + _to_udec(buf, value)) - start;
}

/* Divide by two and round up. */
static void _rlrshift(uint64_t *v)
{
	*v = (*v & 1) + (*v >> 1);
}

/*
 * Tiny integer divide-by-five routine.  The full 64 bit division
 * implementations in libgcc are very large on some architectures, and
 * currently nothing in Zephyr pulls it into the link.  So it makes
 * sense to define this much smaller special case here to avoid
 * including it just for printf.
 *
 * It works by iteratively dividing the most significant 32 bits of
 * the 64 bit value by 5.  This will leave a remainder of 0-4
 * (i.e. three significant bits), ensuring that the top 29 bits of the
 * remainder are zero for the next iteration.  Thus in the second
 * iteration only 35 significant bits remain, and in the third only
 * six.  This was tested exhaustively through the first ~10B values in
 * the input space, and for ~2e12 (4 hours runtime) random inputs
 * taken from the full 64 bit space.
 */
static void _ldiv5(uint64_t *v)
{
	uint32_t hi;
	uint64_t rem = *v, quot = 0U, q;
	int i;

	static const char shifts[] = { 32, 3, 0 };

	/*
	 * Usage in this file wants rounded behavior, not truncation.  So add
	 * two to get the threshold right.
	 */
	rem += 2U;

	for (i = 0; i < 3; i++) {
		hi = rem >> shifts[i];
		q = (uint64_t)(hi / 5U) << shifts[i];
		rem -= q * 5U;
		quot += q;
	}

	*v = quot;
}

static char _get_digit(uint64_t *fr, int *digit_count)
{
	char rval;

	if (*digit_count > 0) {
		--*digit_count;
		*fr *= 10U;
		rval = ((*fr >> 60) & 0xF) + '0';
		*fr &= (BIT64(60) - 1U);
	} else {
		rval = '0';
	}

	return rval;
}

/*
 *	_to_float
 *
 *	Convert a floating point # to ASCII.
 *
 *	Parameters:
 *		"buf"		Buffer to write result into.
 *		"double_temp"	# to convert (either IEEE single or double).
 *		"c"		The conversion type (one of e,E,f,g,G).
 *		"falt"		TRUE if "#" conversion flag in effect.
 *		"fplus"		TRUE if "+" conversion flag in effect.
 *		"fspace"	TRUE if " " conversion flag in effect.
 *		"precision"	Desired precision (negative if undefined).
 *		"zeropad"	To store padding info to be inserted later
 */

/*
 *	The following two constants define the simulated binary floating
 *	point limit for the first stage of the conversion (fraction times
 *	power of two becomes fraction times power of 10), and the second
 *	stage (pulling the resulting decimal digits outs).
 */

#define MAXFP1 0xFFFFFFFF
#define HIGHBIT64 BIT64(63)

/*
 * In the case of floats zero padding can occur in two of three
 * locations:
 *	xxxxxx<zero.predot>.<zero.postdot>
 * or:
 *	x.<zero.postdot>xxxxxx<zero.trail>[e+xx]
 */
struct zero_padding {
	uint8_t predot;
	uint8_t postdot;
	uint8_t trail;
};

static int _to_float(char *buf, uint64_t double_temp, char c,
		     bool falt, bool fplus, bool fspace, int precision,
		     struct zero_padding *zp)
{
	int decexp;
	int exp;
	bool sign;
	int digit_count;
	uint64_t fract;
	uint64_t ltemp;
	bool prune_zero;
	char *start = buf;

	exp = double_temp >> 52 & 0x7ff;
	fract = (double_temp << 11) & ~HIGHBIT64;
	sign = !!(double_temp & HIGHBIT64);

	if (sign) {
		*buf++ = '-';
	} else if (fplus) {
		*buf++ = '+';
	} else if (fspace) {
		*buf++ = ' ';
	}

	if (exp == 0x7ff) {
		if (!fract) {
			if (isupper((int)c)) {
				*buf++ = 'I';
				*buf++ = 'N';
				*buf++ = 'F';
			} else {
				*buf++ = 'i';
				*buf++ = 'n';
				*buf++ = 'f';
			}
		} else {
			if (isupper((int)c)) {
				*buf++ = 'N';
				*buf++ = 'A';
				*buf++ = 'N';
			} else {
				*buf++ = 'n';
				*buf++ = 'a';
				*buf++ = 'n';
			}
		}
		*buf = 0;
		return buf - start;
	}

	if (c == 'F') {
		c = 'f';
	}

	if ((exp | fract) != 0) {
		if (exp == 0) {
			/* this is a denormal */
			while (((fract <<= 1) & HIGHBIT64) == 0) {
				exp--;
			}
		}
		exp -= (1023 - 1);	/* +1 since .1 vs 1. */
		fract |= HIGHBIT64;
	}

	decexp = 0;
	while (exp <= -3) {
		while ((fract >> 32) >= (MAXFP1 / 5)) {
			_rlrshift(&fract);
			exp++;
		}
		fract *= 5U;
		exp++;
		decexp--;

		while ((fract >> 32) <= (MAXFP1 / 2)) {
			fract <<= 1;
			exp--;
		}
	}

	while (exp > 0) {
		_ldiv5(&fract);
		exp--;
		decexp++;
		while ((fract >> 32) <= (MAXFP1 / 2)) {
			fract <<= 1;
			exp--;
		}
	}

	while (exp < (0 + 4)) {
		_rlrshift(&fract);
		exp++;
	}

	if (precision < 0) {
		precision = 6;		/* Default precision if none given */
	}

	prune_zero = false;		/* Assume trailing 0's allowed     */
	if ((c == 'g') || (c == 'G')) {
		if (decexp < (-4 + 1) || decexp > precision) {
			c += 'e' - 'g';
			if (precision > 0) {
				precision--;
			}
		} else {
			c = 'f';
			precision -= decexp;
		}
		if (!falt && (precision > 0)) {
			prune_zero = true;
		}
	}

	if (c == 'f') {
		exp = precision + decexp;
		if (exp < 0) {
			exp = 0;
		}
	} else {
		exp = precision + 1;
	}
	digit_count = 16;
	if (exp > 16) {
		exp = 16;
	}

	ltemp = BIT64(59);
	while (exp--) {
		_ldiv5(&ltemp);
		_rlrshift(&ltemp);
	}

	fract += ltemp;
	if ((fract >> 32) & (0x0FU << 28)) {
		_ldiv5(&fract);
		_rlrshift(&fract);
		decexp++;
	}

	if (c == 'f') {
		if (decexp > 0) {
			while (decexp > 0 && digit_count > 0) {
				*buf++ = _get_digit(&fract, &digit_count);
				decexp--;
			}
			zp->predot = decexp;
			decexp = 0;
		} else {
			*buf++ = '0';
		}
		if (falt || (precision > 0)) {
			*buf++ = '.';
		}
		if (decexp < 0 && precision > 0) {
			zp->postdot = -decexp;
			if (zp->postdot > precision) {
				zp->postdot = precision;
			}
			precision -= zp->postdot;
		}
		while (precision > 0 && digit_count > 0) {
			*buf++ = _get_digit(&fract, &digit_count);
			precision--;
		}
		zp->trail = precision;
	} else {
		*buf = _get_digit(&fract, &digit_count);
		if (*buf++ != '0') {
			decexp--;
		}
		if (falt || (precision > 0)) {
			*buf++ = '.';
		}
		while (precision > 0 && digit_count > 0) {
			*buf++ = _get_digit(&fract, &digit_count);
			precision--;
		}
		zp->trail = precision;
	}

	if (prune_zero) {
		zp->trail = 0;
		while (*--buf == '0')
			;
		if (*buf != '.') {
			buf++;
		}
	}

	if ((c == 'e') || (c == 'E')) {
		*buf++ = c;
		if (decexp < 0) {
			decexp = -decexp;
			*buf++ = '-';
		} else {
			*buf++ = '+';
		}
		if (decexp >= 100) {
			*buf++ = (decexp / 100) + '0';
			decexp %= 100;
		}
		*buf++ = (decexp / 10) + '0';
		decexp %= 10;
		*buf++ = decexp + '0';
	}
	*buf = 0;

	return buf - start;
}

static int _atoi(const char **sptr)
{
	const char *p = *sptr - 1;
	int i = 0;

	while (isdigit((int)*p)) {
		i = 10 * i + *p++ - '0';
	}
	*sptr = p;
	return i;
}

int xsys_vcbprintf(sys_vcbprintf_cb func, void *dest, const char *format, va_list vargs)
{
	/*
	 * The work buffer has to accommodate for the largest data length.
	 * The max range octal length is one prefix + 3 bits per digit
	 * meaning 12 bytes on 32-bit and 23 bytes on 64-bit.
	 * The float code may extract up to 16 digits, plus a prefix,
	 * a leading 0, a dot, and an exponent in the form e+xxx for
	 * a total of 24. Add a trailing NULL so it is 25.
	 */
	char buf[25];
	char c;
	int count;
	char *cptr;
	bool falt, fminus, fplus, fspace, fzero;
	int i;
	int width, precision;
	int clen, prefix, zero_head;
	struct zero_padding zero;
	VALTYPE val;

#define PUTC(c)	do { if ((*func)(c, dest) == EOF) return EOF; } while (false)

	count = 0;

	while ((c = *format++)) {
		if (c != '%') {
			PUTC(c);
			count++;
		} else {
			fminus = fplus = fspace = falt = fzero = false;
			while (strchr("-+ #0", (c = *format++)) != NULL) {
				switch (c) {
				case '-':
					fminus = true;
					break;

				case '+':
					fplus = true;
					break;

				case ' ':
					fspace = true;
					break;

				case '#':
					falt = true;
					break;

				case '0':
					fzero = true;
					break;

				case '\0':
					return count;
				}
			}

			if (c == '*') {
				/* Is the width a parameter? */
				width = va_arg(vargs, int);
				if (width < 0) {
					fminus = true;
					width = -width;
				}
				c = *format++;
			} else if (!isdigit((int)c)) {
				width = 0;
			} else {
				width = _atoi(&format);	/* Find width */
				c = *format++;
			}

			precision = -1;
			if (c == '.') {
				c = *format++;
				if (c == '*') {
					precision = va_arg(vargs, int);
				} else {
					precision = _atoi(&format);
				}

				c = *format++;
			}

			/*
			 * This implementation only supports the following
			 * length modifiers:
			 *    h: short
			 *   hh: char
			 *    l: long
			 *   ll: long long
			 *    z: size_t or ssize_t
			 */
			i = 0;
			if (strchr("hlz", c) != NULL) {
				i = c;
				c = *format++;
				if (IS_ENABLED(CONFIG_VCBPRINTF_64BIT_SUPPORT) &&
				    i == 'l' && c == 'l') {
					i = 'L';
					c = *format++;
				} else if (i == 'h' && c == 'h') {
					i = 'H';
					c = *format++;
				}
			}

			cptr = buf;
			prefix = 0;
			zero.predot = zero.postdot = zero.trail = 0;

			switch (c) {
			case 'c':
				buf[0] = va_arg(vargs, int);
				clen = 1;
				precision = 0;
				break;

			case 'd':
			case 'i':
				switch (i) {
				case 'l':
					val = va_arg(vargs, long);
					break;
#ifdef CONFIG_VCBPRINTF_64BIT_SUPPORT
				case 'L':
					val = va_arg(vargs, long long);
					break;
#endif
				case 'z':
					val = va_arg(vargs, ssize_t);
					break;
				case 'h':
				case 'H':
				default:
					val = va_arg(vargs, int);
					break;
				}
				clen = _to_dec(buf, val, fplus, fspace);
				if (fplus || fspace || val < 0) {
					prefix = 1;
				}
				break;

			case 'e':
			case 'E':
			case 'f':
			case 'F':
			case 'g':
			case 'G':
			{
				/* standard platforms which supports double */
				union {
					double d;
					uint64_t i;
				} u;

				u.d = va_arg(vargs, double);
				if (!IS_ENABLED(CONFIG_VCBPRINTF_FP_SUPPORT)) {
					PUTC('%');
					PUTC(c);
					clen = 0;
					count += 2;
					continue;
				}

				clen = _to_float(buf, u.i, c, falt,
						 fplus, fspace, precision,
						 &zero);
				if (fplus || fspace || (buf[0] == '-')) {
					prefix = 1;
				}

				clen += zero.predot + zero.postdot + zero.trail;
				if (!isdigit((int)buf[prefix])) {
					/* inf or nan: no zero padding */
					fzero = false;
				}
				precision = -1;
				break;
			}

			case 'n':
				switch (i) {
				case 'h':
					*va_arg(vargs, short *) = count;
					break;
				case 'H':
					*va_arg(vargs, char *) = count;
					break;
				case 'l':
					*va_arg(vargs, long *) = count;
					break;
#ifdef CONFIG_VCBPRINTF_64BIT_SUPPORT
				case 'L':
					*va_arg(vargs, long long *) = count;
					break;
#endif
				case 'z':
					*va_arg(vargs, ssize_t *) = count;
					break;
				default:
					*va_arg(vargs, int *) = count;
					break;
				}
				continue;

			case 'p':
				val = (uintptr_t) va_arg(vargs, void *);
				clen = _to_hex(buf, val, true, 'x');
				prefix = 2;
				break;

			case 's':
				cptr = va_arg(vargs, char *);
				/* Get the string length */
				if (precision < 0) {
					precision = INT_MAX;
				}
				for (clen = 0; clen < precision; clen++) {
					if (cptr[clen] == '\0') {
						break;
					}
				}
				precision = 0;
				break;

			case 'o':
			case 'u':
			case 'x':
			case 'X':
				switch (i) {
				case 'l':
					val = va_arg(vargs, unsigned long);
					break;
#ifdef CONFIG_VCBPRINTF_64BIT_SUPPORT
				case 'L':
					val = va_arg(vargs, unsigned long long);
					break;
#endif
				case 'z':
					val = va_arg(vargs, size_t);
					break;
				case 'h':
				case 'H':
				default:
					val = va_arg(vargs, unsigned int);
					break;
				}
				if (c == 'o') {
					clen = _to_octal(buf, val, falt);
				} else if (c == 'u') {
					clen = _to_udec(buf, val);
				} else {
					clen = _to_hex(buf, val, falt, c);
					if (falt) {
						prefix = 2;
					}
				}
				break;

			case '%':
				PUTC('%');
				count++;
				continue;

			default:
				PUTC('%');
				PUTC(c);
				count += 2;
				continue;

			case 0:
				return count;
			}

			if (precision >= 0) {
				zero_head = precision - clen + prefix;
			} else if (fzero) {
				zero_head = width - clen;
			} else {
				zero_head = 0;
			}
			if (zero_head < 0) {
				zero_head = 0;
			}
			width -= clen + zero_head;

			/* padding for right justification */
			if (!fminus && width > 0) {
				count += width;
				while (width-- > 0) {
					PUTC(' ');
				}
			}

			/* data prefix */
			clen -= prefix;
			count += prefix;
			while (prefix-- > 0) {
				PUTC(*cptr++);
			}

			/* zero-padded head */
			count += zero_head;
			while (zero_head-- > 0) {
				PUTC('0');
			}

			/*
			 * main data:
			 *
			 * In the case of floats, 3 possible zero-padding
			 * are included in the clen count, either with
			 *	xxxxxx<zero.predot>.<zero.postdot>
			 * or with
			 *	x.<zero.postdot>xxxxxx<zero.trail>[e+xx]
			 * In the non-float cases, those predot, postdot and
			 * tail params are equal to 0.
			 */
			count += clen;
			if (zero.predot) {
				c = *cptr;
				while (isdigit((int)c)) {
					PUTC(c);
					clen--;
					c = *++cptr;
				}
				clen -= zero.predot;
				while (zero.predot-- > 0) {
					PUTC('0');
				}
			}
			if (zero.postdot) {
				do {
					c = *cptr++;
					PUTC(c);
					clen--;
				} while (c != '.');
				clen -= zero.postdot;
				while (zero.postdot-- > 0) {
					PUTC('0');
				}
			}
			if (zero.trail) {
				c = *cptr;
				while (isdigit((int)c) || c == '.') {
					PUTC(c);
					clen--;
					c = *++cptr;
				}
				clen -= zero.trail;
				while (zero.trail-- > 0) {
					PUTC('0');
				}
			}
			while (clen-- > 0) {
				PUTC(*cptr++);
			}

			/* padding for left justification */
			if (width > 0) {
				count += width;
				while (width-- > 0) {
					PUTC(' ');
				}
			}
		}
	}
	return count;

#undef PUTC
}

static int outs(sys_vcbprintf_cb out,
		void *ctx,
		const char *sp,
		const char *ep)
{
	size_t count = 0;

	while ((sp < ep) || ((ep == NULL) && *sp)) {
		int rc = out((int)*sp++, ctx);

		if (rc < 0) {
			return rc;
		}
		++count;
	}

	return (int)count;
}

static inline size_t conversion_radix(char convspec)
{
	switch (convspec) {
	default:
	case 'd':
	case 'i':
	case 'u':
		return 10;
	case 'o':
		return 8;
	case 'p':
	case 'x':
	case 'X':
		return 16;
	}
}

/* Writes the given value into the buffer in the specified base.
 *
 * Precision is applied *ONLY* within the space allowed.
 *
 * Alternate form value is applied to o, x, and X conversions.
 *
 * The buffer is filled backwards, so the input bpe is the end of the
 * generated representation.  The returned pointer is to the first
 * character of the representation.
 */
static char *encode_uint(uint_value_type value,
			 struct conversion *conv,
			 char *bps,
			 const char *bpe)
{
	static const char lclut[] = "0123456789abcdef";
	static const char uclut[] = "0123456789ABCDEF";
	const char *lut = (conv->convspec == 'X') ? uclut : lclut;
	const unsigned int radix = conversion_radix(conv->convspec);
	char *bp = bps + (bpe - bps);

	do {
		unsigned int lsv = (unsigned int)(value % radix);

		*--bp = lut[lsv];
		value /= radix;
	} while ((value != 0) && (bps < bp));

	/* Record required alternate forms.  This can be determined
	 * from the radix without re-checking convspec.
	 */
	if (conv->flag_hash) {
		if (radix == 8) {
			conv->altform_0 = true;
		} else if (radix == 16) {
			conv->altform_0c = true;
		}
	}

	return bp;
}


/* Synthesize a conversion specification from the decoded structure. */
static const char *encode_conversion(const struct conversion *conv)
{
	static char buf[64];
	char *bp = buf;
	const char *const bpe = bp + sizeof(buf);

	if (conv->invalid) {
		*bp++ = '!';
	} else if (conv->unsupported) {
		*bp++ = '?';
	} else {
		*bp++ = '%';
	}
	if (conv->flag_dash) {
		*bp++ = '-';
	}
	if (conv->flag_plus) {
		*bp++ = '+';
	}
	if (conv->flag_space) {
		*bp++ = ' ';
	}
	if (conv->flag_hash) {
		*bp++ = '#';
	}
	if (conv->flag_zero) {
		*bp++ = '0';
	}
	if (conv->width_present) {
		if (conv->width_star) {
			*bp++ = '*';
		} else {
			bp += snprintf(bp, bpe - bp, "%i", conv->width_value);
		}
	}
	if (conv->prec_present) {
		*bp++ = '.';
		if (conv->prec_star) {
			*bp++ = '*';
		} else {
			bp += snprintf(bp, bpe - bp, "%i", conv->prec_value);
		}
	}

	switch ((enum length_mod_enum)conv->length_mod) {
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

	*bp++ = conv->convspec;
	*bp = 0;

	return buf;
}

/*
 *	_to_float
 *
 *	Convert a floating point # to ASCII.
 *
 *	Parameters:
 *		"buf"		Buffer to write result into.
 *		"double_temp"	# to convert (either IEEE single or double).
 *		"c"		The conversion type (one of e,E,f,g,G).
 *		"falt"		TRUE if "#" conversion flag in effect.
 *		"fplus"		TRUE if "+" conversion flag in effect.
 *		"fspace"	TRUE if " " conversion flag in effect.
 *		"precision"	Desired precision (negative if undefined).
 *		"zeropad"	To store padding info to be inserted later
 */

static char *encode_float(double value,
			  struct conversion *conv,
			  int precision,
			  char *bps,
			  const char **bpe)
{
	const uint32_t MAX_FP1 = UINT32_MAX;
	const uint64_t BIT_63 = BIT64(63);

	union {
		uint64_t u64;
		double dbl;
	} u = {
		.dbl = value,
	};
	char *buf = bps;

	/* Prepend the sign: '-' if negative, flags control
	 * non-negative behavior.
	 */
	if ((u.u64 & BIT_63) != 0U) {
		*buf++ = '-';
	} else if (conv->flag_plus) {
		*buf++ = '+';
	} else if (conv->flag_space) {
		*buf++ = ' ';
	}

	/* Extract the non-negative 11-bit offset exponent, and get
	 * the fraction as a 63-bit value (masking off the upper bit
	 * left over from the exponent).
	 */
	char c = conv->convspec;
	int exp = u.u64 >> 52 & 0x7ff;
	uint64_t fract = (u.u64 << 11) & ~BIT_63;

	/* Exponent of all-ones signals infinity or NaN, which are
	 * text constants.
	 */
	if (exp == BIT_MASK(12)) {
		if (fract == 0) {
			if (isupper((int)c)) {
				*buf++ = 'I';
				*buf++ = 'N';
				*buf++ = 'F';
			} else {
				*buf++ = 'i';
				*buf++ = 'n';
				*buf++ = 'f';
			}
		} else {
			if (isupper((int)c)) {
				*buf++ = 'N';
				*buf++ = 'A';
				*buf++ = 'N';
			} else {
				*buf++ = 'n';
				*buf++ = 'a';
				*buf++ = 'n';
			}
		}

		/* No zero-padding with text values */
		conv->flag_zero = false;

		*bpe = buf;
		return bps;
	}

	/* Case of the conversion specifier F is no longer relevant. */
	if (c == 'F') {
		c = 'f';
	}

	/* Non-zero values need normalization. */
	if ((exp | fract) != 0) {
		if (exp == 0) {
			/* Fraction is subnormal.  Normalize it and
			 * correct the exponent.
			 */
			while (((fract <<= 1) & BIT_63) == 0) {
				exp--;
			}
		}
		/* Adjust the offset exponent to be signed rather than
		 * offset, and set the implicit 1 bit in the (shifted)
		 * 53-bit fraction.
		 */
		exp -= (1023 - 1);	/* +1 since .1 vs 1. */
		fract |= BIT_63;
	}

	/* Magically convert the base-2 exponent to a base-10
	 * exponent.
	 */
	int decexp = 0;

	while (exp <= -3) {
		while ((fract >> 32) >= (MAX_FP1 / 5)) {
			_rlrshift(&fract);
			exp++;
		}
		fract *= 5U;
		exp++;
		decexp--;

		while ((fract >> 32) <= (MAX_FP1 / 2)) {
			fract <<= 1;
			exp--;
		}
	}

	while (exp > 0) {
		_ldiv5(&fract);
		exp--;
		decexp++;
		while ((fract >> 32) <= (MAX_FP1 / 2)) {
			fract <<= 1;
			exp--;
		}
	}

	while (exp < (0 + 4)) {
		_rlrshift(&fract);
		exp++;
	}

	bool prune_zero = false;

	if ((c == 'g') || (c == 'G')) {
		/* Use the specified precision and exponent to select
		 * the representation and correct the precision and
		 * zero-pruning in accordance with the ISO C rule.
		 */
		if (decexp < (-4 + 1) || decexp > precision) {
			c += 'e' - 'g';  /* e or E */
			if (precision > 0) {
				precision--;
			}
		} else {
			c = 'f';
			precision -= decexp;
		}
		if (!conv->flag_hash && (precision > 0)) {
			prune_zero = true;
		}
	}

	if (c == 'f') {
		exp = precision + decexp;
		if (exp < 0) {
			exp = 0;
		}
	} else {
		exp = precision + 1;
	}

	int digit_count = 16;

	if (exp > 16) {
		exp = 16;
	}

	uint64_t ltemp = BIT64(59);

	while (exp--) {
		_ldiv5(&ltemp);
		_rlrshift(&ltemp);
	}

	fract += ltemp;
	if ((fract >> 32) & (0x0FU << 28)) {
		_ldiv5(&fract);
		_rlrshift(&fract);
		decexp++;
	}

	if (c == 'f') {
		if (decexp > 0) {
			/* Emit the digits above the decimal point. */
			while (decexp > 0 && digit_count > 0) {
				*buf++ = _get_digit(&fract, &digit_count);
				decexp--;
			}

			conv->pad0_pre_dp = decexp;

			decexp = 0;
		} else {
			*buf++ = '0';
		}

		/* Emit the decimal point only if required by the
		 * alternative format, or if more digits are to
		 * follow.
		 */
		if (conv->flag_hash || (precision > 0)) {
			*buf++ = '.';
		}

		if (decexp < 0 && precision > 0) {
			conv->pad0_post_dp = -decexp;
			if (conv->pad0_post_dp > precision) {
				conv->pad0_post_dp = precision;
			}

			precision -= conv->pad0_post_dp;
		}
	} else { // e or E
		/* Emit the one digit before the decimal.  If it's not
		 * zero, this is significant so reduce the base-10
		 * exponent.
		 */
		*buf = _get_digit(&fract, &digit_count);
		if (*buf++ != '0') {
			decexp--;
		}

		/* Emit the decimal point only if required by the
		 * alternative format, or if more digits are to
		 * follow.
		 */
		if (conv->flag_hash || (precision > 0)) {
			*buf++ = '.';
		}
	}

	while (precision > 0 && digit_count > 0) {
		*buf++ = _get_digit(&fract, &digit_count);
		precision--;
	}

	conv->pad0_pre_exp = precision;

	if (prune_zero) {
		conv->pad0_pre_exp = 0;
		while (*--buf == '0') {
			;
		}
		if (*buf != '.') {
			buf++;
		}
	}

	/* Emit the explicit exponent, if format requires it. */
	if ((c == 'e') || (c == 'E')) {
		*buf++ = c;
		if (decexp < 0) {
			decexp = -decexp;
			*buf++ = '-';
		} else {
			*buf++ = '+';
		}

		/* At most 3 digits to the decimal.  Spit them out. */
		if (decexp >= 100) {
			*buf++ = (decexp / 100) + '0';
			decexp %= 100;
		}

		*buf++ = (decexp / 10) + '0';
		*buf++ = (decexp % 10) + '0';
	}

	/* Cache whether there's padding required */
	conv->pad_fp = (conv->pad0_pre_dp > 0)
		|| (conv->pad0_post_dp > 0)
		|| (conv->pad0_pre_exp > 0);

	/* Set the end of the encoded sequence, and return its start.
	 * Also store EOS as a non-digit/non-decimal value so we don't
	 * have to check against bpe when iterating in multiple
	 * places.
	 */
	*bpe = buf;
	*buf = 0;
	return bps;
}

static inline void store_count(const struct conversion *conv,
			       const union argument_value *value,
			       size_t count)
{
	void *dp = value->ptr;

	switch ((enum length_mod_enum)conv->length_mod) {
	case LENGTH_NONE:
		*(int*)dp = (int)count;
		break;
	case LENGTH_HH:
		*(signed char*)dp = (signed char)count;
		break;
	case LENGTH_H:
		*(short*)dp = (short)count;
		break;
	case LENGTH_L:
		*(long*)dp = (long)count;
		break;
	case LENGTH_LL:
		*(long long*)dp = (long long)count;
		break;
	case LENGTH_J:
		*(intmax_t*)dp = (intmax_t)count;
		break;
	case LENGTH_Z:
		*(size_t*)dp = (size_t)count;
		break;
	case LENGTH_T:
		*(ptrdiff_t*)dp = (ptrdiff_t)count;
		break;
	default:
		break;
	}
}

int sys_vcbprintf(sys_vcbprintf_cb out, void *ctx, const char *fp, va_list ap)
{
	char buf[CONVERTED_BUFLEN];
	size_t count = 0;

/* Output character, returning EOF if output failed, otherwise
 * updating count.
 *
 * NB: c is evaluated exactly once: side-effects are OK */
#define OUTC(c) do { \
	if ((*out)((int)(c), ctx) < 0) { \
		return EOF; \
	} \
	++count; \
} while (false)

/* Output sequence of characters, returning EOF if output failed.
 *
 * NB: c is evaluated exactly once: side-effects are OK */

#define OUTS(_sp, _ep) do { \
	int rc = outs(out, ctx, _sp, _ep); \
	\
	if (rc == EOF) { \
		return EOF; \
	} \
	count += rc; \
} while (false)

	while (*fp != 0) {
		if (*fp != '%') {
			OUTC(*fp++);
			continue;
		}

		const char *sp = fp;
		struct conversion conv;
		union argument_value value;
		int width = -1;
		int precision = -1;
		const char *bps = NULL;
		const char *bpe = buf + sizeof(buf);
		char *bp = NULL;
		char sign = 0;

		fp = extract_conversion(&conv, sp);

		/* If dynamic width is specified, process it,
		 * otherwise set with if present.
		 */
		if (conv.width_star) {
			width = va_arg(ap, int);

			if (width < 0) {
				conv.flag_dash = true;
				width = -width;
			}
		} else if (conv.width_present) {
			width = conv.width_value;
		}

		/* If dynamic precision is specified, process it,
		 * otherwise set precision if present.  For floating
		 * point where precision is not present use 6.
		 */
		if (conv.prec_star) {
			int arg = va_arg(ap, int);

			if (arg < 0) {
				conv.prec_present = false;
			} else {
				precision = arg;
			}
		} else if (conv.prec_present) {
			precision = conv.prec_value;
		}

		if ((conv.convspec_cat == CONVSPEC_FP) && !conv.prec_present) {
			precision = 6;
		}

		/* Get the value to be converted from the args */
		get_argument(&conv, &value, &ap);

		/* We've now consumed all arguments related to this
		 * specification.  If the conversion is invalid, or is
		 * something we don't support, then output the
		 * original specification and move on.
		 */
		if (conv.invalid || conv.unsupported) {
			OUTS(sp, fp);
			continue;
		}

		/* Do formatting, either into the buffer or
		 * referencing external data.
		 */
		switch (conv.convspec) {
		case '%':
			OUTC('%');
			break;
		case 's': {
			bps = (const char*)value.ptr;

			size_t len = strlen(bps);

			if ((precision >= 0)
			    && ((size_t)precision < len)) {
				len = (size_t)precision;
			}

			bpe = bps + len;
			precision = -1;

			break;
		}
		case 'c':
			bps = buf;
			buf[0] = value.uint;
			bpe = buf + 1;
			break;
		case 'd':
		case 'i':
			if (conv.flag_plus) {
				sign = '+';
			} else if (conv.flag_space) {
				sign = ' ';
			}

			if (value.sint < 0) {
				sign = '-';
				value.uint = -value.sint;
			}

			__fallthrough;
		case 'o':
		case 'u':
		case 'x':
		case 'X':
			bp = encode_uint(value.uint, &conv, buf, bpe);
			if (sign != 0) {
				*--bp = sign;
			}

			bps = bp;

			/* Update pad0 values based on precision and
			 * converted length.
			 */
			if (precision >= 0) {
				size_t len = bpe - bps;

				/* Zero-padding flag is ignored for
				 * integer conversions with precision.
				 */
				conv.flag_zero = false;

				/* Set pad0_prefix to satisfy precision */
				if (len < (size_t)precision) {
					conv.pad0_prefix = precision - (int)len;
				}
			}

			break;
		case 'p':
			/* Implementation-defined: null is "nil",
			 * non-null has 0x prefix and pad0 to full
			 * address range. */
			value.uint = (uintptr_t)value.ptr;

			if ((value.ptr != NULL)
			    || !IS_ENABLED(CONFIG_VCBPRINTF_NULL_AS_NIL)) {
				bp = encode_uint((uintptr_t)value.ptr, &conv, buf, bpe);
				bps = bp;
				precision = 2 * sizeof(void *);

				/* Use 0x prefix */
				conv.altform_0c = true;
				conv.convspec = 'x';

				/* Zero-pad to address length */
				conv.pad0_prefix = (2 * sizeof(void*)) - (bpe - bps);
			} else {
				bps = "nil";
				bpe = bps + 3;
			}

			break;
		case 'n':
			if (IS_ENABLED(CONFIG_VCBPRINTF_N_SPECIFIER)) {
				store_count(&conv, &value, count);
			}

			break;

		case FP_CONV_CASES:
			if (IS_ENABLED(CONFIG_VCBPRINTF_FP_SUPPORT)) {
				bps = encode_float(value.dbl, &conv, precision, buf, &bpe);

				if (conv.flag_plus || conv.flag_space || (*bps == '-')) {
				}
			}
			break;
		}

		/* If we don't have a converted value to emit, move
		 * on.
		 */
		if (bps == NULL) {
			continue;
		}

		/* The converted value is now stored in [bps, bpe),
		 * excluding pad0 padding.
		 *
		 * The unjustified output will be:
		 *
		 * * any altform prefix
		 * * any pad0_prefix
		 * * for floats:
		 *   * any pre-decimal content from the converted value
		 *   * any pad0_pre_dp padding
		 *   * any decimal point in the converted value
		 *   * any pad0_post_dp padding
		 *   * any pre-exponent content from the converted value
		 *   * any pad0_pre_exp padding
		 *   * any exponent content from the converted value
		 * * for non-floats:
		 *   * the converted value
		 */
		size_t nj_len = (bpe - bps);
		int pad_len = 0;
		char pad = conv.flag_zero ? '0' : ' ';

		if (conv.altform_0c) {
			nj_len += 2U;
		} else if (conv.altform_0) {
			nj_len += 1U;
		}
		nj_len += conv.pad0_prefix;
		if (conv.pad_fp) {
			nj_len += conv.pad0_pre_dp;
			nj_len += conv.pad0_post_dp;
			nj_len += conv.pad0_pre_exp;
		}

		/* If we have a width, emit padding now for
		 * right-justified content, otherwise update width to
		 * hold the padding we need for left-justified
		 * output.
		 */
		if (width > 0) {
			width -= (int)nj_len;

			if (!conv.flag_dash) {
				while (width-- > 0) {
					OUTC(pad);
				}
			}
		}

		if (IS_ENABLED(CONFIG_VCBPRINTF_FP_SUPPORT) && conv.pad_fp) {
			const char *cp = bps;

			while (isdigit((int)*cp)) {
				OUTC(*cp++);
			}

			pad_len = conv.pad0_pre_dp;
			while (pad_len-- > 0) {
				OUTC('0');
			}

			if (*cp == '.') {
				OUTC(*cp++);
				pad_len = conv.pad0_post_dp;
				while (pad_len-- > 0) {
					OUTC('0');
				}
			}
			while (isdigit((int)*cp)) {
				OUTC(*cp++);
			}

			pad_len = conv.pad0_pre_exp;
			while (pad_len-- > 0) {
				OUTC('0');
			}

			OUTS(cp, bpe);
		} else {
			if (conv.altform_0c | conv.altform_0) {
				OUTC('0');
			}

			if (conv.altform_0c) {
				OUTC(conv.convspec);
			}

			pad_len = conv.pad0_prefix;
			while (pad_len-- > 0) {
				OUTC('0');
			}

			OUTS(bps, bpe);
		}

		while (width > 0) {
			OUTC(' ');
			--width;
		}

	}

	return count;
#undef OUTS
#undef OUTC
}

/* Wrapper providing the old API. */
int z_prf(int (*func)(), void *dest, const char *format, va_list vargs)
{
	return sys_vcbprintf(func, dest, format, vargs);
}
