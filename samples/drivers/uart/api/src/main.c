/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <device.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <spinlock.h>
#include <drivers/uart.h>

#define UART_NODE DT_NODELABEL(uart0)
#define WITH_PRINTK 0

#if (WITH_PRINTK - 0)
#define PRINTK(...) printk(__VA_ARGS__)
#else /* WITH_PRINTK */
#define PRINTK(...)
#endif /* WITH_PRINTK */

#define STATUS_PERIOD_s 1

static struct device *uart;
static u32_t ch_count;
static u8_t last_char;
static u32_t to_cyc;
static struct k_timer timer;
static struct k_work work;
static char msg[128];

static void timer_cb(struct k_timer *timer)
{
	to_cyc = k_cycle_get_32();
	k_work_submit(&work);
}

void poll_input(struct device *uart)
{
	int rc;

	printk("UART POLL API\n");
	do {
		unsigned char c = -1;

		rc = uart_poll_in(uart, &c);
		if (rc == -1) { /* no data, not error */
			rc = 0;
		} else if (rc == 0) { /* data */
			rc = 1;
			last_char = c;
			++ch_count;
			k_work_submit(&work);
			printk("received %02x '%c'\n", last_char,
			       isprint(last_char) ? last_char : '?');
		} else { /* error */
			printk("poll-in %d\n", rc);
		}

	} while (rc >= 0);
	printk("Out of poll loop: %d\n", rc);
}

static void async_input(struct device *uart);

#if (CONFIG_UART_ASYNC_API - 0)

/* The async API transfers individual blocks represented by an address
 * and a length.  A receive operation places data into a buffer,
 * notifying the application when it is full.  A second receive buffer
 * can be provided to avoid drops while the application processes the
 * completion notification.
 */

#define BUF_COUNT 2
#define RX_BUF_SIZE 8
#define RX_TIMEOUT_ms 500
#define TX_BUF_SIZE 80

typedef u8_t rx_buffer_type[RX_BUF_SIZE];
typedef u8_t tx_buffer_type[TX_BUF_SIZE];

struct buffer_state {
	struct k_spinlock lock;
	u8_t active;
};

struct rx_buffer {
	struct buffer_state state;
	rx_buffer_type buf[BUF_COUNT];
};

static inline u8_t rx_buffer_idx(const struct rx_buffer *rxb,
				 const u8_t *bp)
{
	return (rx_buffer_type*)bp - rxb->buf;
}

struct tx_buffer {
	struct buffer_state state;
	tx_buffer_type buf[BUF_COUNT];
};

static inline u8_t tx_buffer_idx(const struct tx_buffer *txb,
				 const u8_t *bp)
{
	return (tx_buffer_type*)bp - txb->buf;
}

static int put_buffer(u8_t idx,
		      struct buffer_state *sp)
{
	u8_t bit = BIT(idx);

	PRINTK("put %u in %x/%x\n", idx,
	       sp->active, (u8_t)BIT_MASK(BUF_COUNT));

	k_spinlock_key_t key = k_spin_lock(&sp->lock);
	int rc = (sp->active & bit) ? idx : -1 - idx;

	sp->active &= ~BIT(idx);

	k_spin_unlock(&sp->lock, key);

	return rc;
}

static int get_buffer(struct buffer_state *sp)
{
	int rc = -ENOMEM;

	PRINTK("get from %x/%x\n",
	       sp->active, (u8_t)BIT_MASK(BUF_COUNT));

	k_spinlock_key_t key = k_spin_lock(&sp->lock);
	u8_t idx = 0;

	while (idx < BUF_COUNT) {
		u8_t bit = BIT(idx);
		if ((bit & sp->active) == 0) {
			sp->active |= bit;
			break;
		}
		++idx;
	}

	k_spin_unlock(&sp->lock, key);

	if (idx < BUF_COUNT) {
		rc = idx;
	}

	return rc;
}

static struct rx_buffer rx_buffer;
static struct tx_buffer tx_buffer;
static u32_t tx_oob;

