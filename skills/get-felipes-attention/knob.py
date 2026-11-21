#!/usr/bin/env python3
"""Send alerts to Felipe's Timebox Knob (the desk timer that flashes and beeps).

Reads the knob's address and token from ~/.config/timebox-knob/env. Standard library only.
Run `python3 knob.py --help` for the commands.
"""
import argparse
import json
import os
import socket
import sys
import time
import urllib.error
import urllib.request

CONFIG = os.path.expanduser("~/.config/timebox-knob/env")
CACHE = os.path.expanduser("~/.config/timebox-knob/schedule-cache.json")
COLORS = "red, orange, yellow, green, blue, purple, pink, white, or #RRGGBB"
MAX_LABEL = 63


def fail(msg, code=1):
    print(f"knob: {msg}", file=sys.stderr)
    sys.exit(code)


def load_config():
    cfg = {}
    try:
        with open(CONFIG) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    cfg[k.strip()] = v.strip()
    except FileNotFoundError:
        fail(f"no config at {CONFIG} (needs TIMEBOX_HOST and TIMEBOX_TOKEN)")
    cfg["TIMEBOX_HOST"] = os.environ.get("TIMEBOX_HOST", cfg.get("TIMEBOX_HOST", "timebox.local"))
    return cfg


def base_url(host):
    # Look the name up as IPv4 ourselves: a .local name otherwise waits ~5 s on an IPv6 lookup.
    try:
        ip = socket.getaddrinfo(host, 80, socket.AF_INET, socket.SOCK_STREAM)[0][4][0]
    except socket.gaierror:
        fail(f"cannot find '{host}' on this network. The knob is only reachable from Felipe's home "
             "Wi-Fi, and only while its Wi-Fi is on (Settings on the knob).", 2)
    return f"http://{ip}"


def call(method, path, body=None):
    cfg = load_config()
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(base_url(cfg["TIMEBOX_HOST"]) + path, data=data, method=method)
    req.add_header("X-Token", cfg.get("TIMEBOX_TOKEN", ""))
    if data is not None:
        req.add_header("Content-Type", "application/json")
    last = None
    for attempt in range(3):  # the knob's Wi-Fi signal is weak; a retry usually gets through
        try:
            with urllib.request.urlopen(req, timeout=6) as r:
                return json.loads(r.read() or b"{}")
        except urllib.error.HTTPError as e:
            try:
                msg = json.loads(e.read()).get("error", "")
            except Exception:
                msg = ""
            fail(f"the knob refused the request ({e.code}): {msg or e.reason}")
        except (urllib.error.URLError, OSError) as e:
            last = e
            time.sleep(1.5)
    fail(f"the knob did not answer ({last}). It may be off, out of range, or have Wi-Fi turned off.", 2)


def check_label(label):
    label = " ".join(label.split())
    if not label:
        fail("the label is empty")
    if len(label.encode()) > MAX_LABEL:
        fail(f"the label is too long ({len(label.encode())} bytes, max {MAX_LABEL}). "
             "Aim for 2 to 5 words: the screen is 1.28 inches.")
    return label


def epoch_today(hhmm):
    try:
        h, m = (int(x) for x in hhmm.split(":"))
        assert 0 <= h < 24 and 0 <= m < 60
    except Exception:
        fail(f"'{hhmm}' is not a time. Use 24-hour HH:MM, for example 15:30.")
    t = time.localtime()
    return int(time.mktime((t.tm_year, t.tm_mon, t.tm_mday, h, m, 0, 0, 0, -1)))


def entry(at, label, color, beep):
    return {"at": int(at), "label": check_label(label), "color": color, "beep": beep}


def load_cache():
    try:
        with open(CACHE) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def pending():
    """Alerts already on the knob that have not fired yet, in the form PUT /schedule takes."""
    # Older knob firmware does not report colour or beep, so fall back to what this script last sent.
    cache = load_cache()
    out = []
    for s in call("GET", "/status").get("schedule", []):
        if not s.get("fired"):
            c = cache.get(f"{s['at']}|{s['label']}", {})
            out.append(entry(s["at"], s["label"], s.get("color", c.get("color", "red")),
                             s.get("beep", c.get("beep", True))))
    return out


