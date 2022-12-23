/*
 * HD108 Smart LED (strip) Low Level Driver for ESP-IDF
 * 
 * MIT License
 * 
 * Copyright (c) 2022 Zsolt Albert
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 */

#include <stdio.h>
#include <string.h>


#include "esp_timer.h"
#include "HD108_lld.h"


/******************************************************************************
 * Configuration
 *****************************************************************************/
#define HD108_LLD_NUM_OF_0S         (      16UL)    ///< number of 0 bytes at the begining of transaction


/******************************************************************************
 * Typedefs
 *****************************************************************************/
/**
 * @brief Helpre struct to easily set start bit.
 */
typedef struct {
    hd108_current_t          :15;   ///< reserved
    hd108_current_t bit_start:1;    ///< Start bit in pixel data
} hd108_pixel_data_t;


/**
 * @brief Context variable to store LED (strip) related information.
 */
typedef struct {
    spi_device_handle_t device_handle;  ///< SPI device
    spi_transaction_t   transaction;    ///< SPI transaction data
    callback_update     callback;       ///< Address of the callback function
    uint16_t            strip_length;   ///< Number of LEDs in the strip [1 .. HD108_LLD_MAX_COUNT]
} hd108_ctx_t;


/******************************************************************************
 * Prototypes
 *****************************************************************************/
static void         hd108_lld_copy_pixel                (const hd108_pixel_t *src, hd108_pixel_t *dst);
static void         hd108_lld_periodic_timer_callback   (void* arg);
static uint32_t     hd108_get_update_period_time        (hd108_update_frequency_hz_t freq_hz);
static esp_err_t    hd108_lld_start_timer_for_ctx       (void *ctx, hd108_update_frequency_hz_t freq);


/******************************************************************************
 * Function implementation
 *****************************************************************************/


/**
 * @brief Copy pixel data to TX buffer.
 *
 * @note It changes the endianness from little endian (ESP32) to big endian
 *       (device) while moves the data from the source address to the
 *       destination address. One pixel is represented on 4 uint16_t.
 *       The first bit is the start bit, and is always 1.
 *       Then 3 x  5 bit for driving current (RGB).
 *       Then 3 x 16 bit for color intensity (RGB).
 *
 * @param src Source of data.
 * @param dst Destination in the TX buffer.
 */
static void hd108_lld_copy_pixel(const hd108_pixel_t *src, hd108_pixel_t *dst) {
    const uint8_t *src_raw = (const uint8_t *)src;
    uint8_t *dst_raw = (uint8_t *)dst;

    // change endianness from little to big
    dst_raw[0] = src_raw[1];
    dst_raw[1] = src_raw[0];
    dst_raw[2] = src_raw[3];
    dst_raw[3] = src_raw[2];
    dst_raw[4] = src_raw[5];
    dst_raw[5] = src_raw[4];
    dst_raw[6] = src_raw[7];
    dst_raw[7] = src_raw[6];
}


/**
 * @brief Timer callback function.
 *
 * @note The timer is responsible to achieve the desired update frequency.
 *       In each iteration it queues the next transaction and waits until
 *       the transaction is done. At the end of the transaction is calls
 *       the update function so the user can change the value of any LED
 *       for the next transaction.
 *       
 *
 * @param arg The address of the context.
 */
static void hd108_lld_periodic_timer_callback(void* arg) {
    spi_transaction_t *transaction;
    hd108_ctx_t *ctx = (hd108_ctx_t *)arg;
    (void)spi_device_queue_trans(ctx->device_handle, &ctx->transaction, portMAX_DELAY);
    (void)spi_device_get_trans_result(ctx->device_handle, &transaction, portMAX_DELAY);
    ctx->callback();
}


/**
 * @brief Helper function to calculate timer period time.
 *
 * @note It calculates the timer period time in microseconds from
 *       LED update frequency.
 *
 * @param freq_hz Frequency in Hz.
 * 
 * @return
 *         - The timer period, in microseconds.
 */
static uint32_t hd108_get_update_period_time(hd108_update_frequency_hz_t freq_hz) {
    return 1000000 / freq_hz;
}


/**
 * @brief Creates and starts timer.
 *
 * @note It creates and starts the periodic timer that is used to
 *       create and handle SPI transaction.
 *
 * @param ctx The address of the context.
 * @param freq The update frequency of the LEd (strip)
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if some of the create_args are not valid
 *      - ESP_ERR_INVALID_STATE if esp_timer library is not initialized yet
 *      - ESP_ERR_NO_MEM if memory allocation fails
 */
static esp_err_t hd108_lld_start_timer_for_ctx(void *ctx, hd108_update_frequency_hz_t freq) {
    esp_err_t err;
    const esp_timer_create_args_t periodic_timer_args = {
        .arg = ctx,
        .callback = &hd108_lld_periodic_timer_callback,
        .dispatch_method = ESP_TIMER_TASK,
        .name = NULL,
        .skip_unhandled_events = true
    };

    esp_timer_handle_t periodic_timer;

    err = esp_timer_create(&periodic_timer_args, &periodic_timer);
    if (ESP_OK != err) {
        return err;
    }
    
    err = esp_timer_start_periodic(periodic_timer, hd108_get_update_period_time(freq));
    if (ESP_OK != err) {
        // delete timer
        esp_timer_delete(periodic_timer);
        return err;
    }

    return err;
}


/******************************************************************************
 * Interface functions
 * 
 * Interface function documentation can be found in the header file!
 *****************************************************************************/
