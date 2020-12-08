/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SYS_CBPRINTF_H_
#define ZEPHYR_INCLUDE_SYS_CBPRINTF_H_

#include <stdarg.h>
#include <stddef.h>
#include <toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup cbprintf_apis Formatted Output APIs
 * @ingroup support_apis
 * @{
 */

/* Provide typedefs used for signed and unsigned integral types
 * capable of holding all convertable integral values.
 */
#ifdef CONFIG_CBPRINTF_FULL_INTEGRAL
typedef intmax_t cbprintf_sint_value_type;
typedef uintmax_t cbprintf_uint_value_type;
#else
typedef int32_t cbprintf_sint_value_type;
typedef uint32_t cbprintf_uint_value_type;
#endif

/** @brief Categories of conversion specifiers.
 *
 * These determine which tag in cbprintf_argument_value contains the
 * argument value.  The category is available in cbprintf_*/
enum cbprintf_specifier_cat_enum {
	/* unrecognized */
	CBPRINTF_SPECIFIER_INVALID,
	/* d, i */
	CBPRINTF_SPECIFIER_SINT,
	/* c, o, u, x, X */
	CBPRINTF_SPECIFIER_UINT,
	/* n, p, s */
	CBPRINTF_SPECIFIER_PTR,
	/* a, A, e, E, f, F, g, G */
	CBPRINTF_SPECIFIER_FP,
};

/** @brief The allowed types of length modifier. */
enum cbprintf_length_mod_enum {
	CBPRINTF_LENGTH_NONE,
	CBPRINTF_LENGTH_HH,
	CBPRINTF_LENGTH_H,
	CBPRINTF_LENGTH_L,
	CBPRINTF_LENGTH_LL,
	CBPRINTF_LENGTH_J,
	CBPRINTF_LENGTH_Z,
	CBPRINTF_LENGTH_T,
	CBPRINTF_LENGTH_UPPER_L,
};


/** @brief Storage for an argument value.
 *
 * Values are extracted from different sources based on API.  An
 * instance of this object should be associated with an instance of
 * cbprintf_conversion, in which the specifier_cat and length_mod
 * fields determine which of these tags is valid.
 */
union cbprintf_argument_value {
	/** For CBPRINTF_SPECIFIER_SINT conversions */
	cbprintf_sint_value_type sint;

	/* For CBPRINTF_SPECIFIER_UINT conversions */
	cbprintf_uint_value_type uint;

	/* For CBPRINTF_SPECIFIER_FP conversions without length_mod
	 * CBPRINTF_LENGTH_L
	 */
	double dbl;

	/* For CBPRINTF_SPECIFIER_FP conversions with length_mod
	 * CBPRINTF_LENGTH_L
	 */
	long double ldbl;

	/* For CBPRINTF_SPECIFIER_PTR conversions */
	void *ptr;
};

/** @brief Structure capturing all attributes of a conversion
 * specification.
 *
 * Initial values come from the specification, but are updated during
 * the conversion.
 */
struct cbprintf_conversion {
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

	/** Indicates an a or A conversion specifier.
	 *
	 * This affects how precision is handled.
	 */
	bool specifier_a: 1;

	/** Conversion specifier category (value from specifier_cat_enum) */
	unsigned int specifier_cat: 3;

	/** If set alternate form requires 0 before octal. */
	bool altform_0: 1;

	/** If set alternate form requires 0x before hex. */
	bool altform_0c: 1;

	/** Set when pad0_value zeroes are to be to be inserted after
	 * the decimal point in a floating point conversion.
	 */
	bool pad_postdp: 1;

	/** Set for floating point values that have a non-zero
	 * pad0_prefix or pad0_pre_exp.
	 */
	bool pad_fp: 1;

	/** Conversion specifier character */
	char specifier;

	union {
		/** Width value from specification.
		 *
		 * Valid until conversion begins.
		 */
		int width_value;

		/** Number of extra zeroes to be inserted around a
		 * formatted value:
		 *
		 * * before a formatted integer value due to precision
		 *   and flag_zero; or
		 * * before a floating point mantissa decimal point
		 *   due to precision; or
		 * * after a floating point mantissa decimal point due
		 *   to precision.
		 *
		 * For example for zero-padded hexadecimal integers
		 * this would insert where the angle brackets are in:
		 * 0x<>hhhh.
		 *
		 * For floating point numbers this would insert at
		 * either <1> or <2> depending on #pad_postdp:
		 * VVV<1>.<2>FFFFeEEE
		 *
		 * Valid after conversion begins.
		 */
		int pad0_value;
	};

	union {
		/** Precision from specification.
		 *
		 * Valid until conversion begins.
		 */
		int prec_value;

		/** Number of extra zeros to be inserted after a decimal
		 * point due to precision.
		 *
		 * Inserts at <> in: VVVV.FFFF<>eEE
		 *
		 * Valid after conversion begins.
		 */
		int pad0_pre_exp;
	};
};

/** @brief Signature for a cbprintf callback function.
 *
 * This function expects two parameters:
 *
 * * @p c a character to output.  The output behavior should be as if
 *   this was cast to an unsigned char.
 * * @p ctx a pointer to an object that provides context for the
 *   output operation.
 *
 * The declaration does not specify the parameter types.  This allows a
 * function like @c fputc to be used without requiring all context pointers to
 * be to a @c FILE object.
 *
 * @return the value of @p c cast to an unsigned char then back to
 * int, or a negative error code that will be returned from
 * cbprintf().
 */
typedef int (*cbprintf_cb)(/* int c, void *ctx */);

/** @brief *printf-like output through a callback.
 *
 * This is essentially printf() except the output is generated
 * character-by-character using the provided @p out function.  This allows
 * formatting text of unbounded length without incurring the cost of a
 * temporary buffer.
 *
 * All formatting specifiers of C99 are recognized, and most are supported if
 * the functionality is enabled.
 *
 * @note The functionality of this function is significantly reduced
 * when `CONFIG_CBPRINTF_NANO` is selected.
 *
 * @param out the function used to emit each generated character.
 *
 * @param ctx context provided when invoking out
 *
 * @param format a standard ISO C format string with characters and conversion
 * specifications.
 *
 * @param ... arguments corresponding to the conversion specifications found
 * within @p format.
 *
 * @return the number of characters printed, or a negative error value
 * returned from invoking @p out.
 */
__printf_like(3, 4)
int cbprintf(cbprintf_cb out, void *ctx, const char *format, ...);

/** @brief Calculate the number of words required for arguments to a cbprintf
 * format specification.
 *
 * This can be used in cases where the arguments must be copied off the stack
 * into separate storage for processing the conversion in another context.
 *
 * @note The length returned does not count bytes.  It counts native words
 * defined as the size of an `int`.
 *
 * @note If `CONFIG_CBPRINTF_NANO` is selected the count will be incorrect if
 * any passed arguments require more than one `int`.
 *
 * @param format a standard ISO C format string with characters and conversion
 * specifications.
 *
 * @return the number of `int` elements required to provide all arguments
 * required for the conversion.
 */
size_t cbprintf_arglen(const char *format);

/** @brief varargs-aware *printf-like output through a callback.
 *
 * This is essentially vsprintf() except the output is generated
 * character-by-character using the provided @p out function.  This allows
 * formatting text of unbounded length without incurring the cost of a
 * temporary buffer.
 *
 * @note This function is available only when `CONFIG_CBPRINTF_LIBC_SUBSTS` is
 * selected.
 *
 * @note The functionality of this function is significantly reduced when
 * `CONFIG_CBPRINTF_NANO` is selected.
 *
 * @param out the function used to emit each generated character.
 *
 * @param ctx context provided when invoking out
 *
 * @param format a standard ISO C format string with characters and conversion
 * specifications.
 *
 * @param ap a reference to the values to be converted.
 *
 * @return the number of characters generated, or a negative error value
 * returned from invoking @p out.
 */
int cbvprintf(cbprintf_cb out, void *ctx, const char *format, va_list ap);

/** @brief snprintf using Zephyrs cbprintf infrastructure.
 *
 * @note The functionality of this function is significantly reduced
 * when `CONFIG_CBPRINTF_NANO` is selected.
 *
 * @param str where the formatted content should be written
 *
 * @param size maximum number of chaacters for the formatted output,
 * including the terminating null byte.
 *
 * @param format a standard ISO C format string with characters and
 * conversion specifications.
 *
 * @param ... arguments corresponding to the conversion specifications found
 * within @p format.
 *
 * return The number of characters that would have been written to @p
 * str, excluding the terminating null byte.  This is greater than the
 * number actually written if @p size is too small.
 */
__printf_like(3, 4)
int snprintfcb(char *str, size_t size, const char *format, ...);

/** @brief vsnprintf using Zephyrs cbprintf infrastructure.
 *
 * @note This function is available only when `CONFIG_CBPRINTF_LIBC_SUBSTS` is
 * selected.
 *
 * @note The functionality of this function is significantly reduced when
 * `CONFIG_CBPRINTF_NANO` is selected.
 *
 * @param str where the formatted content should be written
 *
 * @param size maximum number of chaacters for the formatted output, including
 * the terminating null byte.
 *
 * @param format a standard ISO C format string with characters and conversion
 * specifications.
 *
 * @param ... arguments corresponding to the conversion specifications found
 * within @p format.
 *
 * @param ap a reference to the values to be converted.
 *
 * return The number of characters that would have been written to @p
 * str, excluding the terminating null byte.  This is greater than the
 * number actually written if @p size is too small.
 */
int vsnprintfcb(char *str, size_t size, const char *format, va_list ap);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_CBPRINTF_H_ */
