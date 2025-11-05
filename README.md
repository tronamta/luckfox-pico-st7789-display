# What is this ?
<img width="521" height="451" alt="image" src="https://github.com/user-attachments/assets/b85eb872-2949-4353-a581-b086aa6150c9" />

This is a tool to use an external SPI display with the Luckfox Pico mini / Rockchip RV1103 SoC, it's coded to work with an ST7789 320x170 1.8" TFT, but can be adapted to other variants. The display will render what's in the Linux framebuffer (`/dev/fb0`):

[![Watch the video](https://img.youtube.com/vi/pl5zG5QBx4o/0.jpg)](https://www.youtube.com/watch?v=pl5zG5QBx4o)

# Getting the tool
You can compile yourself or simply download the compiled binaries.

# Compilling
Make sure to first have the Luckfox Pico SDK installed, after that you'll have the compiler ready in the environment, then just run `make`.

# Usage
The tool supports test mode for the display colors and if everything is connected properly, but by default it runs in framebuffer mode:
* Test display: `./fb_st7789 --test`
* Render framebuffer while running in background: `./fb_st7789 -q &`

> [!WARNING]
> Make sure to have SPI0 enabled and no other functions using/multiplexed to their pins, like UART  
> Also make sure to have a framebuffer at `/dev/fb0`  
> Configure framebuffer to match display resolution and use 16 bits color, ex: `fbset -g 320 170 320 170 16`

# How to connect the display ?
<img width="1617" height="341" alt="image" src="https://github.com/user-attachments/assets/642bc8cf-b89b-4f59-9eb2-a485d5a9de74" />

| Display Pin | Pico Pin |
|-------------|-------------------------|
| VCC         | 3.3V            |
| GND         | GND             |
| SCL (SCK)   | 49 / SPI0_CLK |
| SDA (MOSI)  | 50 / SPI0_MOSI |
| RES (Reset) | 56        |
| DC (Data/CMD) | 57      |
| CS (Chip Select) | 48 / SPI0_CS0 |
| BL (Backlight) | 3.3V         |

# Which exactly ST7789 display model is used in this code ?
Please check [here](https://github.com/themrleon/rpi-st7789-console-display?tab=readme-ov-file#what-exactly-is-the-st7789-model-used-in-this-lib-code-)

# How can you run DOOM if it natively runs at 320x200 but the display and framebuffer are only 320x170 ?
I can't! so I had to increase the framebuffer to match DOOM's 320x200 resolution, if you notice in the demo video, I changed with `fbset`, also the game bottom bar is cutoff due to that.

# What's the performance ?
It's fast and can run a fast paced game like DOOM, with low CPU usage, if DMA was added would reduce CPU usage even more. Check the demo video to see how it performs while running DOOM, but CPU usage has been 22-25 %.

# I am confused/stuck and need more details on the steps
Please check the full bible [here](https://github.com/themrleon/luckfox-pico-mini-b/edit/main/README.md)
