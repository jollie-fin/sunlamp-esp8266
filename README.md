# sunlamp-esp8266

A wifi enabled lamp, made from a bunch of LED-ribbons on aluminium profiles and driven by an ESP8266

Finland is famously dark in winter, so I wanted to have a big lamp with natural white, that could fill up a living room with nice warm light.

Designed around an ESP8266 on a protoboard, some mosfet drivers, two mosfets. Probably not ok EMI-wise...

A wifi interface is available to change color temperature and brightness

Some cares have been taken to reduce standby power consumption (<1W)

Features a few on-off events stored in emulated-EEPROM, to be used as an alarm-clock

Thanks to [DIY Perks](https://youtu.be/V5uycGosYq4?t=491) for the idea

## Issues

Designing time-sensitive things with an ESP8266 brings a few issues :

- PWM is software-emulated, meaning it's slow. It's slightly "vibrating", which can lead to some headaches
- PWM stops when Wifi communicates

An RPI Pico W would have been a better choice, since it features hardware PWN

## Images

![Circuit board](./images%20for%20README/lamp_4.jpg 'Circuit board')
![Main view](./images%20for%20README/lamp_2.jpg 'Main view')
