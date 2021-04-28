#include <zephyr.h>

#include <device.h>
#include <drivers/counter.h>
#include <spinlock.h>
#include <sys/printk.h>
#include <string.h>

#define DELAY 1000
#define ALARM_CHANNEL_ID 0

#if defined(CONFIG_BOARD_ATSAMD20_XPRO) || defined(CONFIG_BOARD_ATSAMD21_XPRO)
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
	bool available;
	size_t reps;
	uint32_t timestamp;
} items[10];
size_t alloc_fails;

static void process_my_data(struct k_work *work)
{
	static size_t ctr;
	struct my_data *rx_block;

	rx_block = CONTAINER_OF(work, struct my_data, work);

	uint32_t now = k_cycle_get_32();
	uint32_t dcycles = now - rx_block->timestamp;
	uint32_t dtime = k_cyc_to_us_near32(dcycles);

	if (dtime > 600)
		printk("d%zu %u\n", rx_block - items, dtime);
	else if (true && ++ctr == 5000) {
		printk("ctr %zu alloc fails %zu\n", ctr, alloc_fails);
		for (size_t i = 0; i < ARRAY_SIZE(items); ++i) {
			struct my_data *ip = items + i;
			printk("%zu: %02x %s %u %zu\n", i,
			       k_work_busy_get(&ip->work),
			       ip->available ? "avail" : "waiting",
			       k_cyc_to_us_near32(now - ip->timestamp),
			       ip->reps);
		}
		ctr = 0;
	}

	rx_block->available = true;
	rx_block->reps += 1;
}

static void test_counter_interrupt_fn(const struct device *counter_dev,
				      void *user_data)
{
	struct my_data *rx_block = items;
	const struct my_data *rxbe = items + ARRAY_SIZE(items);

	while (rx_block < rxbe) {
		if (rx_block->available) {
			break;
		}
		++rx_block;
	}

	if (rx_block == rxbe) {
		++alloc_fails;
		return;
	}

	rx_block->available = false;
	rx_block->timestamp = k_cycle_get_32();

	int rc = k_work_submit_to_queue(&my_work_q, &rx_block->work);
	if (rc != 1) {
		printk("submit %zu failed %d\n", rx_block - items, rc);
		/* NB: In original version this is not checked, and
		 * allocations made before the work queue is accepting
		 * items would never be freed. */
		rx_block->available = true;
	}
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

#define WQ_PRIO K_PRIO_COOP(15)
	printk("prio main %d wq %d\n", CONFIG_MAIN_THREAD_PRIORITY, WQ_PRIO);

	top_alarm_cfg = (struct counter_top_cfg){
		.flags = 0,
		.ticks = counter_us_to_ticks(counter_dev, DELAY),
		.callback = test_counter_interrupt_fn,
		.user_data = NULL,
	};

	memset(items, 0, sizeof(items));
	for (size_t i = 0; i < ARRAY_SIZE(items); ++i) {
		items[i].available = true;
		k_work_init(&items[i].work, process_my_data);
		printk("inited %zu\n", i);
	}

	struct k_work_queue_config qcfg = {
		.no_yield = false, /* true has no visible effect on issue */
	};

	k_work_queue_start(&my_work_q, my_stack,
			   K_THREAD_STACK_SIZEOF(my_stack), WQ_PRIO,
			   &qcfg);
	/* Gross synchronization with work queue thread.  Without this
	 * attempts to submit items from the counter interrupt may
	 * fail because the work queue isn't initialized and accepting
	 * items yet. */
	k_yield();

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

	counter_start(counter_dev);

	while (1) {
		k_sleep(K_MSEC(1));
	}
}
