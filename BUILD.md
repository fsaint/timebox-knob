# Build guide

Four stages: print, wire on the bench, flash, assemble into the box. Wiring and flashing can happen while the print is on order. Budget two evenings once everything is on the desk.

Diagrams: [`docs/wiring.png`](docs/wiring.png) and [`docs/assembly.png`](docs/assembly.png). Keep both open.

## 1. Print

Send `enclosure/3mf/tray.3mf` and `enclosure/3mf/lid.3mf` to a print service (JLC3DP, PCBWay, Craftcloud, Treatstock) or your own printer.

- FDM, PLA or PETG (PETG if the desk gets direct sun)
- 0.2 mm layers, 3 walls, 15–20 % infill
- Both parts print flat on their base, no supports

Before ordering, measure the three things listed under "Things to verify" in the README. If `grip_h` is different from 5.5 mm, edit `enclosure/src/build.py` and regenerate.

## 2. Wire on the bench (no case yet)

Keep the battery unplugged until step 2d. Follow `docs/assembly.png` step by step.

**a. Power lead (Cable A).** Cut one Adafruit 4924 cable in half. Strip 5 mm off the red and black leads of one half. Red → the charger's `5V OUT` screw, black → `GND`. Fold the two unused leads back and tape them.

**b. Confirm the knob's power pins.** The knob's MX1.25 "5V" port has four pins; only two are 5 V and GND. Check the silkscreen on the underside, or meter the included MX1.25-to-USB cable. If the pin order doesn't match the pigtail's red/black, swap the wires at the terminal block. This is the one mistake that kills the board.

**c. Beeper lead (Cable B).** Take the other half of the cut 4924 cable. Cut the three pins off the Adafruit 3893 cable and strip its wires. Join them to the small cable's wires, one joint at a time, taping each:

| Amp wire (3893) | Knob I2C port pin | Note |
|---|---|---|
| GND (black) | GND | |
| Vin (red) | 5V | |
| Signal (white) | SDA | |
| — | SCL | unused; tape its bare end |

Go by the labels printed next to the knob's I2C port, not by the small cable's wire colours. On the reference unit 5 V was white and GND was yellow.

Plug the 3893's JST end into the amp's input port. Screw the piezo's two wires into the amp's output terminal (either way round).

**d. Gain.** The amp is powered from the knob's 5 V, so set the DIP switches to **×2** (only the ×2 switch on). Do not use ×3 at 5 V; it exceeds the chip's rating.

**e. Battery.** Plug the LiPo into the charger's `BATT` JST-PH socket. Green LED = 5 V boost is live. Plug Cable A into the knob's 5V port. The screen boots.

**f. Charging.** Any USB-C cable into the charger: orange LED = charging.

## 3. Flash

The knob has no USB-C port. To program it:

1. Unplug Cable A from the knob.
2. Plug the included MX1.25-to-USB cable into the same 5V port, other end into your computer.
3. Upload from the Arduino IDE (ESP32 board package, board: ESP32S3 Dev Module). Elecrow's wiki has the full setup.
4. Unplug the USB cable, plug Cable A back in.

Never have both cables in at once.

`firmware/beep_test/beep_test.ino` is the smallest possible proof the beeper works: one beep per second on GPIO 38, the I2C port's SDA pin. It does not start the I2C bus, because the pin is being used as a plain output. Your real timer firmware should do the same.

## 4. Assemble

1. Foam-tape the battery flat in the front-left bay, the charger against the back wall of the back-left bay with its USB-C in the window, and the amp in the right-hand strip. Follow the ribs on the tray floor.
2. Glue the piezo face-down over the five slots in the back-right bay (hot glue or CA on the rim).
3. Route the two leads up through the lid's opening.
4. Sit the knob's fixed base plate in the lid's ring; the connectors hang through into the box. Stick the base down with double-sided tape, or use the knob's M2.5 screw points if you've added holes.
5. Drop the lid into the tray and drive the four M3 screws into the corner bosses.

## Optional: power switch

The Adafruit 805 switch wires between the charger's `EN` pad and `GND`. The EN pad has no connector, so this is the one soldered connection in the project. Closed = 5 V off (the battery still charges), open = on. The tray already has a 7 × 4.5 mm slot in the left wall for the actuator; hot-glue the switch body behind it.

Without the switch, unplugging the battery is the off switch.
