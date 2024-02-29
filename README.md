# sunlamp-esp8266

A wifi enabled lamp, made from a bunch of LED-ribbons on aluminium profiles and driven by an ESP8266

Finland is famously dark in winter, so I wanted to have a big lamp with natural white, that could fill up a living room with nice warm light.

Designed around an ESP8266 on a protoboard, some mosfet drivers, two mosfets. Probably not ok EMI-wise...

A wifi interface is available to change brightness, enable or disable alarm clock, etc.

A nice feature is that the color temperature change depending on the brightness. It starts orangy, yellow, white, and even gets a slight blue hue. Perfect for waking up.

Some cares have been taken to reduce standby power consumption (<1W)

Thanks to [DIY Perks](https://youtu.be/V5uycGosYq4?t=491) for the idea

## Issues

Designing time-sensitive things with an ESP8266 brings a few issues :

- PWM is software-emulated, meaning it's slow. It's slightly "vibrating", which can lead to some headaches
- PWM stops when Wifi communicates

An RPI Pico W would have been a better choice, since it features hardware PWN

## Images

![Website](./images%20for%20README/website.png)
![Circuit board](./images%20for%20README/lamp_4.jpg 'Circuit board')
![Main view](./images%20for%20README/lamp_2.jpg 'Main view')
![Schematic for version 2](./images%20for%20README/schematic.png 'Schematic for version 2')
