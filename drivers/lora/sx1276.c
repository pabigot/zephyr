/*
 * Copyright (c) 2019 Manivannan Sadhasivam
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT semtech_sx1276

#include <drivers/gpio.h>
#include <drivers/lora.h>
#include <drivers/spi.h>
#include <zephyr.h>
#include "sx12xx_common.h"

/* LoRaMac-node specific includes */
#include <sx1276/sx1276.h>

#define LOG_LEVEL CONFIG_LORA_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(sx1276);

#define GPIO_RESET_PIN		DT_INST_GPIO_PIN(0, reset_gpios)
#define GPIO_RESET_FLAGS	DT_INST_GPIO_FLAGS(0, reset_gpios)
#define GPIO_CS_PIN		DT_INST_SPI_DEV_CS_GPIOS_PIN(0)

#define SX1276_REG_PA_CONFIG			0x09
#define SX1276_REG_PA_DAC			0x4d
#define SX1276_REG_VERSION			0x42

extern DioIrqHandler *DioIrq[];

struct sx1276_dio {
	const char *port;
	gpio_pin_t pin;
	gpio_dt_flags_t flags;
};

/* Helper macro that UTIL_LISTIFY can use and produces an element with comma */
#define SX1276_DIO_GPIO_ELEM(idx, inst) \
	{ \
		DT_INST_GPIO_LABEL_BY_IDX(inst, dio_gpios, idx), \
		DT_INST_GPIO_PIN_BY_IDX(inst, dio_gpios, idx), \
		DT_INST_GPIO_FLAGS_BY_IDX(inst, dio_gpios, idx), \
	},

#define SX1276_DIO_GPIO_INIT(n) \
	UTIL_LISTIFY(DT_INST_PROP_LEN(n, dio_gpios), SX1276_DIO_GPIO_ELEM, n)

static const struct sx1276_dio sx1276_dios[] = { SX1276_DIO_GPIO_INIT(0) };

#define SX1276_MAX_DIO ARRAY_SIZE(sx1276_dios)

static struct sx1276_data {
	const struct device *reset;
	const struct device *spi;
	struct spi_config spi_cfg;
	const struct device *dio_dev[SX1276_MAX_DIO];
	struct k_work dio_work[SX1276_MAX_DIO];
} dev_data;

bool SX1276CheckRfFrequency(uint32_t frequency)
{
	/* TODO */
	return true;
}

void SX1276SetAntSwLowPower(bool status)
{
	/* TODO */
}

void SX1276SetBoardTcxo(uint8_t state)
{
	/* TODO */
}

void SX1276SetAntSw(uint8_t opMode)
{
	/* TODO */
}

void SX1276Reset(void)
{
	gpio_pin_configure(dev_data.reset, GPIO_RESET_PIN,
			   GPIO_OUTPUT_ACTIVE | GPIO_RESET_FLAGS);

	k_sleep(K_MSEC(1));

	gpio_pin_set(dev_data.reset, GPIO_RESET_PIN, 0);

	k_sleep(K_MSEC(6));
}

static void sx1276_dio_work_handle(struct k_work *work)
{
	int dio = work - dev_data.dio_work;

	(*DioIrq[dio])(NULL);
}

static void sx1276_irq_callback(const struct device *dev,
				struct gpio_callback *cb, uint32_t pins)
{
	unsigned int i, pin;

	pin = find_lsb_set(pins) - 1;

	for (i = 0; i < SX1276_MAX_DIO; i++) {
		if (dev == dev_data.dio_dev[i] &&
		    pin == sx1276_dios[i].pin) {
			k_work_submit(&dev_data.dio_work[i]);
		}
	}
}

void SX1276IoIrqInit(DioIrqHandler **irqHandlers)
{
	unsigned int i;
	static struct gpio_callback callbacks[SX1276_MAX_DIO];

	/* Setup DIO gpios */
	for (i = 0; i < SX1276_MAX_DIO; i++) {
		if (!irqHandlers[i]) {
			continue;
		}

		dev_data.dio_dev[i] = device_get_binding(sx1276_dios[i].port);
		if (dev_data.dio_dev[i] == NULL) {
			LOG_ERR("Cannot get pointer to %s device",
				sx1276_dios[i].port);
			return;
		}

		k_work_init(&dev_data.dio_work[i], sx1276_dio_work_handle);

		gpio_pin_configure(dev_data.dio_dev[i], sx1276_dios[i].pin,
				   GPIO_INPUT | GPIO_INT_DEBOUNCE
				   | sx1276_dios[i].flags);

		gpio_init_callback(&callbacks[i],
				   sx1276_irq_callback,
				   BIT(sx1276_dios[i].pin));

		if (gpio_add_callback(dev_data.dio_dev[i], &callbacks[i]) < 0) {
			LOG_ERR("Could not set gpio callback.");
			return;
		}
		gpio_pin_interrupt_configure(dev_data.dio_dev[i],
					     sx1276_dios[i].pin,
					     GPIO_INT_EDGE_TO_ACTIVE);
	}

}

