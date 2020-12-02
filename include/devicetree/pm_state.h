/*
 * Copyright (c) 2020 Intel corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DEVICETREE_PM_STATE_H_
#define ZEPHYR_INCLUDE_DEVICETREE_PM_STATE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <power/power_state.h>

/**
 * @defgroup devicetree-pm_states Power Management states device tree macros
 * @{
 */

/* to be documented */
#define DT_PM_STATE(node_id) _CONCAT(PM_STATE_, DT_ENUM_TOKEN(node_id, pm_state))


/**
 * @brief Construct a pm_state_info from 'pm_states' property at index 'i'
 *
 * @param node_id A node identifier compatible with pm-states
 * @param i index of pm_states prop which type is 'phandles'
 * @return pm_state_info item from 'pm_states' property at index 'i'
 */
#define DT_PM_STATE_INFO_ITEM_BY_IDX(node_id, i)                    \
	{                                                           \
		.state = DT_PM_STATE(DT_PHANDLE_BY_IDX(node_id, pm_states, i)), \
		.min_residency_us = DT_PROP_BY_PHANDLE_IDX_OR(node_id, \
				pm_states, i, min_residency_us, -1),   \
	},

/**
 * @brief Length of 'pm-states' property which type is 'phandles'
 *
 * @param node_id A node identifier compatible with pm-states
 * @return length of 'pm-states' property which type is 'phandles'
 */
#define DT_PM_STATE_ITEMS_LEN(node_id) DT_PROP_LEN(node_id, pm_states)

/**
 * @brief Macro function to construct enum pm_state item in UTIL_LISTIFY
 * extension.
 *
 * @param child child index in UTIL_LISTIFY extension.
 * @param node_id A node identifier compatible with pm-states
 * @return macro function to construct a pm_state_info
 */
#define DT_PM_STATE_INFO_ITEMS_FUC(child, node_id) \
	DT_PM_STATE_INFO_ITEM_BY_IDX(node_id, child)

/**
 * @brief Macro function to construct a list of 'pm_state_info' items by
 * UTIL_LISTIFY func
 *
 * Example devicetree fragment:
 *	cpus {
 *		...
 *		cpu0: cpu@0 {
 *			device_type = "cpu";
 *			...
 *			pm-states = <&state0 &state1>;
 *		};
 *	};
 *
 *	...
 *	state0: state0 {
 *		compatible = "pm-state";
 *		pm-state = "PM_STATE_SUSPEND_TO_IDLE";
 *		min-residency-us = <1>;
 *	};
 *
 *	state1: state1 {
 *		compatible = "pm-state";
 *		pm-state = "PM_STATE_SUSPEND_TO_RAM";
 *		min-residency-us = <5>;
 *	};
 *
 * Example usage: *
 *    const enum pm_state states[] =
 *		DT_PM_STATE_INFO_ITEMS_LIST(DT_NODELABEL(cpu0));
 *
 * @param node_id A node identifier compatible with pm-states
 * @return an array of struct pm_state_info.
 */
#define DT_PM_STATE_INFO_ITEMS_LIST(node_id) {         \
	UTIL_LISTIFY(DT_PM_STATE_ITEMS_LEN(node_id),   \
		     DT_PM_STATE_INFO_ITEMS_FUC,       \
		     node_id)                          \
	}

/**
 * @brief Construct a pm_state enum from 'pm_states' property at index 'i'
 *
 * @param node_id A node identifier compatible with pm-states
 * @param i index of pm_states prop which type is 'phandles'
 * @return pm_state item from 'pm_states' property at index 'i'
 */
#define DT_PM_STATE_ITEM_BY_IDX(node_id, i)                \
		DT_ENUM_IDX(DT_PHANDLE_BY_IDX(node_id,     \
				pm_states, i), pm_state),


/**
 * @brief Macro function to construct enum pm_state item in UTIL_LISTIFY
 * extension.
 *
 * @param child child index in UTIL_LISTIFY extension.
 * @param node_id A node identifier compatible with pm-states
 * @return macro function to construct a pm_state enum
 */
#define DT_PM_STATE_ITEMS_FUC(child, node_id) \
	DT_PM_STATE_ITEM_BY_IDX(node_id, child)

/**
 * @brief Macro function to construct a list of enum pm_state items by
 * UTIL_LISTIFY func
 *
 * Example devicetree fragment:
 *	cpus {
 *		...
 *		cpu0: cpu@0 {
 *			device_type = "cpu";
 *			...
 *			pm-states = <&state0 &state1>;
 *		};
 *	};
 *
 *	...
 *	state0: state0 {
 *		compatible = "pm-state";
 *		pm-state = "PM_STATE_SUSPEND_TO_IDLE";
 *		min-residency-us = <1>;
 *	};
 *
 *	state1: state1 {
 *		compatible = "pm-state";
 *		pm-state = "PM_STATE_SUSPEND_TO_RAM";
 *		min-residency-us = <5>;
 *	};
 *
 * Example usage: *
 *    const enum pm_state states[] = DT_PM_STATE_ITEMS_LIST(DT_NODELABEL(cpu0));
 *
 * @param node_id A node identifier compatible with pm-states
 * @return an array of enum pm_state items.
 */
#define DT_PM_STATE_ITEMS_LIST(node_id) {           \
	UTIL_LISTIFY(DT_PM_STATE_ITEMS_LEN(node_id),\
		     DT_PM_STATE_ITEMS_FUC,         \
		     node_id)                       \
	}


/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DEVICETREE_PM_STATE_H_ */
