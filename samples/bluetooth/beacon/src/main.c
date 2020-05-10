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

static void sent(struct bt_le_ext_adv *adv,
		 struct bt_le_ext_adv_sent_info *info)
{
	printk("sent\n");
}

static void connected(struct bt_le_ext_adv *adv,
		       struct bt_le_ext_adv_connected_info *info)
{
	printk("connected\n");
}

static void scanned(struct bt_le_ext_adv *adv,
		    struct bt_le_ext_adv_scanned_info *info)
{
	printk("scanned\n");
}

static const struct bt_le_ext_adv_cb adv_callbacks = {
	.sent = sent,
	.connected = connected,
	.scanned = scanned,
};
static struct bt_le_ext_adv *adv;
static uint8_t adv_index;

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	/* Start advertising */
	struct bt_le_adv_param param = *
		BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY,
				BT_GAP_ADV_SLOW_INT_MIN,
				BT_GAP_ADV_SLOW_INT_MAX,
				NULL);
	printk("options %x, plc %c%c%c\n", param.options,
	       plc[0], plc[1], plc[2]);
#if 0
	err = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
#else
	struct bt_le_ext_adv_start_param as_param = {
		.num_events = 5,
	};

	err = bt_le_ext_adv_create(&param, &adv_callbacks, &adv);
	printk("leac %d\n", err);
	if (err == 0) {
		adv_index = bt_le_ext_adv_get_index(adv);
		printk("%p at %u\n", adv, adv_index);
		err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad),
					     sd, ARRAY_SIZE(sd));
		printk("leasd %d\n", err);
	}
	if (err == 0) {
		err = bt_le_ext_adv_start(adv, &as_param);
		printk("leas %d\n", err);
	}
#endif

	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Beacon started\n");
}

void main(void)
{
	int err;

	printk("Starting Beacon Demo\n");

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}
}