static int sx1276_transceive(uint8_t reg, bool write, void *data, size_t length)
{
	const struct spi_buf buf[2] = {
		{
			.buf = &reg,
			.len = sizeof(reg)
		},
		{
			.buf = data,
			.len = length
		}
	};
	struct spi_buf_set tx = {
		.buffers = buf,
		.count = ARRAY_SIZE(buf),
	};

	if (!write) {
		const struct spi_buf_set rx = {
			.buffers = buf,
			.count = ARRAY_SIZE(buf)
		};

		return spi_transceive(dev_data.spi, &dev_data.spi_cfg,
				&tx, &rx);
	}

	return spi_write(dev_data.spi, &dev_data.spi_cfg, &tx);
}

int sx1276_read(uint8_t reg_addr, uint8_t *data, uint8_t len)
{
	return sx1276_transceive(reg_addr, false, data, len);
}

int sx1276_write(uint8_t reg_addr, uint8_t *data, uint8_t len)
{
	return sx1276_transceive(reg_addr | BIT(7), true, data, len);
}

void SX1276WriteBuffer(uint16_t addr, uint8_t *buffer, uint8_t size)
{
	int ret;

	ret = sx1276_write(addr, buffer, size);
	if (ret < 0) {
		LOG_ERR("Unable to write address: 0x%x", addr);
	}
}

void SX1276ReadBuffer(uint16_t addr, uint8_t *buffer, uint8_t size)
{
	int ret;

	ret = sx1276_read(addr, buffer, size);
	if (ret < 0) {
		LOG_ERR("Unable to read address: 0x%x", addr);
	}
}

void SX1276SetRfTxPower(int8_t power)
{
	int ret;
	uint8_t pa_config = 0;
	uint8_t pa_dac = 0;

	ret = sx1276_read(SX1276_REG_PA_CONFIG, &pa_config, 1);
	if (ret < 0) {
		LOG_ERR("Unable to read PA config");
		return;
	}

	ret = sx1276_read(SX1276_REG_PA_DAC, &pa_dac, 1);
	if (ret < 0) {
		LOG_ERR("Unable to read PA dac");
		return;
	}

	pa_config = (pa_config & RF_PACONFIG_MAX_POWER_MASK) | 0x70;
	pa_config &= RF_PACONFIG_PASELECT_MASK;

#if defined CONFIG_PA_BOOST_PIN
	pa_config |= RF_PACONFIG_PASELECT_PABOOST;

	if (power > 17) {
		pa_dac = (pa_dac & RF_PADAC_20DBM_MASK) | RF_PADAC_20DBM_ON;
	} else {
		pa_dac = (pa_dac & RF_PADAC_20DBM_MASK) | RF_PADAC_20DBM_OFF;
	}

	if ((pa_dac & RF_PADAC_20DBM_ON) == RF_PADAC_20DBM_ON) {
		if (power < 5) {
			power = 5;
		} else if (power > 20) {
			power = 20;
		}

		pa_config = (pa_config & RF_PACONFIG_OUTPUTPOWER_MASK) |
			     ((power - 5) & 0x0F);
	} else {
		if (power < 2) {
			power = 2;
		} else if (power > 17) {
			power = 17;
		}

		pa_config = (pa_config & RF_PACONFIG_OUTPUTPOWER_MASK) |
			     ((power - 2) & 0x0F);
	}
#elif CONFIG_PA_RFO_PIN
	if (power < -1) {
		power = -1;
	} else if (power > 14) {
		power = 14;
	}

	pa_config = (pa_config & RF_PACONFIG_OUTPUTPOWER_MASK) |
			     ((power + 1) & 0x0F);
#endif
	ret = sx1276_write(SX1276_REG_PA_CONFIG, &pa_config, 1);
	if (ret < 0) {
		LOG_ERR("Unable to write PA config");
		return;
	}

	ret = sx1276_write(SX1276_REG_PA_DAC, &pa_dac, 1);
	if (ret < 0) {
		LOG_ERR("Unable to write PA dac");
		return;
	}
}

