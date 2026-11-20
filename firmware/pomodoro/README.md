# Pomodoro firmware

Arduino sketch for the Elecrow CrowPanel 1.28" ESP32-S3 rotary display. The round screen is a glass
of liquid that drains as the session runs; the LED ring follows the level.

## Controls

| Action | Result |
|---|---|
| Press the knob | Start / pause / resume |
| Turn the knob | ±1 minute (session length before start, time left while running) |
| Hold 1 s | Reset the session; hold again to skip to the next session type |

Sessions cycle focus (25 min) → break (5 min), with a long break (15 min) after four focus sessions.

## Sound

The piezo amp's signal wire is on GPIO 38, the I2C port's SDA pin. The sketch drives it as a plain
2.7 kHz PWM output and never starts I2C on pins 38/39.

- Short beep on each of the last 5 seconds
- Long beep at 0
- Short beep on reset

## Low power

After 2 minutes without knob input while idle or paused (`SLEEP_AFTER_MS`), the screen, backlight and
LED ring switch off and the ESP32 enters light sleep. Press or turn the knob to wake it; that press or
turn is ignored, so waking never starts the timer. A running countdown never sleeps.

The USB serial port disappears while the board is asleep. Wake it with the knob before flashing.

Status: the low power mode compiles but has not yet been tested on hardware.

## Build

Libraries (Arduino Library Manager): **LovyanGFX**, **Adafruit NeoPixel**.
Board package: **esp32 by Espressif**, 3.x.

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB" .
arduino-cli upload -p <port> --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB" .
```

In the Arduino IDE the same settings are: ESP32S3 Dev Module, USB CDC On Boot enabled, PSRAM "OPI PSRAM",
Flash Size 16MB, Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)".
