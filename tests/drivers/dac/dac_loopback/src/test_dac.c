/*
 * Copyright (c) 2020 Libre Solar Technologies GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * @addtogroup test_dac_loopback
 * @{
 * @defgroup t_dac_basic_loopback test_dac_loopback
 * @brief TestPurpose: read back DAC driver output with ADC
 * @}
 */

#include <drivers/dac.h>
#include <drivers/adc.h>
#include <zephyr.h>
#include <ztest.h>

/*
 * We need to define an ADC channel to read back the output generated by the
 * ADC. The two pins need to be connected with a jumper in order to pass the
 * test in actual hardware.
 *
 * ADC and DAC need to use the same reference voltage, as the test sampling
 * point is at half of the full scale voltage.
 */

#if defined(CONFIG_BOARD_NUCLEO_L073RZ) || \
	defined(CONFIG_BOARD_NUCLEO_L152RE)

/*
 * DAC output on PA4 (Arduino A2 pin of Nucleo board)
 * ADC input read from PA1 (Arduino A1 pin of Nucleo board)
 */

#define DAC_DEVICE_NAME		DT_LABEL(DT_NODELABEL(dac1))
#define DAC_CHANNEL_ID		1
#define DAC_RESOLUTION		12

#define ADC_DEVICE_NAME		DT_LABEL(DT_NODELABEL(adc1))
#define ADC_CHANNEL_ID		1
#define ADC_RESOLUTION		12
#define ADC_GAIN		ADC_GAIN_1
#define ADC_REFERENCE		ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME	ADC_ACQ_TIME_DEFAULT

#elif defined(CONFIG_BOARD_TWR_KE18F)

/* DAC0 output on PTE9, ADC input read from ADC0_SE12 */

#define DAC_DEVICE_NAME		DT_LABEL(DT_NODELABEL(dac0))
#define DAC_RESOLUTION		12
#define DAC_CHANNEL_ID		0

#define ADC_DEVICE_NAME		DT_LABEL(DT_NODELABEL(adc0))
#define ADC_RESOLUTION		12
#define ADC_GAIN		ADC_GAIN_1
#define ADC_REFERENCE		ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME	ADC_ACQ_TIME_DEFAULT
#define ADC_CHANNEL_ID		12

#elif defined(CONFIG_BOARD_FRDM_K64F)

/* DAC0 output is internally available on ADC0_SE23 */

#define DAC_DEVICE_NAME		DT_LABEL(DT_NODELABEL(dac0))
#define DAC_RESOLUTION		12
#define DAC_CHANNEL_ID		0

#define ADC_DEVICE_NAME		DT_LABEL(DT_NODELABEL(adc0))
#define ADC_RESOLUTION		12
#define ADC_GAIN		ADC_GAIN_1
#define ADC_REFERENCE		ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME	ADC_ACQ_TIME_DEFAULT
#define ADC_CHANNEL_ID		23

#else
#error "Unsupported board."
#endif

static const struct dac_channel_cfg dac_ch_cfg = {
	.channel_id = DAC_CHANNEL_ID,
	.resolution = DAC_RESOLUTION
};

static const struct adc_channel_cfg adc_ch_cfg = {
	.gain             = ADC_GAIN,
	.reference        = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	.channel_id       = ADC_CHANNEL_ID,
};

static const struct device *init_dac(void)
{
	int ret;
	const struct device *dac_dev = device_get_binding(DAC_DEVICE_NAME);

	zassert_not_null(dac_dev, "Cannot get DAC device");

	ret = dac_channel_setup(dac_dev, &dac_ch_cfg);
	zassert_equal(ret, 0,
		"Setting up of the first channel failed with code %d", ret);

	return dac_dev;
}

/* ADC necessary to read back the value from DAC */
static const struct device *init_adc(void)
{
	int ret;
	const struct device *adc_dev = device_get_binding(ADC_DEVICE_NAME);

	zassert_not_null(adc_dev, "Cannot get ADC device");

	ret = adc_channel_setup(adc_dev, &adc_ch_cfg);
	zassert_equal(ret, 0,
		"Setting up of the ADC channel failed with code %d", ret);

	return adc_dev;
}

/*
 * test_dac_loopback
 */
static int test_task_loopback(void)
{
	int ret;

	const struct device *dac_dev = init_dac();
	const struct device *adc_dev = init_adc();

	if (!dac_dev || !adc_dev) {
		return TC_FAIL;
	}

	/* write a value of half the full scale resolution */
	ret = dac_write_value(dac_dev, DAC_CHANNEL_ID,
		(1U << DAC_RESOLUTION) / 2);
	zassert_equal(ret, 0, "dac_write_value() failed with code %d", ret);

	/* wait to let DAC output settle */
	k_sleep(K_MSEC(10));

	static int16_t m_sample_buffer[1];
	static const struct adc_sequence sequence = {
		.channels    = BIT(ADC_CHANNEL_ID),
		.buffer      = m_sample_buffer,
		.buffer_size = sizeof(m_sample_buffer),
		.resolution  = ADC_RESOLUTION,
	};

	ret = adc_read(adc_dev, &sequence);
	zassert_equal(ret, 0, "adc_read() failed with code %d", ret);
	zassert_within(m_sample_buffer[0],
		(1U << ADC_RESOLUTION) / 2, 32,
		"Value %d read from ADC does not match expected range.",
		m_sample_buffer[0]);

	return TC_PASS;
}

void test_dac_loopback(void)
{
	zassert_true(test_task_loopback() == TC_PASS, NULL);
}