/* Initialize Radio driver callbacks */
const struct Radio_s Radio = {
	.Init = SX1276Init,
	.GetStatus = SX1276GetStatus,
	.SetModem = SX1276SetModem,
	.SetChannel = SX1276SetChannel,
	.IsChannelFree = SX1276IsChannelFree,
	.Random = SX1276Random,
	.SetRxConfig = SX1276SetRxConfig,
	.SetTxConfig = SX1276SetTxConfig,
	.Send = SX1276Send,
	.Sleep = SX1276SetSleep,
	.Standby = SX1276SetStby,
	.Rx = SX1276SetRx,
	.Write = SX1276Write,
	.Read = SX1276Read,
	.WriteBuffer = SX1276WriteBuffer,
	.ReadBuffer = SX1276ReadBuffer,
	.SetMaxPayloadLength = SX1276SetMaxPayloadLength,
	.IrqProcess = NULL,
	.RxBoosted = NULL,
	.SetRxDutyCycle = NULL,
	.SetTxContinuousWave = SX1276SetTxContinuousWave,
};

static int sx1276_lora_init(const struct device *dev)
{
#if DT_INST_SPI_DEV_HAS_CS_GPIOS(0)
	static struct spi_cs_control spi_cs;
#endif
	int ret;
	uint8_t regval;

	dev_data.spi = device_get_binding(DT_INST_BUS_LABEL(0));
	if (!dev_data.spi) {
		LOG_ERR("Cannot get pointer to %s device",
			    DT_INST_BUS_LABEL(0));
		return -EINVAL;
	}

	dev_data.spi_cfg.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB;
	dev_data.spi_cfg.frequency = DT_INST_PROP(0, spi_max_frequency);
	dev_data.spi_cfg.slave = DT_INST_REG_ADDR(0);

#if DT_INST_SPI_DEV_HAS_CS_GPIOS(0)
	spi_cs.gpio_pin = GPIO_CS_PIN,
	spi_cs.gpio_dev = device_get_binding(
			DT_INST_SPI_DEV_CS_GPIOS_LABEL(0));
	if (!spi_cs.gpio_dev) {
		LOG_ERR("Cannot get pointer to %s device",
		       DT_INST_SPI_DEV_CS_GPIOS_LABEL(0));
		return -EIO;
	}

	dev_data.spi_cfg.cs = &spi_cs;
#endif

	/* Setup Reset gpio */
	dev_data.reset = device_get_binding(
			DT_INST_GPIO_LABEL(0, reset_gpios));
	if (!dev_data.reset) {
		LOG_ERR("Cannot get pointer to %s device",
		       DT_INST_GPIO_LABEL(0, reset_gpios));
		return -EIO;
	}

	/* Perform soft reset */
	ret = gpio_pin_configure(dev_data.reset, GPIO_RESET_PIN,
				 GPIO_OUTPUT_ACTIVE | GPIO_RESET_FLAGS);

	k_sleep(K_MSEC(100));
	gpio_pin_set(dev_data.reset, GPIO_RESET_PIN, 0);
	k_sleep(K_MSEC(100));

	ret = sx1276_read(SX1276_REG_VERSION, &regval, 1);
	if (ret < 0) {
		LOG_ERR("Unable to read version info");
		return -EIO;
	}

	LOG_INF("SX1276 Version:%02x found", regval);

	ret = sx12xx_init(dev);
	if (ret < 0) {
		LOG_ERR("Failed to initialize SX12xx common");
		return ret;
	}

	return 0;
}

static const struct lora_driver_api sx1276_lora_api = {
	.config = sx12xx_lora_config,
	.send = sx12xx_lora_send,
	.recv = sx12xx_lora_recv,
	.test_cw = sx12xx_lora_test_cw,
};

DEVICE_AND_API_INIT(sx1276_lora, DT_INST_LABEL(0),
		    &sx1276_lora_init, NULL,
		    NULL, POST_KERNEL, CONFIG_LORA_INIT_PRIORITY,
		    &sx1276_lora_api);
