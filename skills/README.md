# Agent skill

`get-felipes-attention/` is a [Claude Code](https://claude.com/claude-code) skill that lets an AI agent
alert you through the knob: flash a labelled alert now, add one at a time of day, or set the alerts
for the whole day. It wraps the alert API described in [`firmware/pomodoro/README.md`](../firmware/pomodoro/README.md).

It is written for its author, by name. To use it yourself, rename the folder and the `name:` line in
`SKILL.md`, and replace the name in the text.

## Install

```bash
cp -R skills/get-felipes-attention ~/.claude/skills/
mkdir -p ~/.config/timebox-knob
cp skills/get-felipes-attention/env.example ~/.config/timebox-knob/env
chmod 600 ~/.config/timebox-knob/env      # then put your ALERT_TOKEN in it
```

Check it: `python3 ~/.claude/skills/get-felipes-attention/knob.py status`

The script needs Python 3 and nothing else. The computer must be on the same Wi-Fi network as the knob.
