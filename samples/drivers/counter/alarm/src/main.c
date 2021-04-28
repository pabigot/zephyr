#include <zephyr.h>

#include <device.h>
#include <drivers/counter.h>
#include <sys/printk.h>

#define DELAY 1000
#define ALARM_CHANNEL_ID 0

#if defined(CONFIG_BOARD_ATSAMD20_XPRO)
#define TIMER DT_LABEL(DT_NODELABEL(tc4))
#elif defined(CONFIG_COUNTER_SAM_TC)
#define TIMER DT_LABEL(DT_NODELABEL(tc0))
#elif defined(CONFIG_COUNTER_RTC0)
#define TIMER DT_LABEL(DT_NODELABEL(rtc0))
#elif defined(CONFIG_COUNTER_RTC_STM32)
#define TIMER DT_LABEL(DT_INST(0, st_stm32_rtc))
#elif defined(CONFIG_COUNTER_NATIVE_POSIX)
#define TIMER DT_LABEL(DT_NODELABEL(counter0))
#elif defined(CONFIG_COUNTER_XLNX_AXI_TIMER)
#define TIMER DT_LABEL(DT_INST(0, xlnx_xps_timer_1_00_a))
#elif defined(CONFIG_COUNTER_ESP32)
#define TIMER DT_LABEL(DT_NODELABEL(timer0))
#endif

K_KERNEL_STACK_DEFINE(my_stack, 1024);

struct k_work_q my_work_q;

struct my_data {
	struct k_work work;
	uint32_t timestamp;
};

K_MEM_SLAB_DEFINE(rx_mem_slab, sizeof(struct my_data), 10, 32);

static void process_my_data(struct k_work *work)
{
	struct my_data *rx_block;

	rx_block = CONTAINER_OF(work, struct my_data, work);

	uint32_t now = k_cycle_get_32();
	uint32_t dcycles = now - rx_block->timestamp;
	uint32_t dtime = k_cyc_to_us_near32(dcycles);

	if (dtime > 600)
		printk("d %u\n", dtime);

	k_mem_slab_free(&rx_mem_slab, (void **)&rx_block);
}

static void test_counter_interrupt_fn(const struct device *counter_dev,
				      void *user_data)
{
	int ret;
	struct my_data *rx_block;

	ret = k_mem_slab_alloc(&rx_mem_slab, (void **)&rx_block, K_NO_WAIT);
	if (ret < 0) {
		printk("Failed to allocate rx_block\n");
		return;
	}

	rx_block->timestamp = k_cycle_get_32();

	k_work_init(&rx_block->work, process_my_data);
	k_work_submit_to_queue(&my_work_q, &rx_block->work);
}

void main(void)
{
	const struct device *counter_dev;
	struct counter_top_cfg top_alarm_cfg;
	int ret;

	printk("Counter alarm sample\n\n");
	counter_dev = device_get_binding(TIMER);
	if (counter_dev == NULL) {
		printk("Device not found\n");
		return;
	}

	top_alarm_cfg = (struct counter_top_cfg){
		.flags = 0,
		.ticks = counter_us_to_ticks(counter_dev, DELAY),
		.callback = test_counter_interrupt_fn,
		.user_data = NULL,
	};

	ret = counter_set_top_value(counter_dev, &top_alarm_cfg);

	printk("Set alarm in %u usec (%u ticks)\n",
	       (uint32_t)(counter_ticks_to_us(counter_dev,
					      top_alarm_cfg.ticks)),
	       top_alarm_cfg.ticks);

	if (-EINVAL == ret) {
		printk("Alarm settings invalid\n");
	} else if (-ENOTSUP == ret) {
		printk("Alarm setting request not supported\n");
	} else if (ret != 0) {
		printk("Error\n");
	}

	k_work_queue_start(&my_work_q, my_stack,
			   K_THREAD_STACK_SIZEOF(my_stack), K_PRIO_COOP(15),
			   NULL);

	counter_start(counter_dev);

	while (1) {
		k_sleep(K_MSEC(1));
	}
}
