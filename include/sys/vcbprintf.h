/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Utility for formatted output through a callback
 *
 *
 */

#ifndef ZEPHYR_INCLUDE_SYS_VCBPRINTF_H_
#define ZEPHYR_INCLUDE_SYS_VCBPRINTF_H_

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Signature for a vcbprintf callback function.
 *
 * This function expects two parameters:
 *
 * * @p c a character to output.  The output behavior should be as if
 *   this was cast to an unsigned char.
 * * @p ctx a pointer to an object that provides context for the
 *   output operation.
 *
 * For legacy compatibility the declaration does not specify the
 * parameter types.  (This allows a function like @c fputc to be used
 * without requiring all context pointers to be to a @c FILE object.)
 *
 * @return the value of @p c cast to an unsigned char then back to
 * int, or a negative error code that will be returned from
 * sys_vcbprintf().
 */
typedef int (*sys_vcbprintf_cb)(/* int c, void *ctx */);

/** @brief *printf-like output through a callback.
 *
 * This is essentialy vfprintf() except the output is generated
 * character-by-character using the provided @p out function.  This
 * allows formatting text of unbounded length without incurring the
 * cost of a temporary buffer.
 *
 * @param out the function used to emit each generated character.
 *
 * @param ctx context provided when invoking out
 *
 * @param format a standard ISO C format string with characters and
 * conversion specifications.
 *
 * @param ap a reference to the values to be converted.
 *
 * @return the number of characters printed, or a negative error value
 * returned from invoking @p out.
 */
int sys_vcbprintf(sys_vcbprintf_cb out, void *ctx, const char *format,
		  va_list ap);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_VCBPRINTF_H_ */
