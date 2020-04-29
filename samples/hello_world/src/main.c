#include <zephyr.h>
#include <hal/nrf_rtc.h>
#include <arch/arm/aarch32/cortex_m/cmsis.h>
#include <drivers/clock_control.h>
#include <drivers/clock_control/nrf_clock_control.h>

void clock_check(void)
{
	/* Spin, checking both clocks as close to simultaneously as
	 * possible.  We need to do this in a tight loop without
	 * logging because the SysTick clock wraps faster than you
	 * think
	 */
	u64_t rtc_tot = 0, systick_tot = 0;
	u32_t systick_last = SysTick->VAL;
	u32_t rtc_last = nrf_rtc_counter_get(NRF_RTC1);

	while(rtc_tot / 32768 < 5) {
		u32_t systick_now = SysTick->VAL;
		u32_t rtc_now = nrf_rtc_counter_get(NRF_RTC1);

		/* Update totals, note that SysTick counts DOWN */
		systick_tot += (systick_last - systick_now) & 0xffffff;
		rtc_tot += rtc_now - rtc_last;

		systick_last = systick_now;
		rtc_last = rtc_now;
	}

	/* Dump the total cycle counts for both clocks, and a ratio in
	 * percent units. */
	printk("RTC: %d SysTick: %d R%%: %d\n",
	       (int)rtc_tot, (int)systick_tot,
	       (int)(100 * systick_tot / rtc_tot));
}

void main(void)
{
	/* Mask interrupts so nothing interferes */
	arch_irq_lock();

	/* Initialize SysTick so it counts through the full 24 bit
	 * range
	 */
	SysTick->LOAD = 0xffffff;
	SysTick->CTRL |= (SysTick_CTRL_ENABLE_Msk |
			  SysTick_CTRL_CLKSOURCE_Msk);

	if (true) {
		struct device *clock = device_get_binding(DT_LABEL(DT_NODELABEL(clock)));
		int rc = clock_control_on(clock, CLOCK_CONTROL_NRF_SUBSYS_HF);
		printk("Clock on %d\n", rc);
	}

	while (true) {
		clock_check();
	}
}
