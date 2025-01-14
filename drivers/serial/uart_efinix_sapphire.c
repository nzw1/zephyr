/*
 * Copyright (c) 2023 Efinix Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT efinix_sapphire_uart0

#include "uart_efinix_sapphire_priv.h"
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>

#include <soc.h>

#define UART0_BASE_ADDR DT_INST_REG_ADDR(0)

#define UART0_DATA_REG_ADDR   UART0_BASE_ADDR + BSP_UART_DATA
#define UART0_STATUS_REG_ADDR UART0_BASE_ADDR + BSP_UART_STATUS
#define UART0_CLOCK_REG_ADDR  UART0_BASE_ADDR + BSP_UART_CLOCK_DIVIDER
#define UART0_FRAME_REG_ADDR  UART0_BASE_ADDR + BSP_UART_FRAME_CONFIG

#define UART_IRQ DT_INST_IRQN(0)

struct uart_efinix_sapphire_config {
	uint32_t baudrate;
};

struct uart_efinix_sapphire_data {
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart_irq_callback_user_data_t callback;
	void *cb_data;
#endif
};

static void uart_efinix_sapphire_poll_out(const struct device *dev, unsigned char c)
{
	/* uart_writeAvailability */
	while ((sapphire_msb_read16(UART0_STATUS_REG_ADDR) & 0xFF) == 0) {
	}

	sapphire_write8(UART0_DATA_REG_ADDR, c);
}

