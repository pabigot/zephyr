/*
 * Copyright (c) 2018 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr.h>
#include <drivers/sensple/sht21.h>

void main(void)
{
	static const char *binding_name = DT_LABEL(DT_NODELABEL(sht21));
	int rc = 0;

	struct device *dev = device_get_binding(binding_name);
	if (!dev) {
		printf("Could not get %s device\n", binding_name);
		return;
	}
	struct k_poll_signal *sht21_sig = sensple_access(dev);
	if (!sht21_sig) {
		printf("SHT21 not available\n");
		return;
	}

	while (true) {
		struct k_poll_event evt =
			K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
						 K_POLL_MODE_NOTIFY_ONLY,
						 sht21_sig);
		rc = k_poll(&evt, 1, K_MSEC(1000));
		if (K_POLL_STATE_SIGNALED == evt.state) {
			k_poll_signal_reset(sht21_sig);
			rc = sensple_work(dev);
			if ((0 < rc)
			    && (SENSPLE_STATE_OBSERVATION_READY & rc)) {
				struct sht21_observation obs;
				rc = sht21_fetch(dev, &obs);
				if (0 <= rc) {
					printf("%d cCel ; %u pptt\n",
					       obs.temperature_cCel,
					       obs.humidity_pptt);
				} else {
					printf("OBS failed: %d\n", rc);
				}
			}
		} else {
			rc = sht21_sample(dev);
			if (0 != rc) {
				printf("SAMPLE failed: %d\n", rc);
			}
		}
	}

	printf("exit %d\n", rc);
}
