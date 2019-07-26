/*
 * Copyright (c) 2018 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include "drivers/sensple/sdp8xx.h"
#include <stdio.h>

void main(void)
{
	static const char *binding_name = DT_LABEL(DT_NODELABEL(sdp810));
	int rc = 0;

	struct device *dev = device_get_binding(binding_name);
	if (!dev) {
		printf("Could not get %s device\n", binding_name);
		return;
	}
	struct k_poll_signal *sdp8xx_sig = sensple_access(dev);
	if (!sdp8xx_sig) {
		printf("SDP8XX not available\n");
		return;
	}

	while (true) {
		struct k_poll_event evt =
			K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
						 K_POLL_MODE_NOTIFY_ONLY,
						 sdp8xx_sig);
		rc = k_poll(&evt, 1, K_MSEC(1000));
		if (K_POLL_STATE_SIGNALED == evt.state) {
			k_poll_signal_reset(sdp8xx_sig);
			rc = sensple_work(dev);
			if ((0 < rc)
			    && (SENSPLE_STATE_OBSERVATION_READY & rc)) {
				struct sdp8xx_observation obs;
				rc = sdp8xx_fetch(dev, &obs);
				if (0 <= rc) {
					printf("%d cPa ; %d cCel\n",
					       obs.diffpres_cPa,
					       obs.temperature_cCel);
				} else {
					printf("OBS failed: %d\n", rc);
				}
			}
		} else {
			rc = sdp8xx_sample(dev);
			if (0 != rc) {
				printf("SAMPLE failed: %d\n", rc);
			}
		}
	}

	printf("exit %d\n", rc);
}
