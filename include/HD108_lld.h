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

#ifndef __HD108_LLD_H__
#define __HD108_LLD_H__


#include <stdint.h>
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C"
{
#endif


#define HD108_LLD_MIN_COUNT         (       1UL)    ///< minimum number of LEDs
#define HD108_LLD_MAX_COUNT         (    1024UL)    ///< maximum number of LEDs
#define HD108_LLD_MAX_SPI_SPEED     (40000000UL)    ///< maximum SPI speed 40MHz


/**
 * @brief Possible return values of interface functions.
 */
typedef enum {
    HD108_LLD_OK                = 0,    ///< OK
    HD108_LLD_ERROR_UNKNOWN     = 1,    ///< Unknown error
    HD108_LLD_ERROR_INVALID     = 2,    ///< Invalid argument is provided
    HD108_LLD_ERROR_SPI_IN_USE  = 3,    ///< SPI allocation error
    HD108_LLD_ERROR_NO_DMA      = 4,    ///< DMA allocation error
    HD108_LLD_ERROR_NO_MEMORY   = 5,    ///< Memory allocation error
    HD108_LLD_ERROR_NO_CS       = 6,    ///< SPI host doesn't have any free CS slots
    HD108_LLD_ERROR_LENGTH      = 7,    ///< Length of the strip is out of range [HD108_LLD_MIN_COUNT .. HD108_LLD_MAX_COUNT]
    HD108_LLD_ERROR_INDEX       = 8,    ///< Index is out of range.
    HD108_LLD_ERROR_DATA_RATE   = 9     ///< SPI clock speed is too low for the desired update frequency.
} hd108_status_t;


/**
 * @brief Possible LED strip update values in Hz.
 */
typedef enum {
    HD108_LLD_UPDATE_1HZ        =   1,  ///<   1Hz
    HD108_LLD_UPDATE_2HZ        =   2,  ///<   2Hz
    HD108_LLD_UPDATE_5HZ        =   5,  ///<   5Hz
    HD108_LLD_UPDATE_10HZ       =  10,  ///<  10Hz
    HD108_LLD_UPDATE_20HZ       =  20,  ///<  20Hz
    HD108_LLD_UPDATE_24HZ       =  24,  ///<  24Hz
    HD108_LLD_UPDATE_25HZ       =  25,  ///<  25Hz
    HD108_LLD_UPDATE_30HZ       =  30,  ///<  30Hz
    HD108_LLD_UPDATE_50HZ       =  50,  ///<  50Hz
    HD108_LLD_UPDATE_60HZ       =  60,  ///<  60Hz
    HD108_LLD_UPDATE_100HZ      = 100,  ///< 100Hz
    HD108_LLD_UPDATE_120HZ      = 120   ///< 120Hz
} hd108_update_frequency_hz_t;


/**
 * @brief New type for red/green/blue color value.
 */
typedef uint16_t hd108_color_t;


/**
 * @brief New type for driving current value.
 */
typedef uint16_t hd108_current_t;

/**
 * @brief Pixel descriptor for one LED.
 */
typedef struct {
    hd108_current_t cl_blue  :5;    ///< current level for blue
    hd108_current_t cl_green :5;    ///< current level for green
    hd108_current_t cl_red   :5;    ///< current level for red
    hd108_current_t          :1;    ///< restricted (start bit in SPI data)
    hd108_color_t   red;            ///< color value for red
    hd108_color_t   green;          ///< color value for green
    hd108_color_t   blue;           ///< color value for blue
} hd108_pixel_t;


/**
 * @brief New type for update function.
 *          Update function is called when LED (strip) update is possible.
 *          Each LED can be updated by using the hd108_lld_set_pixel function.
 */
typedef void (*callback_update)(void);


/**
 * @brief HD108 LED (strip) configuration descriptor.
 */
typedef struct {
    spi_host_device_t           spi_host;           ///< SPI host. Can be any of the following two: SPI2_HOST or SPI3_HOST
    uint32_t                    spi_speed_hz;       ///< Clock speed of the SPI bus
    uint8_t                     pin_mosi;           ///< MOSI PIN number
    uint8_t                     pin_clk;            ///< CLK PIN number
    uint16_t                    count;              ///< Number of LEDs to be controlled [HD108_LLD_MIN_COUNT .. HD108_LLD_MAX_COUNT].
                                                    ///< The upper limit is coming from the data sheet.
    hd108_update_frequency_hz_t frequency_hz;       ///< Update frequency of the LEDs
    callback_update             update_function;    ///< Update function. It is called when LED update is possible.
} hd108_configuration_t;


/**
 * @brief HD108 LED (strip) init.
 *
 * @note It initializes the SPI bus according to the configuration and creates the context
 *       variable on heap in order to store the LED strip related data including the TX buffer.
 *       Finally registers and starts a timer with the apropriate period time. The period time
 *       is calculated from the provided parameter (hd108_update_frequency_hz_t frequency_hz).
 *
 * @param hd108_configuration Pointer to the configuration struct. After the initialization 
 *                            the struct is not used. 
 * @param ctx_out The address of the context pointer. This is used to identify the context
 *                which has been created during the init phase.
 * 
 * @return
 *         - HD108_LLD_OK                on success
 *         - HD108_LLD_ERROR_UNKNOWN     if unknown error occured
 *         - HD108_LLD_ERROR_INVALID     if one of the configuration parameters is invalid
 *         - HD108_LLD_ERROR_SPI_IN_USE  if the selected spi host is already in use
 *         - HD108_LLD_ERROR_NO_DMA      if all the DMAs are used
 *         - HD108_LLD_ERROR_NO_MEMORY   if memory allocation is not possible
 *         - HD108_LLD_ERROR_NO_CS       if the SPI host doesn't have any free CS slots (should not happen)
 *         - HD108_LLD_ERROR_LENGTH      if the length of the strip is out of range [HD108_LLD_MIN_COUNT .. HD108_LLD_MAX_COUNT]
 *         - HD108_LLD_ERROR_DATA_RATE   if SPI clock speed is too low for the desired update frequency
 */
extern hd108_status_t hd108_lld_init(
    const hd108_configuration_t *hd108_configuration,
    void **ctx_out
);


/**
 * @brief HD108 LED (pixel) update.
 *
 * @note It updates the value of a pixel in the TX buffer.
 *
 * @param ctx_in The address of the context.
 * @param index The index of the LED within the strip.
 * @param pixel Pointer to the pixel data.
 *              The first bit in the data field is always set by the function.
 * 
 * @return
 *         - HD108_LLD_OK                on success
 *         - HD108_LLD_ERROR_INDEX       if the provided index is out of range
 */
extern hd108_status_t hd108_lld_set_pixel(
    void *ctx_in,
    uint16_t index,
    hd108_pixel_t *pixel
);

#ifdef __cplusplus
}
#endif

#endif /* __HD108_LLD_H__ */