static void async_cb(struct uart_event *ep,
		     void *ud)
{
	static const char* type_s[] = {
		[UART_TX_DONE] = "TX_DONE",
		[UART_TX_ABORTED] = "TX_ABORTED",
		[UART_RX_RDY] = "RX_RDY",
		[UART_RX_BUF_REQUEST] = "RX_BUF_REQUEST",
		[UART_RX_BUF_RELEASED] = "RX_BUF_RELEASED",
		[UART_RX_DISABLED] = "RX_DISABLED",
		[UART_RX_STOPPED] = "RX_STOPPED",
	};
	struct device *uart = (struct device *)ud;
	int rc;

	__ASSERT_NO_MSG(ep->type < ARRAY_SIZE(type_s));
	switch (ep->type) {
	case UART_TX_DONE:
	case UART_TX_ABORTED: {
		const struct uart_event_tx *txep = &ep->data.tx;
		u8_t bi = tx_buffer_idx(&tx_buffer, txep->buf);

		printk("%s %u.%u\n", type_s[ep->type], bi, txep->len);
		put_buffer(bi, &tx_buffer.state);
	}
		break;
	case UART_RX_RDY: {
		const struct uart_event_rx *rxep = &ep->data.rx;
		const u8_t *sp = rxep->buf + rxep->offset;
		u8_t buf[2 * RX_BUF_SIZE + 1];

		ch_count += rxep->len;
		memmove(buf, sp, rxep->len);
		buf[rxep->len] = 0;

		last_char = sp[rxep->len - 1];
		k_work_submit(&work);

		printk("RX_RDY buf %u ofs %u len %u: '%s'\n",
		       rx_buffer_idx(&rx_buffer, rxep->buf),
		       rxep->offset, rxep->len, buf);
	}
		break;
	case UART_RX_BUF_REQUEST:
		rc = get_buffer(&rx_buffer.state);
		if (rc >= 0) {
			u8_t idx = rc;
			rx_buffer_type *rxb = rx_buffer.buf + idx;

			rc = uart_rx_buf_rsp(uart, *rxb, sizeof(*rxb));
			printk("RX_BUF_REQ provide %u got %d\n", idx, rc);
		} else {
			printk("RX_BUF_REQ no buffers available: %d\n", rc);
		}
		break;
	case UART_RX_BUF_RELEASED: {
		rc = put_buffer(rx_buffer_idx(&rx_buffer, ep->data.rx_buf.buf),
				&rx_buffer.state);
		printk("RX_BUF_RELEASE %d\n", rc);
	}
		break;
	case UART_RX_DISABLED:
		printk("RX_DISABLED\n");
		break;
	case UART_RX_STOPPED: {
		const struct uart_event_rx_stop *sep = &ep->data.rx_stop;
		const struct uart_event_rx *rxep = &sep->data;

		printk("RX_STOPPED buf %u ofs %u len %u: %s%s%s%s\n",
		       rx_buffer_idx(&rx_buffer, rxep->buf), rxep->offset, rxep->len,
		       (sep->reason & UART_ERROR_OVERRUN) ? "OVERRUN " : "",
		       (sep->reason & UART_ERROR_PARITY) ? "PARITY " : "",
		       (sep->reason & UART_ERROR_FRAMING) ? "FRAMING " : "",
		       (sep->reason & UART_BREAK) ? "BREAK" : "");

	}
		break;
	default:
		__ASSERT_NO_MSG(false);
		break;
	}
}

void async_input(struct device *uart)
{
	int rc;

	printk("UART ASYNC API\n");

	/* NB: Must provide device as user data since the event
	 * doesn't tell you which device generated it.
	 */
	rc = uart_callback_set(uart, async_cb, uart);
	printk("callback set got %d\n", rc);

	if (rc >= 0) {
		rc = get_buffer(&rx_buffer.state);

		if (rc >= 0) {
			u8_t idx = (u8_t)rc;

			rc = uart_rx_enable(uart, rx_buffer.buf[idx],
					    sizeof(rx_buffer.buf[idx]),
					    RX_TIMEOUT_ms);
		}
		printk("enable got %d\n", rc);
	}

	while (rc >= 0) {
		k_sleep(K_SECONDS(STATUS_PERIOD_s));
	}
}

#else /* CONFIG_UART_ASYNC_API */
void async_input(struct device *uart) { }
#endif /* CONFIG_UART_ASYNC_API */

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

#include <sys/ring_buffer.h>

/* The architecture of this solution involves coordination between the
 * interrupt handler and the application using ring buffers.
 *
 * When receive is enabled the interrupt handler puts data into a
 * receive buffer and queues a worker to take it out.
 *
 * When transmit is necessary a worker shovels data into a transmit
 * buffer and enables transmit.  The interrupt handler shovels it into
 * the transmit routine, disables itself as soon as the last has gone
 * out, and schedules the user-side tx worker.
 */

static struct k_spinlock tx_lock;
static u8_t tx_buffer[80];
static struct ring_buf tx_rb;

static struct k_spinlock rx_lock;
static u8_t rx_buffer[8];
static struct ring_buf rx_rb;

static struct k_work rx_work;
static struct tx_work {
	struct k_work work;
	const char *mpe;
	const char *mp;
} tx_state;

static u32_t rx_dropped;