hd108_status_t hd108_lld_init(const hd108_configuration_t *hd108_configuration, void **ctx_out) {
    esp_err_t err;

    // check strip length
    if( (HD108_LLD_MIN_COUNT > hd108_configuration->count) || (HD108_LLD_MAX_COUNT < hd108_configuration->count)) {
        return HD108_LLD_ERROR_LENGTH;
    }

    // check SPI frequency
    if ( HD108_LLD_MAX_SPI_SPEED < hd108_configuration->spi_speed_hz) {
        return HD108_LLD_ERROR_INVALID;
    }

    // check callback function
    if (NULL == hd108_configuration->update_function) {
        return HD108_LLD_ERROR_INVALID;
    }

    // check data rate
    uint16_t buffer_len = HD108_LLD_NUM_OF_0S + hd108_configuration->count * sizeof(hd108_pixel_t);
    uint32_t rate = buffer_len * 8 * hd108_configuration->frequency_hz * 2;
    if (rate > hd108_configuration->spi_speed_hz) {
        return HD108_LLD_ERROR_DATA_RATE;
    }

    // allocate memory for context
    hd108_ctx_t *ctx = (hd108_ctx_t *)calloc(1, sizeof(hd108_ctx_t));
    if (!ctx) {
        return HD108_LLD_ERROR_NO_MEMORY;
    }

    // allocate memory for LED strip data (TX buffer)
    uint8_t *buffer = (uint8_t *)heap_caps_malloc(buffer_len, MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    if (!buffer) {
        free(ctx);
        return HD108_LLD_ERROR_NO_MEMORY;
    }

    // initialize TX buffer
    memset(buffer, 0, buffer_len);

    // initialize transaction
    ctx->transaction.tx_buffer = buffer;
    ctx->transaction.length = 8 * buffer_len;
    ctx->strip_length = hd108_configuration->count;
    ctx->callback = hd108_configuration->update_function;

    // init SPI bus
    spi_bus_config_t bus_config = {
        .mosi_io_num = hd108_configuration->pin_mosi,
        .sclk_io_num = hd108_configuration->pin_clk,
        .miso_io_num = -1,
        .quadhd_io_num = -1,
        .quadwp_io_num = -1,
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .max_transfer_sz = buffer_len,
    };

    err = spi_bus_initialize(hd108_configuration->spi_host, &bus_config, SPI_DMA_CH_AUTO);
    switch (err) {
        case ESP_ERR_INVALID_ARG:
            //   if configuration is invalid
            free(buffer);
            free(ctx);
            return HD108_LLD_ERROR_INVALID;
        case ESP_ERR_INVALID_STATE:
            // if host already is in use
            free(buffer);
            free(ctx);
            return HD108_LLD_ERROR_SPI_IN_USE;
        case ESP_ERR_NOT_FOUND:
            // if there is no available DMA channel
            free(buffer);
            free(ctx);
            return HD108_LLD_ERROR_NO_DMA;
        case ESP_ERR_NO_MEM:
            // if out of memory
            free(buffer);
            free(ctx);
            return HD108_LLD_ERROR_NO_MEMORY;
        case ESP_OK:
            // on success
            break;
        default:
            free(buffer);
            free(ctx);
            return HD108_LLD_ERROR_UNKNOWN;
    }

    // init SPI device
    spi_device_interface_config_t device_interface_config = {
        .clock_speed_hz = hd108_configuration->spi_speed_hz,
        .mode = 3,
        .spics_io_num = -1,
        .queue_size = 1,
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0
    };

    err = spi_bus_add_device(hd108_configuration->spi_host, &device_interface_config, &ctx->device_handle);
    switch (err) {
        case ESP_ERR_INVALID_ARG:
            // if parameter is invalid
            free(buffer);
            free(ctx);
            return HD108_LLD_ERROR_INVALID;
        case ESP_ERR_NOT_FOUND:
            // if host doesn't have any free CS slots
            free(buffer);
            free(ctx);
            return HD108_LLD_ERROR_NO_CS;
        case ESP_ERR_NO_MEM:
            // if out of memory
            free(buffer);
            free(ctx);
            return HD108_LLD_ERROR_NO_MEMORY;
        case ESP_OK:
            // on success
            break;
        default:
            free(buffer);
            free(ctx);
            return HD108_LLD_ERROR_UNKNOWN;
    }

    err = hd108_lld_start_timer_for_ctx((void*)ctx, hd108_configuration->frequency_hz);

    switch (err) {
        case ESP_ERR_INVALID_ARG:
            // if parameter is invalid
            free(buffer);
            free(ctx);
            return HD108_LLD_ERROR_INVALID;
        case ESP_ERR_INVALID_STATE:
            // if host already is in use
            free(buffer);
            free(ctx);
            return HD108_LLD_ERROR_SPI_IN_USE;
        case ESP_ERR_NO_MEM:
            // if out of memory
            free(buffer);
            free(ctx);
            return HD108_LLD_ERROR_NO_MEMORY;
        case ESP_OK:
            // on success
            break;
        default:
            free(buffer);
            free(ctx);
            return HD108_LLD_ERROR_UNKNOWN;
    }

    // set out parameter
    *ctx_out = ctx;

    return HD108_LLD_OK;
}

hd108_status_t hd108_lld_set_pixel(void *ctx_in, uint16_t index, hd108_pixel_t *pixel) {
    // cast context
    hd108_ctx_t *ctx = ctx_in;

    // Check index
    if (index >= ctx->strip_length) {
        return HD108_LLD_ERROR_INDEX;
    }

    // Set start bit
    hd108_pixel_data_t *pixel_data = (hd108_pixel_data_t *)pixel;
    pixel_data->bit_start = 1;

    // calculate address
    uint8_t *dst = (uint8_t*)ctx->transaction.tx_buffer;

    // set data in buffer
    hd108_lld_copy_pixel(pixel, (hd108_pixel_t *)(dst + HD108_LLD_NUM_OF_0S + sizeof(hd108_pixel_t) * index));

    return HD108_LLD_OK;
}

