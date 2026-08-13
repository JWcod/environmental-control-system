# Embedded Environmental Control System

An embedded environmental monitoring and control system built on an RP2040-based board (PlatformIO `proton` target). It continuously senses temperature, humidity, and ambient light, then drives a fan, a water-level indicator, an RGB status LED, and an adaptive display backlight in response to configurable thresholds.

## Features

- **Sensing**
  - Temperature and humidity via an AHT20 sensor over I2C
  - Ambient light level via an LDR on an ADC input, with exponential filtering to smooth readings
- **Actuation**
  - Fan control triggered when temperature exceeds a configurable threshold
  - Water-level warning indicator triggered when humidity drops below a configurable threshold
  - RGB LED brightness that continuously adapts to ambient light (auto-dimming)
  - Display backlight brightness that adapts to ambient light via PWM
- **Display / UI**
  - Live status shown on a character display: clock, temperature, humidity, light level, and active alerts
  - Matrix keypad input for setting the system clock and configuring thresholds (temperature, humidity, light) through an on-device menu
- **Timekeeping**: software real-time clock driven by a repeating hardware timer

## Hardware / Platform

- Board: RP2040 (`proton`, via PlatformIO `platform-raspberrypi` with Pico SDK framework)
- Sensors: AHT20 (I2C temperature/humidity), LDR (ADC light sensor)
- Outputs: SPI character display, RGB LED (PWM), fan and water-indicator digital outputs

## Building

This project uses [PlatformIO](https://platformio.org/). With PlatformIO installed:

```bash
pio run
```

See `platformio.ini` for the board and framework configuration.

## Contributors

This project was built as a team effort:

- Jensen Wang (王子宸)
- Corrine Tseng
- Aimee Chiu
- Derek Liu