static int uart_efinix_sapphire_poll_in(const struct device *dev, unsigned char *c)
{

	if ((sapphire_read32(UART0_STATUS_REG_ADDR) & 0xFF000000) != 0) {
		*c = (unsigned char)sapphire_read32(UART0_DATA_REG_ADDR);
		return 0;
	}

	return -1;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

static void uart_efinix_sapphire_irq_tx_enable(const struct device *dev)
{
	sapphire_write32(UART0_STATUS_REG_ADDR, sapphire_read32(UART0_STATUS_REG_ADDR) | 0x1);
}

static void uart_efinix_sapphire_irq_tx_disable(const struct device *dev)
{
	sapphire_write32(UART0_STATUS_REG_ADDR, sapphire_read32(UART0_STATUS_REG_ADDR) & ~0x1);
}

static void uart_efinix_sapphire_irq_rx_enable(const struct device *dev)
{
	sapphire_write32(UART0_STATUS_REG_ADDR, sapphire_read32(UART0_STATUS_REG_ADDR) | 0x2);
}

static void uart_efinix_sapphire_irq_rx_disable(const struct device *dev)
{
	sapphire_write32(UART0_STATUS_REG_ADDR, sapphire_read32(UART0_STATUS_REG_ADDR) & ~0x2);
}

static int uart_efinix_sapphire_irq_tx_ready(const struct device *dev)
{
	if (sapphire_read32(UART0_STATUS_REG_ADDR) & 0x100) {
		return 1;
	}

	return 0;
}

static int uart_efinix_sapphire_irq_rx_ready(const struct device *dev)
{
	/* Check bit 9 of uart status return true if set else false*/
	if (sapphire_read32(UART0_STATUS_REG_ADDR) & 0x200) {
		return 1;
	}

	return 0;
}

static int uart_efinix_sapphire_fifo_fill(const struct device *dev, const uint8_t *tx_data, int len)
{
	uint32_t fifo_size = (sapphire_read32(UART0_STATUS_REG_ADDR) >> 16) & 0xFF;

	sapphire_write8(UART0_DATA_REG_ADDR, *tx_data);

	return 1;
}

static int uart_efinix_sapphire_fifo_read(const struct device *dev, uint8_t *rx_data, const int len)
{
	uint32_t fifo_size = (sapphire_read32(UART0_STATUS_REG_ADDR) >> 24) & 0xFF;

	if (fifo_size > 0) {
		*rx_data = (uint8_t)sapphire_read32(UART0_DATA_REG_ADDR) & 0xFF;
		return 1;
	}

	return 0;
}

static void uart_efinix_sapphire_irq_err(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static int uart_efinix_sapphire_irq_is_pending(const struct device *dev)
{
	if (sapphire_read32(UART0_STATUS_REG_ADDR) & 0x300) {
		return 1;
	}

	return 0;
}

static int uart_efinix_sapphire_irq_update(const struct device *dev)
{
	return 1;
}

static void uart_efinix_sapphire_irq_callback_set(const struct device *dev,
						  uart_irq_callback_user_data_t cb, void *cb_data)
{
	struct uart_efinix_sapphire_data *data = dev->data;

	data->callback = cb;
	data->cb_data = cb_data;
}

static void uart_efinix_sapphire_isr(const struct device *dev)
{
	struct uart_efinix_sapphire_data *data = dev->data;
	unsigned int key = irq_lock();

	if (data->callback) {
		data->callback(dev, data->cb_data);
	}

	/* check which cause of the ISR and disable and enable back */
	if (uart_efinix_sapphire_irq_tx_ready(dev)) {
		uart_efinix_sapphire_irq_tx_disable(dev);
		uart_efinix_sapphire_irq_tx_enable(dev);
	}

	if (uart_efinix_sapphire_irq_rx_ready(dev)) {
		uart_efinix_sapphire_irq_rx_disable(dev);
		uart_efinix_sapphire_irq_rx_enable(dev);
	}

	irq_unlock(key);
}

#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static const struct uart_driver_api uart_efinix_sapphire_api = {
	.poll_in = uart_efinix_sapphire_poll_in,
	.poll_out = uart_efinix_sapphire_poll_out,
	.err_check = NULL,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = uart_efinix_sapphire_fifo_fill,
	.fifo_read = uart_efinix_sapphire_fifo_read,
	.irq_tx_enable = uart_efinix_sapphire_irq_tx_enable,
	.irq_tx_disable = uart_efinix_sapphire_irq_tx_disable,
	.irq_tx_ready = uart_efinix_sapphire_irq_tx_ready,
	.irq_rx_enable = uart_efinix_sapphire_irq_rx_enable,
	.irq_rx_disable = uart_efinix_sapphire_irq_rx_disable,
	.irq_rx_ready = uart_efinix_sapphire_irq_rx_ready,
	.irq_err_enable = uart_efinix_sapphire_irq_err,
	.irq_err_disable = uart_efinix_sapphire_irq_err,
	.irq_is_pending = uart_efinix_sapphire_irq_is_pending,
	.irq_update = uart_efinix_sapphire_irq_update,
	.irq_callback_set = uart_efinix_sapphire_irq_callback_set,
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
};

static const struct uart_efinix_sapphire_config uart_efinix_sapphire_cfg_0 = {
	.baudrate = DT_INST_PROP(0, current_speed),
};

static int uart_efinix_sapphire_init(const struct device *dev)
{
	ARG_UNUSED(dev);

#define UART0_SAMPLE_PER_BAUD 8
#define UART0_PARITY	      0 /* Off */
#define UART0_STOP	      0 /* 1 stop bit */

	uint32_t prescaler = ((CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC /
			       (uart_efinix_sapphire_cfg_0.baudrate * UART0_SAMPLE_PER_BAUD)) -
			      1) &
			     0xFFFFF;
	sapphire_write32(UART0_CLOCK_REG_ADDR, prescaler);

	/* 8 data bits, no parity, 1 stop bit */
	uint32_t frame_config = (UART0_SAMPLE_PER_BAUD - 1) | UART0_PARITY << 8 | UART0_STOP << 16;

	sapphire_write32(UART0_FRAME_REG_ADDR, frame_config);


#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	IRQ_CONNECT(UART_IRQ, 0, uart_efinix_sapphire_isr, DEVICE_DT_INST_GET(0), 0);
	irq_enable(UART_IRQ);
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

	return 0;
}

/* Device tree instance 0 init */
DEVICE_DT_INST_DEFINE(0, uart_efinix_sapphire_init, NULL, NULL, &uart_efinix_sapphire_cfg_0,
		      PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY, (void *)&uart_efinix_sapphire_api);
