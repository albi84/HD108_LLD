# HD108/NS108 Smart LED (strip) Low Level Driver for ESP-IDF
ESP-IDF component to control HD108/NS108 smart LEDs. The driver has been implemented according to the following datasheets:

- [NS108-5050-16bit.pdf](https://www.addressableledstrip.com/uploads/20200605/3cc6b3c4b37d1544ed0c4a320947d9d5.pdf)
- [NS108-2020-16bit.pdf](https://www.addressableledstrip.com/uploads/20200605/4f52cfe131c621396b39cc84dd83422f.pdf)
- both datasheets are saved to the datasheets folder

It uses 2 wire SPI bus with DMA to control the LEDs. Maximum 1024 LEDs/SPI bus are supported, the limitation comes from the datasheet. The MOSI PIN shall be connected to the first LED's DI PIN (1) and the CLK to the CI PIN (2).

```
HD108/NS108 pinout
         ┌─────┐
1 DI  ───┤/    ├─── 6 DO
2 CI  ───┤     ├─── 5 CO
3 GND ───┤     ├─── 4 VCC
         └─────┘
```

## Usage
---
A configuration structure shall be passed to the init function to initialize the drivers. After initialization, the struct can be freed as it is not used anymore by the driver. Both SPI buses, the SPI2_HOST, and SPI3_HOST can be used (even at the same time). The maximum SPI clock speed is 40 MHz, count shall be set to at least the number of LEDs on the related SPI bus. Frequency is the update frequency of LEDs and can be set to a pre-defined value. The update function is a void - void function that is called every time when the LEDs can be updated. The second argument of the init function is an out parameter, where the created context address is saved. Later this address shall be passed to the `hd108_lld_set_pixel` function.

The driver is not thread-safe, so the LEDs can be updated only in the callback function, it ensures that no DMA transfer occurs during the update.

To update a LED the `hd108_lld_set_pixel` function shall be called. The first parameter is the context address that is provided by the init function, the second argument is the index of the LED to be updated and the third argument is a pointer to a struct that holds the new values of the LED. Current level and color intensity can be set individualy for each RGB channel. The current level can be set between 0 and 31 the color intensity can be any value between 0 and 65535.

```c
void *ctx = NULL;

void callback(void) {
    // update function
    hd108_pixel_t pixel = {
        .cl_red = 31,
        .cl_green = 31,
        .cl_blue = 31,
        .red = 0xffffU,
        .green = 0,
        .blue = 0
    };

    hd108_status_t status = hd108_lld_set_pixel(ctx, 0, &pixel);
    
    // Check status here
}

void app_main(void) {
    hd108_configuration_t hd108 = {
        .spi_host = SPI2_HOST,
        .spi_speed_hz = 20000000,
        .pin_mosi = 13,
        .pin_clk = 14,
        .count = 1,
        .frequency_hz = HD108_LLD_UPDATE_30HZ,
        .update_function = callback
    };

    hd108_status_t status = hd108_lld_init(&hd108, &ctx);

    // Check status here

    // ...
}

```
