#include <stdio.h>
#include <bsp/board_api.h>
#include <tusb.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

#include "pico/time.h"
#include "ws2812.pio.h"

typedef struct {
	uint8_t index;
	uint8_t format;
} vendor_ctrl_chan_cfg_t;

#define PP_FORMAT_UNSET	0x0
#define PP_FORMAT_RGB	0x1
#define PP_FORMAT_RGBW	0x2

#define PP_VENDOR_CTRL_REQ_CFG_CHAN 0x1

#define PIXDATA_BUFSZ 4096

typedef struct {
	vendor_ctrl_chan_cfg_t cfg;
	bool configured;
	/* PIO */
	PIO pio;
	uint sm;
	uint offset;
	/* DMA */
	alarm_id_t xfer_finished_delay_alarm;
	struct semaphore xfer_finished_sem;
	/* Buffer */
	uint8_t buf[PIXDATA_BUFSZ];
} pp_channel_t;

#define NUM_CHANNELS 8

static pp_channel_t pp_channels[NUM_CHANNELS];

static bool pp_pio_deinit(uint8_t index);
static bool pp_dma_deinit(uint8_t index);

static bool pp_init_channel(uint8_t index, uint8_t format)
{
	bool success = true;
	vendor_ctrl_chan_cfg_t *cfg;
	pp_channel_t *chan;
	uint8_t Bpp;
	uint16_t bytes;

	switch (format) {
		case PP_FORMAT_RGB: Bpp = 3; break;
		case PP_FORMAT_RGBW: Bpp = 4; break;
		default: success = false; goto out;
	}

	chan = &pp_channels[index];

	if (chan->configured) {
		pp_pio_deinit(index);
		pp_dma_deinit(index);
	}

	cfg = &chan->cfg;
	cfg->index = index;
	cfg->format = format;
	chan->configured = true;

	printf("Configuring channel %d\n", cfg->index);

out:
	if (!success) printf("Failed to configure PIO\n");
	return success;
}

#define PP_GPIO_PIN_OFFSET 3

static bool pp_pio_init(uint8_t index)
{
	bool success = true;
	uint pin;

	pp_channel_t *chan = &pp_channels[index];
	pin = index + PP_GPIO_PIN_OFFSET;

    	success = pio_claim_free_sm_and_add_program_for_gpio_range(
		&ws2812_program, &chan->pio, &chan->sm,
		&chan->offset, pin, 1, true);
	if (!success) {
		printf("Failed calling pio_claim_free_sm_and_"
			"add_program_for_gpio_range: pin %d, pio %s\n",
			pin, chan->pio == NULL ? "unavailble" : "available");
		goto out;
	}

	printf("Configured PIO at %p for pin %d sm %d offset %d\n", chan->pio, pin, chan->sm, chan->offset);

	ws2812_program_init(chan->pio, chan->sm, chan->offset, pin, 800000);

out:
	return success;
}

static bool pp_pio_deinit(uint8_t index)
{
	pp_channel_t *chan = &pp_channels[index];

	if (chan->pio != NULL) {
		pio_remove_program_and_unclaim_sm(&ws2812_program,
			chan->pio, chan->sm, chan->offset);
	}
}

static int64_t pp_reset_delay_complete(alarm_id_t id, void *user_data)
{
	pp_channel_t *chan = (pp_channel_t *)user_data;

	chan->xfer_finished_delay_alarm = 0;
	sem_release(&chan->xfer_finished_sem);

	return 0;
}

#define PP_RESET_TIME_US (320)  /* WS2815B minimum reset time determined experimentally */

void pp_dma_complete_channel(uint8_t channel)
{
	/* ASSUMPTION: DMA channel number is output channel index */
	pp_channel_t *chan = &pp_channels[channel];

	dma_hw->ints0 = 1 << channel;

	/* If there's already an end-of-transfer delay
	 * alarm running, cancel it... */
	if (chan->xfer_finished_delay_alarm != 0) {
		cancel_alarm(chan->xfer_finished_delay_alarm);
	}

	/* Set an alarm to prevent further transfers for PP_RESET_TIME_US at
	 * end of each DMA to allow pixels to latch the data in. */
	chan->xfer_finished_delay_alarm = add_alarm_in_us(PP_RESET_TIME_US,
		pp_reset_delay_complete, chan, true);

	return;
}

static uint32_t configured_dma_mask = 0;

void pp_dma_complete_handler(void)
{
	uint32_t mask = dma_hw->ints0 & configured_dma_mask;
	uint8_t channel = 0;

	for (channel = 0; channel < 32; channel++) {
		if (mask & 1) pp_dma_complete_channel(channel);
		mask >>= 1;
		if (!mask)
			break;
	}

	return;
}

