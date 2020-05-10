/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <sys/printk.h>
#include <sys/util.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/*
 * Set Advertisement data. Based on the Eddystone specification:
 * https://github.com/google/eddystone/blob/master/protocol-specification.md
 * https://github.com/google/eddystone/tree/master/eddystone-url
 */

static uint8_t ad_url[] = {
		      0xaa, 0xfe, /* Eddystone UUID */
		      0x10, /* Eddystone-URL frame type */
		      0x00, /* Calibrated Tx power at 0m */
		      0x00, /* URL Scheme Prefix http://www. */
		      'z', 'e', 'p', 'h', 'y', 'r',
		      'p', 'r', 'o', 'j', 'e', 'c', 't',
		      0x08, /* .org */
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0xaa, 0xfe),
	{
		.type = BT_DATA_SVC_DATA16,
		.data_len = sizeof(ad_url),
		.data = ad_url,
	},
};
static uint8_t *plc = ad_url + 5;

/* Set Scan Response data */
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static struct bt_le_ext_adv *adv;
static struct k_delayed_work adv_work;

static void sent(struct bt_le_ext_adv *adv,
		 struct bt_le_ext_adv_sent_info *info);

static const struct bt_le_ext_adv_cb adv_callbacks = {
	.sent = sent,
};

static uint64_t init_ts;

void work_handler(struct k_work *work)
{
	static size_t count;
	uint8_t c0 = ++count % 26U;
	uint8_t c1 = (count / 26U) % 26U;
	int rc = 0;

	plc[0] = 'A' + c1;
	plc[1] = 'A' + c0;

	const struct bt_le_adv_param param = *
		BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY,
				BT_GAP_ADV_SLOW_INT_MIN,
				BT_GAP_ADV_SLOW_INT_MAX,
				NULL);

	struct bt_le_ext_adv_start_param as_param = {
		.num_events = 1,
	};

	/* Start advertising */
	printk("options %x, plc %c%c%c\n", param.options,
	       plc[0], plc[1], plc[2]);

	if (adv == NULL) {
		/* First time through: create the advertising set. */
		rc = bt_le_ext_adv_create(&param, &adv_callbacks, &adv);
		printk("lea create: %d\n", rc);
	} else {
		/* It's unclear whether this is necessary if the sent
		 * callback has been invoked, but do it anyway.
		 */
		rc = bt_le_ext_adv_stop(adv);
		printk("lea stop: %d\n", rc);
	}
	if (rc < 0) {
		return;
	}

	if (rc == 0) {
		rc = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad),
					     sd, ARRAY_SIZE(sd));
		printk("lea sd %d\n", rc);
	}
	if (rc == 0) {
		rc = bt_le_ext_adv_start(adv, &as_param);
		printk("leas %d\n", rc);
	}

	init_ts = k_uptime_ticks();
	k_delayed_work_submit(&adv_work, K_SECONDS(5));
}

static void sent(struct bt_le_ext_adv *adv,
		 struct bt_le_ext_adv_sent_info *info)
{
	uint64_t cmpl_ts = k_uptime_ticks();

	printk("sent after %u ticks\n", (uint32_t)(cmpl_ts - init_ts));
}

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	k_delayed_work_submit(&adv_work, K_NO_WAIT);
}

void main(void)
{
	int err;

	printk("Starting Beacon Demo\n");

	k_delayed_work_init(&adv_work, work_handler);

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}
}
