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

## Settings

Tap the gear at the top of the screen (shown while idle or paused), or swipe down from the top half.
Turn the knob to move, press to act; hold, swipe up, or wait 15 seconds to close.

- **Wi-Fi: On / Off** — remembered across restarts. Off also allows light sleep in standby.
- Connection status: network name, IP address, signal strength, and the clock.

The touch panel is a CST816D on its own I2C bus (GPIO 6/7), separate from the port the beeper uses.

## Sound

The piezo amp's signal wire is on GPIO 38, the I2C port's SDA pin. The sketch drives it as a plain
2.7 kHz PWM output and never starts I2C on pins 38/39.

- Short beep on each of the last 5 seconds
- Long beep at 0
- Short beep on reset

## Low power

After 2 minutes without knob input while idle or paused (`SLEEP_AFTER_MS`), the screen, backlight and
LED ring switch off. Press or turn the knob to wake it; that press or turn is ignored, so waking never
starts the timer. A running countdown never sleeps.

- **Offline build** (no `secrets.h`): the ESP32 also enters light sleep. The USB serial port disappears
  while it is asleep, so wake it with the knob before flashing.
- **Wi-Fi build**: the processor stays awake so alerts can arrive at any time. Expect roughly 1.5–2 days
  on the 2000 mAh battery; it is happiest left on the charger.

## Alerts over Wi-Fi

With a `secrets.h` in place the knob joins your Wi-Fi and runs a small HTTP server, so a script or an
AI agent can put an alert on it. An alert fills the screen with its label, flashing in a colour, with
the LED ring flashing and two beeps every 1.5 seconds, **until the knob is pressed**. A running timer
keeps counting underneath. Alerts that arrive while one is showing queue up (8 max) and appear one
press at a time.

### Setup

1. Copy `secrets.example.h` to `secrets.h` (git-ignored) and fill in the Wi-Fi name, password and a token.
2. Install the **ArduinoJson** library, then build and flash as below.
3. The knob shows its IP address at the bottom of the idle screen. It also answers at `http://timebox.local`.

Every request needs the token in an `X-Token` header. Bodies are JSON and must be sent with `Content-Type: application/json`.

### API

| Request | What it does |
|---|---|
| `POST /alert` | Show an alert now |
| `DELETE /alert` | Stop the current alert and clear the queue |
| `PUT /schedule` | Replace the schedule with a list of timed alerts |
| `DELETE /schedule` | Clear the schedule |
| `GET /status` | Clock, timer state, current alert and the schedule |

An alert is `{"label": "...", "color": "red", "beep": true}`. `color` is `red` (default), `orange`,
`yellow`, `green`, `blue`, `purple`, `pink`, `white` or `#RRGGBB`. `beep` defaults to `true`. Labels
are wrapped to fit the round screen; up to about 40 characters reads well.

```bash
KNOB=http://timebox.local
TOKEN=...   # ALERT_TOKEN from secrets.h

# right now
curl -X POST $KNOB/alert -H "X-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"label": "Server crash!", "color": "red"}'

# all the alerts for the day (replaces any earlier schedule)
curl -X PUT $KNOB/schedule -H "X-Token: $TOKEN" -H "Content-Type: application/json" -d '{"alerts": [
  {"at": "09:30", "label": "Standup", "color": "blue"},
  {"at": "15:10", "label": "School pickup", "color": "red"},
  {"in": 600,     "label": "Tea is ready", "color": "green", "beep": false}
]}'

curl $KNOB/status -H "X-Token: $TOKEN"
```

Schedule times are `"at": "HH:MM"` (24 h, today, in the `TZ_INFO` timezone from `secrets.h`),
`"at": <Unix seconds>`, or `"in": <seconds from now>`. Times already in the past are not scheduled and
are listed in the reply under `skipped_past`. The schedule is stored in flash, so it survives a
restart; an alert that came due more than 30 minutes ago while the knob was off is dropped. The clock
comes from NTP, so scheduling needs internet access a few seconds after boot.

Status: the alert service compiles but has not yet been tested on hardware.

## Build

Libraries (Arduino Library Manager): **LovyanGFX**, **Adafruit NeoPixel**, and **ArduinoJson** for the Wi-Fi build.
Board package: **esp32 by Espressif**, 3.x.

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB" .
arduino-cli upload -p <port> --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB" .
```

In the Arduino IDE the same settings are: ESP32S3 Dev Module, USB CDC On Boot enabled, PSRAM "OPI PSRAM",
Flash Size 16MB, Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)".