static void rx_work_cb(struct k_work *ignore)
{
	u32_t avail;
	size_t loops = 0;
	size_t total = 0;

	do {
		++loops;

		u8_t *sp = NULL;
		k_spinlock_key_t key = k_spin_lock(&rx_lock);

		avail = ring_buf_get_claim(&rx_rb, &sp, (u32_t)-1);
		if (avail > 0) {
			ch_count += avail;
			total += avail;
			last_char = sp[avail - 1];
			sp[avail - 1] = 0;
		}
		ring_buf_get_finish(&rx_rb, avail);

		k_spin_unlock(&rx_lock, key);
		k_work_submit(&work);
	} while (avail > 0);
	uart_irq_rx_enable(uart);

	printk("rx work %u in %u loops\n", total, loops);
}

static void tx_work_cb(struct k_work *work)
{
	struct tx_work *txs = CONTAINER_OF(work, struct tx_work, work);
	u32_t avail = 0;
	u32_t len = 0;
	bool enable = false;

	k_spinlock_key_t key = k_spin_lock(&tx_lock);

	if (txs->mp != NULL) {
		u8_t *dp = NULL;
		u32_t len = 0;

		avail = ring_buf_put_claim(&tx_rb, &dp, (u32_t)-1);
		if (avail > 0) {
			enable = true;
			len = MIN(avail, txs->mpe - txs->mp);
			memmove(dp, txs->mp, len);
			txs->mp += len;
			if (txs->mp == txs->mpe) {
				txs->mp = NULL;
			}
		}
		ring_buf_put_finish(&tx_rb, len);
	}
	k_spin_unlock(&tx_lock, key);

	if (enable) {
		uart_irq_tx_enable(uart);
	}
}