def put_schedule(alerts):
    if len(alerts) > 32:
        fail("the knob holds at most 32 scheduled alerts")
    r = call("PUT", "/schedule", {"alerts": alerts})
    try:
        with open(CACHE, "w") as f:
            json.dump({f"{a['at']}|{a['label']}": {"color": a["color"], "beep": a["beep"]} for a in alerts}, f)
    except OSError:
        pass
    for label in r.get("skipped_past", []):
        print(f"skipped (time already passed): {label}")
    return r


def show_schedule():
    st = call("GET", "/status")
    rows = st.get("schedule", [])
    if not rows:
        print("no scheduled alerts")
    for s in sorted(rows, key=lambda s: s["at"]):
        when = time.strftime("%a %H:%M", time.localtime(s["at"]))
        print(f"{when}  {s['label']}{'  (already fired)' if s.get('fired') else ''}")


def main():
    p = argparse.ArgumentParser(description="Alert Felipe on the Timebox Knob.")
    sub = p.add_subparsers(dest="cmd", required=True)

    def alert_opts(sp):
        sp.add_argument("--color", default="red", help=COLORS + " (default red)")
        sp.add_argument("--silent", action="store_true", help="flash without beeping")

    sp = sub.add_parser("now", help="flash and beep right now, until Felipe presses the knob")
    sp.add_argument("label")
    alert_opts(sp)

    sp = sub.add_parser("at", help="add one alert at HH:MM today (keeps the others)")
    sp.add_argument("time")
    sp.add_argument("label")
    alert_opts(sp)

    sp = sub.add_parser("in", help="add one alert N minutes from now (keeps the others)")
    sp.add_argument("minutes", type=float)
    sp.add_argument("label")
    alert_opts(sp)

    sp = sub.add_parser("day", help="REPLACE the whole schedule with a JSON list from a file or - (stdin)")
    sp.add_argument("file")

    sub.add_parser("list", help="show the scheduled alerts")
    sub.add_parser("status", help="show whether the knob is reachable and what it is doing")
    sub.add_parser("stop", help="stop the alert that is flashing now (and any queued behind it)")
    sub.add_parser("clear", help="remove every scheduled alert")

    a = p.parse_args()

    if a.cmd == "now":
        r = call("POST", "/alert", {"label": check_label(a.label), "color": a.color, "beep": not a.silent})
        print("alert is flashing on the knob" + (f" ({r['queued']} queued behind another)" if r.get("queued") else ""))
    elif a.cmd in ("at", "in"):
        at = epoch_today(a.time) if a.cmd == "at" else time.time() + a.minutes * 60
        if at < time.time() - 60:
            fail(f"{a.time} has already passed today")
        put_schedule(pending() + [entry(at, a.label, a.color, not a.silent)])
        show_schedule()
    elif a.cmd == "day":
        raw = sys.stdin.read() if a.file == "-" else open(a.file).read()
        items = json.loads(raw)
        items = items.get("alerts", items) if isinstance(items, dict) else items
        alerts = []
        for it in items:
            at = it["at"]
            at = epoch_today(at) if isinstance(at, str) else at
            alerts.append(entry(at, it["label"], it.get("color", "red"), it.get("beep", True)))
        put_schedule(alerts)
        show_schedule()
    elif a.cmd == "list":
        show_schedule()
    elif a.cmd == "status":
        st = call("GET", "/status")
        t = st["timer"]
        print(f"reachable at {st['ip']}, signal {st['rssi']} dBm, screen {st['screen']}")
        print(f"timer: {t['mode']} {t['state']}, {t['remaining_s'] // 60}:{t['remaining_s'] % 60:02d} left")
        al = st["alert"]
        print(f"alert: {'flashing: ' + al.get('label', '') if al['active'] else 'none'}, {al['queued']} queued")
        print(f"scheduled: {len(st.get('schedule', []))}")
    elif a.cmd == "stop":
        call("DELETE", "/alert")
        print("alert stopped")
    elif a.cmd == "clear":
        call("DELETE", "/schedule")
        print("schedule cleared")


if __name__ == "__main__":
    main()
