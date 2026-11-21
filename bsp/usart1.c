/* SPDX-License-Identifier: GPL-3.0-or-later */
/* USART1 as knx_uart_hal_t; see usart1.h. GD32F10x USART registers. */
#include "usart1.h"

#include "board.h"

#define REG(addr) (*(volatile uint32_t *)(addr))

#define RCU_APB2EN REG(0x40021018u)
#define RCU_APB2RST REG(0x4002100Cu)
#define USART_STAT REG(0x40013800u)
#define USART_DATA REG(0x40013804u)
#define USART_BAUD REG(0x40013808u)
#define USART_CTL0 REG(0x4001380Cu)
#define USART_CTL1 REG(0x40013810u)
#define USART_CTL2 REG(0x40013814u)
#define NVIC_ISER1 REG(0xE000E104u)
#define NVIC_ICER1 REG(0xE000E184u)
#define NVIC_IPR(n) (*(volatile uint8_t *)(0xE000E400u + (n)))

#define USART1_IRQN 37u
#define USART1_PRIORITY 0x40u /* below SysTick (0x00) */

/* SAVE (PB15) edge interrupt: EXTI line 15, port B. Same priority as USART1,
 * below SysTick, so its time stamp is current. */
#define AFIO_EXTISS3 REG(0x40010014u) /* sources of EXTI lines 12-15 */
#define EXTI_INTEN REG(0x40010400u)
#define EXTI_RTEN REG(0x40010408u)
#define EXTI_FTEN REG(0x4001040Cu)
#define EXTI_PD REG(0x40010414u)
#define SAVE_LINE (1u << 15)
#define SAVE_EXTISS_SHIFT 12u
#define SAVE_EXTISS_PORT_B 1u
#define EXTI15_10_IRQN 40u
#define SAVE_PRIORITY USART1_PRIORITY

/* STAT bits */
#define ST_PERR (1u << 0)
#define ST_FERR (1u << 1)
#define ST_NERR (1u << 2)
#define ST_ORERR (1u << 3)
#define ST_RBNE (1u << 5)
#define ST_TBE (1u << 7)
/* CTL0 bits */
#define C0_REN (1u << 2)
#define C0_TEN (1u << 3)
#define C0_RBNEIE (1u << 5)
#define C0_TBEIE (1u << 7)
#define C0_PM (1u << 9)
#define C0_PCEN (1u << 10)
#define C0_WL (1u << 12)
#define C0_UEN (1u << 13)

static knx_uart_hal_rx_cb_t g_rx_cb;
static void *g_rx_ctx;
static volatile bool g_open;

static uint8_t g_tx_ring[H8I8O_USART1_TX_RING];
static volatile uint32_t g_tx_head; /* next free slot, written with interrupts masked */
static volatile uint32_t g_tx_tail; /* next to send, written by the interrupt */

static volatile h8i8o_usart1_stats_t g_stats;
static uint8_t g_context; /* non-null context for the HAL */

static knx_uart_hal_save_edge_cb_t g_save_cb;
static void *g_save_ctx;

static inline uint32_t irq_save(void)
{
    uint32_t primask;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(primask)::"memory");
    return primask;
}

static inline void irq_restore(uint32_t primask) { __asm volatile("msr primask, %0" ::"r"(primask) : "memory"); }

static uint32_t tx_used(void) { return (g_tx_head - g_tx_tail) % H8I8O_USART1_TX_RING; }

#if H8I8O_TRACE
/* Octet trace; see usart1.h. Written only from the interrupt. */
static uint16_t g_trace[H8I8O_USART1_TRACE_ENTRIES];
static volatile uint32_t g_trace_head; /* written by the interrupt */
static volatile uint32_t g_trace_tail; /* written by the reader */
static volatile uint32_t g_trace_dropped;
static uint32_t g_trace_last_us;
static uint32_t g_trace_last_dir = 2u; /* neither: the first octet opens a burst */

static void trace_put(uint16_t entry)
{
    const uint32_t head = g_trace_head;
    const uint32_t next = (head + 1u) % H8I8O_USART1_TRACE_ENTRIES;
    if (next == g_trace_tail) {
        g_trace_dropped++;
        return;
    }
    g_trace[head] = entry;
    g_trace_head = next;
}