static bool pp_dma_init(uint8_t index)
{
	bool success = true;

	pp_channel_t *chan = &pp_channels[index];

	/* ASSUMPTION: we're the only code running and we can have a
	 * one-to-one relationship between the output index and the DMA
	 * channel number, rather than claiming and unclaiming DMA channels
	 * as required. */
	dma_channel_claim(index);
	dma_channel_config channel_config = dma_channel_get_default_config(index);

	configured_dma_mask |= (1 << index);

	/* Configure DMA channel to write to PIO FIFO */
	channel_config_set_dreq(&channel_config, pio_get_dreq(chan->pio, chan->sm, true));
	channel_config_set_transfer_data_size(&channel_config, DMA_SIZE_8);
	channel_config_set_read_increment(&channel_config, true);
	channel_config_set_write_increment(&channel_config, false);
	channel_config_set_write_address_update_type(&channel_config, DMA_ADDRESS_UPDATE_NONE);
	channel_config_set_chain_to(&channel_config, index);
	dma_channel_configure(index, &channel_config, &chan->pio->txf[chan->sm],
                        NULL, 0, false);
	irq_set_exclusive_handler(DMA_IRQ_0, pp_dma_complete_handler);
	dma_channel_set_irq0_enabled(index, true);
	irq_set_enabled(DMA_IRQ_0, true);

	sem_init(&chan->xfer_finished_sem, 1, 1);

	printf("Configured DMA %d\n", index);

	return success;
}

static bool pp_dma_deinit(uint8_t index)
{
	pp_channel_t *chan = &pp_channels[index];

	dma_channel_cleanup(index);
	configured_dma_mask &= ~(1 << index);
	dma_channel_unclaim(index);
}

/**
 * USB control
 */

CFG_TUD_MEM_SECTION static struct {
  TUD_EPBUF_DEF(buf, CFG_TUD_ENDPOINT0_SIZE);
} _ctrl_epbuf;

bool tud_vendor_control_xfer_cb(uint8_t rhport,
		uint8_t stage, tusb_control_request_t const* request)
{
	bool success = true;
	vendor_ctrl_chan_cfg_t *chan_cfg;

	if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR) {
		success = false;
		goto out;
	}

	switch (request->bRequest) {
		case PP_VENDOR_CTRL_REQ_CFG_CHAN:
			switch (stage) {
				case CONTROL_STAGE_SETUP:
					tud_control_xfer(rhport, request, &_ctrl_epbuf, sizeof(_ctrl_epbuf));
					break;

				case CONTROL_STAGE_DATA: break;

				case CONTROL_STAGE_ACK:
					chan_cfg = (void *)&_ctrl_epbuf;
					printf("PP_VENDOR_CTRL_REQ_CFG_CHAN "
						"index: %d format: 0x%x\n",
						chan_cfg->index, chan_cfg->format);

					if (chan_cfg->index >= NUM_CHANNELS) {
						success = false;
						goto out;
					}

					success = pp_init_channel(chan_cfg->index, chan_cfg->format);
					if (!success) goto out;

					pp_pio_init(chan_cfg->index);
					pp_dma_init(chan_cfg->index);
					break;

				default: success = false; goto out;
			}
			break;
		default:
			success = false; goto out;
	}

out:
	return success;
}

void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize)
{
	pp_channel_t *chan;

	(void) itf;

	uint8_t channel = buffer[0];
	if (channel > NUM_CHANNELS - 1) {
		printf("Invalid channel index %d\n", channel);
		return;
	}

	if (bufsize > PIXDATA_BUFSZ) {
		printf("Buffer size too big %d (max %d)\n", bufsize, PIXDATA_BUFSZ);
		return;
	}

	chan = &pp_channels[channel];
	if (!chan->configured) {
		printf("Buffer write to unconfigured buffer %d\n", channel);
		return;
	}

	/* Copy to channel buffer and trigger DMA to PIO FIFO */
	sem_acquire_blocking(&chan->xfer_finished_sem);
	memcpy(&chan->buf[0], &buffer[1], bufsize - 1);
	dma_channel_transfer_from_buffer_now(chan->cfg.index, &chan->buf[0],
		dma_encode_transfer_count(bufsize - 1));

	return;
}

int main(void)
{
    stdio_uart_init();

    board_init();
    tusb_init();

    if (board_init_after_tusb) {
        board_init_after_tusb();
    }

    /* Main loop handling USB requests */
    while (1) {
        tud_task();
    }

    return 0;
}