static void irq_handler(void *ud)
{
	static bool invoked;
	struct device *dev = ud;
	size_t loops = 0;

	while (uart_irq_update(dev)
	       && uart_irq_is_pending(dev)) {
		++loops;
		if (uart_irq_rx_ready(dev)) {
			u8_t *dp = NULL;
			u32_t got = 0;
			k_spinlock_key_t key = k_spin_lock(&rx_lock);
			u32_t avail = ring_buf_put_claim(&rx_rb, &dp, (u32_t)-1);

			if (avail > 0) {
				int rc = uart_fifo_read(dev, dp, avail);
				if (rc >= 0) {
					got = rc;
				}
				(void)ring_buf_put_finish(&rx_rb, got);
			} else {
				u8_t discard[8];
				int rc = uart_fifo_read(dev, discard, sizeof(discard));

				if (rc >= 0) {
					rx_dropped += rc;
				}
			}

			k_spin_unlock(&rx_lock, key);

			if (avail == 0) {
				/* No room left in the buffer, stop
				 * reception until it's drained.
				 */
				uart_irq_rx_disable(dev);
			} else if (got > 0) {
				k_work_submit(&rx_work);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			u8_t *dp = NULL;
			u32_t wrote = 0;
			k_spinlock_key_t key = k_spin_lock(&tx_lock);
			u32_t avail = ring_buf_get_claim(&tx_rb, &dp, (u32_t)-1);

			if (avail > 0) {
				int rc = uart_fifo_fill(dev, dp, avail);
				if (rc >= 0) {
					wrote = rc;
				}
				(void)ring_buf_get_finish(&tx_rb, wrote);
			}

			k_spin_unlock(&tx_lock, key);

			if ((avail == 0) && uart_irq_tx_complete(dev)) {
				uart_irq_tx_disable(dev);
			}

			k_work_submit(&tx_state.work);
		}
	}
}

static void interrupt_input(struct device *uart)
{
	printk("UART INTERRUPT-DRIVEN API\n");

	ring_buf_init(&rx_rb, sizeof(rx_buffer), rx_buffer);
	ring_buf_init(&tx_rb, sizeof(tx_buffer), tx_buffer);

	k_work_init(&rx_work, rx_work_cb);
	k_work_init(&tx_state.work, tx_work_cb);
	uart_irq_rx_disable(uart);
	uart_irq_tx_disable(uart);
	uart_irq_callback_user_data_set(uart, irq_handler, uart);
	uart_irq_rx_enable(uart);
}
#else /* CONFIG_UART_INTERRUPT_DRIVEN */
static void interrupt_input(struct device *uart) { }
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

/* Transmit up to len bytes from msg, returning the number of bytes
 * transmitted or a negative error code.
 */
static s32_t send_msg(const u8_t *msg,
		      u32_t len)
{
#ifdef CONFIG_UART_ASYNC_API
	int rc = get_buffer(&tx_buffer.state);

	if (rc >= 0) {
		u8_t bi = (u8_t)rc;
		char *dp = tx_buffer.buf[bi];

		if (len >= sizeof(tx_buffer.buf[bi])) {
			len = sizeof(tx_buffer.buf[bi]) - 1;
		}
		memmove(dp, msg, len);
		dp[len] = 0;
		rc = uart_tx(uart, dp, len, -1);
		if (rc == 0) {
			rc = len;
		}
	}

#elif defined(CONFIG_UART_INTERRUPT_DRIVEN)
	int rc = -ENOMEM;
	k_spinlock_key_t key = k_spin_lock(&tx_lock);
	bool overflow = tx_state.mp != NULL;

	if (!overflow) {
		tx_state.mp = msg;
		tx_state.mpe = msg + len;
	}

	k_spin_unlock(&tx_lock, key);

	if (!overflow) {
		rc = len;
		k_work_submit(&tx_state.work);
	}
#else
	int rc = 0;
	const char *bp = msg;
	const char *bpe = bp + len;

	while (bp < bpe) {
		++rc;
		uart_poll_out(uart, *bp++);
	}
#endif
	return rc;
}

static void work_cb(struct k_work *work)
{
	u32_t now_cyc = k_cycle_get_32();
        u32_t now = k_uptime_get_32();
        unsigned int ms = now % MSEC_PER_SEC;
        unsigned int s;
        unsigned int min;
        unsigned int h;

        now /= MSEC_PER_SEC;
        s = now % 60U;
        now /= 60U;
        min = now % 60U;
        now /= 60U;
        h = now;

	u32_t lc;
	char lcbuf[8];
	char *lcbp = lcbuf;

	if (uart_line_ctrl_get(uart, UART_LINE_CTRL_RTS, &lc) == 0) {
		*lcbp++ = (lc ? 'R' : 'r');
	}
	if (uart_line_ctrl_get(uart, UART_LINE_CTRL_DTR, &lc) == 0) {
		*lcbp++ = (lc ? 'T' : 't');
	}
	if (uart_line_ctrl_get(uart, UART_LINE_CTRL_DCD, &lc) == 0) {
		*lcbp++ = (lc ? 'C' : 'c');
	}
	if (uart_line_ctrl_get(uart, UART_LINE_CTRL_DSR, &lc) == 0) {
		*lcbp++ = (lc ? 'S' : 's');
	}
	if (lcbp != lcbuf) {
		*lcbp++ = ' ';
	}
	*lcbp = 0;

	snprintf(msg, sizeof(msg), "%u:%02u:%02u.%03u [%u]: %s%u rx, last char %02x '%c'"
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
		 ", %u rx drop"
#endif
		 "\n",
		 h, min, s, ms, now_cyc - to_cyc, lcbuf, ch_count, last_char,
		 isprint(last_char) ? last_char : '?'
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
		 , rx_dropped
#endif
		);
	int rc = send_msg(msg, strlen(msg));
	printk("'%s' => %d\n", msg, rc);
}

void main(void)
{
	const char *uart_id = DT_LABEL(UART_NODE);
	NRF_UART_Type *uart0 = (NRF_UART_Type *)DT_REG_ADDR(UART_NODE);

	printk("Hello World! %s %s %s\n", CONFIG_BOARD, __DATE__, __TIME__);

	uart = device_get_binding(uart_id);
	printk("UART %s (%s) at %p hw %p\n", uart_id,
	       DT_NODE_HAS_COMPAT(UART_NODE,nordic_nrf_uart) ? "uart"
	       : DT_NODE_HAS_COMPAT(UART_NODE,nordic_nrf_uarte) ? "uarte"
	       : "??",
	       uart, uart0);

	/* RTS is an MCU output active low.
	 * CTS is an MCU input active low.  When high MCU will not transmit.
	 */

	printk("TXD %u RXD %u CTS %u RTS %u; CFG %x\n",
	       uart0->PSEL.TXD, uart0->PSEL.RXD,
	       uart0->PSEL.CTS, uart0->PSEL.RTS,
	       uart0->CONFIG);

	if (!uart) {
		return;
	}

	/* Here's a nice race condition: When preemptible threads are
	 * enabled (which is the default) the main thread's default
	 * priority is the highest-priority preemptible thread.
	 *
	 * The timer callback submits a work item, which is processed
	 * by the system work queue, which has the lowest-priority
	 * cooperative thread.
	 *
	 * Which means in the code below if the timer callback occurs
	 * before the conditional block configures the UART for
	 * interrupt processing, interrupt-related UART API may be
	 * invoked before the UART is ready, resulting in undefined
	 * behavior such as accessing unconfigured ring buffers.
	 *
	 * We could use k_sched_lock() to prevent the context switch
	 * to the work thread, but there's no place to do the unlock
	 * since the flow branches into calls that don't generally
	 * return.
	 */
	k_work_init(&work, work_cb);
	k_timer_init(&timer, timer_cb, NULL);
	k_timer_start(&timer, K_MSEC(10), K_SECONDS(STATUS_PERIOD_s));

	if (IS_ENABLED(CONFIG_UART_ASYNC_API)) {
		async_input(uart);
	} else if (IS_ENABLED(CONFIG_UART_INTERRUPT_DRIVEN)) {
		interrupt_input(uart);
	} else {
		poll_input(uart);
	}
}
