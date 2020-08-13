/* Copyright (c) 2020 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INCLUDE_EMUL_H
#define INCLUDE_EMUL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct emulator_registration;

/* Standard callback for emulator initialization providing the
 * initializer record and the device that calls the emulator
 * functions.
 */
typedef int (*emulator_init)(const struct emulator_registration *reg,
			     struct device *parent);

/* Registration of an emulator instance. */
struct emulator_registration {
	/* The function used to initialize the emulator state. */
	emulator_init init;
	/* A handle to the device for which this provides low-level
	 * emulation.
	 */
	const char * dev_label;
	/* Emulator-specific configuration data */
	const void *cfg;
};

/* Emulator registrations are aggregated into an array at link time,
 * from which emulating devices can find the emulators that they're to
 * use.
 */
extern const struct emulator_registration __emul_registration_start[];
extern const struct emulator_registration __emul_registration_end[];

/* Use the devicetree node identifier as a unique name. */
#define EMULATOR_REG_NAME(node_id) (_CONCAT(__emulreg_, node_id))

#define EMULATOR_DEFINE(init_ptr, node_id, cfg_ptr) \
	static struct emulator_registration EMULATOR_REG_NAME(node_id)	\
	__attribute__((__section__(".emulators"))) __used = {	     \
		.init = (init_ptr),			     \
		.dev_label = DT_LABEL(node_id),		     \
		.cfg = (cfg_ptr),			     \
	};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* INCLUDE_EMUL_H */
