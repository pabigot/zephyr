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
	unsigned int idx;
	size_t reps;
	uint32_t timestamp;
} items[10];

//K_MEM_SLAB_DEFINE(rx_mem_slab, sizeof(struct my_data), 10, 32);

static void process_my_data(struct k_work *work)
{
	static size_t ctr;
	struct my_data *rx_block;

	rx_block = CONTAINER_OF(work, struct my_data, work);

	uint32_t now = k_cycle_get_32();
	uint32_t dcycles = now - rx_block->timestamp;
	uint32_t dtime = k_cyc_to_us_near32(dcycles);

	if (dtime > 600) {
		unsigned int idx = 0;
#if 0
		static struct k_spinlock lock;
		k_spinlock_key_t key = k_spin_lock(&lock);
		sys_snode_t *node;
		SYS_SLIST_FOR_EACH_NODE(&my_work_q.pending, node) {
			++idx;
			struct k_work *wp = CONTAINER_OF(node, struct k_work, node);
			struct my_data *mdp = CONTAINER_OF(wp, struct my_data, work);
			mdp->idx = idx++;

		}
		k_spin_unlock(&lock, key);
#endif
		printk("d%zu %u %u\n", rx_block - items, dtime, idx);
	}
	else if (false && ++ctr == 1000) {
		static struct k_spinlock lock;
		unsigned int idx = 0;
		k_spinlock_key_t key = k_spin_lock(&lock);
		sys_snode_t *node;
		SYS_SLIST_FOR_EACH_NODE(&my_work_q.pending, node) {
			struct k_work *wp = CONTAINER_OF(node, struct k_work, node);
			struct my_data *mdp = CONTAINER_OF(wp, struct my_data, work);
			mdp->idx = idx++;
		}
		k_spin_unlock(&lock, key);

		printk("found %u\n", idx);
		for (size_t i = 0; i < ARRAY_SIZE(items); ++i) {
			struct my_data *ip = items + i;
			printk("%zu: %02x %u %s %u %zu\n", i,
			       k_work_busy_get(&ip->work), ip->idx,
			       ip->available ? "avail" : "waiting",
			       k_cyc_to_us_near32(now - ip->timestamp),
			       ip->reps);
		}
		ctr = 0;
	}

	rx_block->available = true;
	rx_block->reps += 1;
	//k_mem_slab_free(&rx_mem_slab, (void **)&rx_block);
}

static void test_counter_interrupt_fn(const struct device *counter_dev,
				      void *user_data)
{
	static size_t li;
	struct my_data *rx_block = NULL;

	//ret = k_mem_slab_alloc(&rx_mem_slab, (void **)&rx_block, K_NO_WAIT);
	size_t i = li;
	while (true) {
		if (++i == ARRAY_SIZE(items)) {
			i = 0;
		}
		if (i == li) {
			break;
		}
		rx_block = items + i;
		if (rx_block->available) {
			if (++li == ARRAY_SIZE(items)) {
				li = 0;
			}
			break;
		}
		rx_block = NULL;
	}

	if (!rx_block) {
		//printk("Failed to allocate rx_block\n");
		return;
	}
	//printk("alloc %zu\n", rx_block - items);

	rx_block->available = false;
	rx_block->timestamp = k_cycle_get_32();

	//k_work_init(&rx_block->work, process_my_data);
	int rc = k_work_submit_to_queue(&my_work_q, &rx_block->work);
	if (rc != 1) {
		printk("submit %zu failed %d\n", rx_block - items, rc);
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

// CONFIG_NUM_COOP_PRIORITIES=16
// _NUM_COOP_PRIORITIES=17
// K_PRIO_COOP(x) = -(17 - x)
// K_PRIO_COOP(15) = -2
#define WQ_PRIO K_PRIO_COOP(15)
	printk("prio main %d wq %d\n", CONFIG_MAIN_THREAD_PRIORITY, WQ_PRIO);
	printk("err BUSY %d INVAL %d\n", -EBUSY, -EINVAL);

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
		.no_yield = true,
	};

	k_work_queue_start(&my_work_q, my_stack,
			   K_THREAD_STACK_SIZEOF(my_stack), WQ_PRIO,
			   &qcfg);
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
		k_yield();
	}
}