static void trace_octet(uint8_t octet, uint32_t dir)
{
    const uint32_t now = (uint32_t)board_micros();
    if (dir != g_trace_last_dir || now - g_trace_last_us > 2000u) {
        trace_put((uint16_t)(H8I8O_USART1_TRACE_MARK | (dir != 0u ? H8I8O_USART1_TRACE_TX : 0u) |
                             ((now / 1000u) & 0x3FFFu)));
        g_trace_last_dir = dir;
    }
    g_trace_last_us = now;
    trace_put(octet);
}

size_t h8i8o_usart1_trace_read(uint16_t *out, size_t max)
{
    size_t n = 0u;
    uint32_t tail = g_trace_tail;
    const uint32_t head = g_trace_head;
    while (tail != head && n < max) {
        out[n++] = g_trace[tail];
        tail = (tail + 1u) % H8I8O_USART1_TRACE_ENTRIES;
    }
    g_trace_tail = tail;
    return n;
}

uint32_t h8i8o_usart1_trace_dropped(void) { return g_trace_dropped; }
#else
static inline void trace_octet(uint8_t octet, uint32_t dir) { (void)octet; (void)dir; }
#endif

void USART1_IRQHandler(void)
{
    const uint32_t stat = USART_STAT; /* STAT then DATA clears PERR/FERR/NERR/ORERR */
    if ((stat & (ST_RBNE | ST_ORERR)) != 0u) {
        const uint8_t octet = (uint8_t)USART_DATA;
        uint8_t flags = 0u;
        if (stat & ST_PERR) flags |= KNX_UART_HAL_RX_PARITY_ERROR;
        if (stat & ST_FERR) flags |= KNX_UART_HAL_RX_FRAMING_ERROR;
        if (stat & ST_NERR) flags |= KNX_UART_HAL_RX_NOISE;
        if (stat & ST_ORERR) flags |= KNX_UART_HAL_RX_OVERRUN;
        g_stats.rx_octets++;
        if (flags != 0u) g_stats.rx_errors++;
        trace_octet(octet, 0u);
        if (g_rx_cb != 0) {
            g_rx_cb(g_rx_ctx, octet, flags, (uint32_t)board_micros());
        }
    }
    if ((USART_CTL0 & C0_TBEIE) != 0u && (USART_STAT & ST_TBE) != 0u) {
        const uint32_t tail = g_tx_tail;
        if (tail == g_tx_head) {
            USART_CTL0 &= ~C0_TBEIE;
        } else {
            const uint8_t octet = g_tx_ring[tail % H8I8O_USART1_TX_RING];
            USART_DATA = octet;
            g_tx_tail = (tail + 1u) % H8I8O_USART1_TX_RING;
            g_stats.tx_octets++;
            trace_octet(octet, 1u);
        }
    }
}

static bool hal_open(void *context, const knx_uart_hal_config_t *config, knx_uart_hal_rx_cb_t rx_cb,
                     void *rx_context)
{
    (void)context;
    if (g_open || rx_cb == 0 || config->baud == 0u || config->parity == KNX_UART_HAL_PARITY_ODD) {
        return false;
    }
    RCU_APB2EN |= 1u << 14;  /* USART0EN (the manual's USART0 is the STM32's USART1) */
    RCU_APB2RST |= 1u << 14;
    RCU_APB2RST &= ~(1u << 14);

    g_rx_cb = rx_cb;
    g_rx_ctx = rx_context;
    g_tx_head = 0u;
    g_tx_tail = 0u;

    /* APB2 = core clock. 48 MHz / 19200 = 2500 exactly. */
    USART_BAUD = (H8I8O_CORE_HZ + config->baud / 2u) / config->baud;
    USART_CTL1 = 0u; /* 1 stop bit */
    USART_CTL2 = 0u;
    uint32_t ctl0 = C0_UEN | C0_TEN | C0_REN | C0_RBNEIE;
    if (config->parity == KNX_UART_HAL_PARITY_EVEN) {
        ctl0 |= C0_PCEN | C0_WL; /* 8 data bits + parity = 9-bit word, even */
    }
    NVIC_IPR(USART1_IRQN) = USART1_PRIORITY;
    g_open = true;
    USART_CTL0 = ctl0;
    NVIC_ISER1 = 1u << (USART1_IRQN - 32u);
    return true;
}

