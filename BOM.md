# Bill of materials

Prices as paid in November 2026, USD, before shipping and tax.

## Electronics

| # | Part | Part number | Qty | Price | Role |
|---|------|-------------|-----|-------|------|
| 1 | 1.28" HMI ESP32-S3 Rotary Display, 240×240 | Elecrow DHE38128D (Amazon: IoTeikXgo B0G6DKFCZD and other brand names) | 1 | ~$30 | the knob, screen, encoder and brain |
| 2 | bq25185 USB / DC / Solar Charger with 5V Boost | Adafruit 6106 | 1 | $8.95 | LiPo charging, 5 V output to the knob |
| 3 | Lithium Ion Polymer Battery 3.7 V 2000 mAh | Adafruit 2011 | 1 | $12.50 | ~10 h runtime |
| 4 | STEMMA Piezo Driver Amp (PAM8904) | Adafruit 5791 | 1 | $4.95 | drives the piezo from a 3.3–5 V square wave |
| 5 | Large Enclosed Piezo Element w/ Wires, 30 mm | Adafruit 1739 | 1 | $0.95 | the beep |
| 6 | 1.25 mm pitch 4-pin cable, 20 cm (Molex PicoBlade compatible) | Adafruit 4924 | 2 | $1.90 | power and signal leads to the knob. **Note: both ends have plugs.** Cut in half for bare wires |
| 7 | STEMMA JST-PH 3-pin to Male Header cable | Adafruit 3893 | 1 | $1.25 | amp input lead |
| 8 | Female/Female jumper wires, 20 × 75 mm | Adafruit 1951 | 1 | $1.95 | only if you don't want to twist wires; optional |
| 9 | Breadboard-friendly SPDT slide switch | Adafruit 805 | 1 | $0.95 | optional power switch, **requires soldering** to the charger's EN pad |

Electronics subtotal: $33.40 (items 2–9) + knob.

## Enclosure

| Part | Qty | Price | Notes |
|------|-----|-------|-------|
| `tray` printed in PLA or PETG, 0.2 mm layers, 20 % infill, no supports | 1 | ~$8–12 from a print service | 103 × 97 × 23 mm |
| `lid` printed the same | 1 | ~$7–10 | 98 × 92 × 36 mm |
| M3 × 8 mm screws (self-tapping or machine) | 4 | pennies | lid to tray corner bosses |
| Double-sided foam tape (3M VHB or similar) | — | — | holds battery, charger and amp to the tray floor |
| USB-C cable | 1 | — | charging; any cable |

## Included with the knob (used in the build)

- MX1.25-to-USB cable, 50 cm — this is how you flash the knob; it has no USB-C port of its own.
- 4-pin cable — not needed if you cut a 4924 in half.

## Tools

- Small flat screwdriver (terminal blocks)
- Wire strippers
- Tape (electrical, Scotch, masking — anything non-conductive)
- Calipers, for the measurements in `README.md` → "Things to verify"
