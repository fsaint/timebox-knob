---
name: get-felipes-attention
description: Use when Felipe asks to be alerted, pinged, buzzed, reminded or interrupted physically ("alert me when it's done", "remind me at 3", "beep the knob", "set my alerts for the day"), or when something urgent needs him and he may not be watching the screen: a production outage, a long job he asked to be told about, a time-critical pickup or meeting. Sends a flashing, beeping message to the Timebox Knob on his desk.
---

# Get Felipe's attention

Felipe has a Timebox Knob on his desk: a small round screen with a light ring and a beeper. An alert
fills the screen with a flashing colour and a short label, and beeps **until he presses the knob**.
It is an interruption, like tapping him on the shoulder.

All commands go through the bundled script (Python standard library only):

```bash
python3 ~/.claude/skills/get-felipes-attention/knob.py <command>
```

| Goal | Command |
|---|---|
| Alert him now | `knob.py now "Server crash!"` |
| Alert at a time today | `knob.py at 15:30 "School pickup"` |
| Alert in N minutes | `knob.py in 20 "Standup"` |
| Set the whole day (replaces everything scheduled) | `knob.py day alerts.json` or `knob.py day -` (stdin) |
| See what is scheduled | `knob.py list` |
| Stop the alert flashing now | `knob.py stop` |
| Remove all scheduled alerts | `knob.py clear` |
| Check the knob is reachable | `knob.py status` |

`now`, `at` and `in` take `--color` (red, orange, yellow, green, blue, purple, pink, white, `#RRGGBB`;
default red) and `--silent` (flash without beeping).

Day file, times in 24-hour local time:

```json
[
  {"at": "09:00", "label": "Standup", "color": "blue"},
  {"at": "15:10", "label": "School pickup"},
  {"at": "17:30", "label": "Call the bank", "color": "orange", "beep": false}
]
```

## When to alert

- **He asked for it**, now or earlier in the session ("tell me when the build finishes"): send it.
- **He did not ask**: use `now` only for something he would want to be interrupted for, such as an
  outage, data at risk, or a deadline minutes away. Ordinary "task finished" news stays in the chat.
- One alert per event. It keeps beeping until he presses the knob, so a second one adds nothing.

## Labels

2 to 5 words, at most 63 bytes, saying what to do or what happened: "School pickup",
"Deploy failed", "Call starts now". The screen is 1.28 inches; the detail goes in the chat.
Use red for urgent, orange or yellow for soon, blue or green for routine reminders.

## Things to know

- `at` and `in` add to the schedule. `day` **replaces** it: run `list` first and include anything
  that should stay.
- A time that has already passed is not scheduled; the script says which ones it skipped.
- Scheduled alerts are stored on the knob and survive a restart. They only fire for today's times.
- The knob is reachable only from Felipe's home Wi-Fi, as `timebox.local`. Exit code 2 means it could
  not be reached (off, out of range, or Wi-Fi turned off in its settings). Then tell Felipe in the
  chat instead; do not keep retrying.
- After sending, tell Felipe in one line what was sent or scheduled.
- The address and token are in `~/.config/timebox-knob/env`. Never print the token or put it in a
  command line. Firmware and API reference: `firmware/pomodoro/README.md` in the timebox-knob repository.