static void hal_close(void *context)
{
    (void)context;
    NVIC_ICER1 = 1u << (USART1_IRQN - 32u);
    USART_CTL0 = 0u;
    g_open = false;
    g_rx_cb = 0;
    g_tx_head = 0u;
    g_tx_tail = 0u;
}

static bool hal_write(void *context, const uint8_t *data, size_t len)
{
    (void)context;
    const uint32_t primask = irq_save();
    const uint32_t free_octets = H8I8O_USART1_TX_RING - 1u - tx_used();
    if (!g_open || len == 0u || len > free_octets) {
        g_stats.tx_refused++;
        irq_restore(primask);
        return false;
    }
    uint32_t head = g_tx_head;
    for (size_t i = 0; i < len; ++i) {
        g_tx_ring[head] = data[i];
        head = (head + 1u) % H8I8O_USART1_TX_RING;
    }
    g_tx_head = head;
    USART_CTL0 |= C0_TBEIE;
    irq_restore(primask);
    return true;
}

static size_t hal_tx_free(void *context)
{
    (void)context;
    if (!g_open) return 0u;
    return H8I8O_USART1_TX_RING - 1u - tx_used();
}

static bool hal_read_save(void *context)
{
    (void)context;
    return board_bus_power_failing();
}

/* Both edges: the stack reads the level itself and acts on the edge into
 * power failure only. */
void EXTI15_10_IRQHandler(void)
{
    if ((EXTI_PD & SAVE_LINE) == 0u) {
        return;
    }
    EXTI_PD = SAVE_LINE; /* write 1 to clear */
    if (g_save_cb != 0) {
        g_save_cb(g_save_ctx, (uint32_t)board_micros());
    }
}

static bool hal_install_save_edge_isr(void *context, knx_uart_hal_save_edge_cb_t cb, void *save_context)
{
    (void)context;
    if (cb == 0) {
        return false;
    }
    NVIC_ICER1 = 1u << (EXTI15_10_IRQN - 32u);
    g_save_cb = cb;
    g_save_ctx = save_context;
    AFIO_EXTISS3 = (AFIO_EXTISS3 & ~(0xFu << SAVE_EXTISS_SHIFT)) | (SAVE_EXTISS_PORT_B << SAVE_EXTISS_SHIFT);
    EXTI_RTEN |= SAVE_LINE;
    EXTI_FTEN |= SAVE_LINE;
    EXTI_PD = SAVE_LINE;
    EXTI_INTEN |= SAVE_LINE;
    NVIC_IPR(EXTI15_10_IRQN) = SAVE_PRIORITY;
    NVIC_ISER1 = 1u << (EXTI15_10_IRQN - 32u);
    return true;
}

static void hal_remove_save_edge_isr(void *context)
{
    (void)context;
    NVIC_ICER1 = 1u << (EXTI15_10_IRQN - 32u);
    EXTI_INTEN &= ~SAVE_LINE;
    EXTI_RTEN &= ~SAVE_LINE;
    EXTI_FTEN &= ~SAVE_LINE;
    EXTI_PD = SAVE_LINE;
    g_save_cb = 0;
    g_save_ctx = 0;
}

knx_uart_hal_t h8i8o_usart1_hal(void)
{
    knx_uart_hal_t hal = {0};
    hal.context = &g_context;
    hal.ops.open = hal_open;
    hal.ops.close = hal_close;
    hal.ops.write = hal_write;
    hal.ops.tx_free = hal_tx_free;
    hal.ops.read_save = hal_read_save;
    hal.ops.install_save_edge_isr = hal_install_save_edge_isr;
    hal.ops.remove_save_edge_isr = hal_remove_save_edge_isr;
    return hal;
}

h8i8o_usart1_stats_t h8i8o_usart1_stats(void)
{
    h8i8o_usart1_stats_t copy;
    const uint32_t primask = irq_save();
    copy.rx_octets = g_stats.rx_octets;
    copy.rx_errors = g_stats.rx_errors;
    copy.tx_octets = g_stats.tx_octets;
    copy.tx_refused = g_stats.tx_refused;
    irq_restore(primask);
    return copy;
}
