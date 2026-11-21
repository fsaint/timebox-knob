#pragma once
// Wi-Fi alert service: a small HTTP server so an agent can raise alerts on the knob.
// Included from pomodoro.ino only when secrets.h exists. See README.md for the API.

WebServer server(80);
Preferences prefs;

const int MAX_SCHED = 32;
SchedEntry sched[MAX_SCHED];
int schedCount = 0;

bool clockSynced() { return time(nullptr) > 1700000000; }

void saveSchedule() {
  prefs.putInt("n", schedCount);
  prefs.putBytes("sched", sched, sizeof(SchedEntry) * schedCount);
}

void loadSchedule() {
  int n = prefs.getInt("n", 0);
  if (n < 0 || n > MAX_SCHED || prefs.getBytesLength("sched") != sizeof(SchedEntry) * n) return;
  prefs.getBytes("sched", sched, sizeof(SchedEntry) * n);
  schedCount = n;
}

// ---- parsing
bool parseColor(const char *s, Alert &a) {
  static const struct { const char *name; uint8_t r, g, b; } NAMED[] = {
    {"red", 230, 30, 30}, {"orange", 245, 120, 10}, {"yellow", 240, 200, 20}, {"green", 30, 190, 80},
    {"blue", 40, 110, 240}, {"purple", 150, 60, 230}, {"pink", 240, 80, 160}, {"white", 235, 235, 235},
  };
  if (!s || !*s) s = "red";
  if (s[0] == '#' && strlen(s) == 7) {
    char *end;
    long v = strtol(s + 1, &end, 16);
    if (*end) return false;
    a.r = v >> 16; a.g = (v >> 8) & 0xFF; a.b = v & 0xFF;
    return true;
  }
  for (auto &n : NAMED)
    if (!strcasecmp(s, n.name)) { a.r = n.r; a.g = n.g; a.b = n.b; return true; }
  return false;
}

bool parseAlert(JsonVariantConst v, Alert &a, String &err) {
  const char *label = v["label"] | "";
  if (!*label) { err = "label is required"; return false; }
  strlcpy(a.label, label, sizeof a.label);
  if (!parseColor(v["color"] | "red", a)) { err = "unknown color (use a name like red, or #RRGGBB)"; return false; }
  a.beep = v["beep"] | true;
  return true;
}

// "at": Unix seconds or "HH:MM" today (local time); or "in": seconds from now. Returns 0 on error.
int64_t parseWhen(JsonVariantConst v, String &err) {
  time_t now = time(nullptr);
  if (v["in"].is<long>()) return (int64_t)now + v["in"].as<long>();
  if (v["at"].is<const char *>()) {
    int h, m;
    if (sscanf(v["at"].as<const char *>(), "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
      err = "at must be \"HH:MM\" (24 h) or Unix seconds";
      return 0;
    }
    struct tm tmv;
    localtime_r(&now, &tmv);
    tmv.tm_hour = h; tmv.tm_min = m; tmv.tm_sec = 0; tmv.tm_isdst = -1;
    return (int64_t)mktime(&tmv);
  }
  if (v["at"].is<long long>()) return v["at"].as<long long>();
  err = "each alert needs \"at\" (\"HH:MM\" or Unix seconds) or \"in\" (seconds from now)";
  return 0;
}

// ---- replies
void sendJson(int code, JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

void sendError(int code, const String &msg) {
  JsonDocument doc;
  doc["error"] = msg;
  sendJson(code, doc);
}

bool authed() {
  if (!strlen(ALERT_TOKEN) || server.header("X-Token") == ALERT_TOKEN) return true;
  sendError(401, "missing or wrong X-Token header");
  return false;
}

// The body must arrive with "Content-Type: application/json": with curl's default form type the
// server parses the body as form fields and the JSON is lost.
bool readBody(JsonDocument &doc) {
  if (!server.hasArg("plain") || deserializeJson(doc, server.arg("plain"))) {
    sendError(400, "body must be JSON, sent with the header Content-Type: application/json");
    return false;
  }
  return true;
}

void hhmm(int64_t at, char *buf, size_t n) {
  time_t tt = (time_t)at;
  struct tm tmv;
  localtime_r(&tt, &tmv);
  strftime(buf, n, "%H:%M", &tmv);
}

// ---- handlers
void handleStatus() {
  if (!authed()) return;
  static const char *STATE_NAME[] = {"idle", "running", "paused", "done"};
  JsonDocument doc;
  char buf[32];
  time_t now = time(nullptr);
  doc["clock_synced"] = clockSynced();
  if (clockSynced()) {
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmv);
    doc["time"] = buf;
  }
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["screen"] = screenOff ? "off" : "on";
  doc["timer"]["mode"] = MODE_LABEL[mode];
  doc["timer"]["state"] = STATE_NAME[state];
  doc["timer"]["remaining_s"] = (remainMs + 999) / 1000;
  doc["alert"]["active"] = alertActive;
  if (alertActive) doc["alert"]["label"] = curAlert.label;
  doc["alert"]["queued"] = alertQueued;
  JsonArray arr = doc["schedule"].to<JsonArray>();
  for (int i = 0; i < schedCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    hhmm(sched[i].at, buf, sizeof buf);
    o["time"] = buf;
    o["at"] = sched[i].at;
    o["label"] = sched[i].a.label;
    snprintf(buf, sizeof buf, "#%02X%02X%02X", sched[i].a.r, sched[i].a.g, sched[i].a.b);
    o["color"] = buf;
    o["beep"] = sched[i].a.beep;
    o["fired"] = sched[i].fired;
  }
  sendJson(200, doc);
}

void handleAlertPost() {
  if (!authed()) return;
  JsonDocument doc;
  if (!readBody(doc)) return;
  Alert a;
  String err;
  if (!parseAlert(doc, a, err)) return sendError(400, err);
  if (!showAlert(a)) return sendError(429, "alert queue is full");
  Serial.printf("[net] alert \"%s\"\n", a.label);
  JsonDocument out;
  out["ok"] = true;
  out["queued"] = alertQueued;
  sendJson(200, out);
}

void handleAlertDelete() {
  if (!authed()) return;
  alertQueued = 0;
  if (alertActive) dismissAlert();
  JsonDocument out;
  out["ok"] = true;
  sendJson(200, out);
}

// Replaces the whole schedule with the alerts in the body.
void handleSchedulePut() {
  if (!authed()) return;
  JsonDocument doc;
  if (!readBody(doc)) return;
  if (!clockSynced()) return sendError(503, "clock not synced yet, try again in a few seconds");
  JsonArrayConst arr = doc.is<JsonArrayConst>() ? doc.as<JsonArrayConst>() : doc["alerts"].as<JsonArrayConst>();
  if (arr.isNull()) return sendError(400, "expected {\"alerts\": [...]}");
  if ((int)arr.size() > MAX_SCHED) return sendError(400, "too many alerts (max 32)");

  static SchedEntry next[MAX_SCHED];
  int n = 0;
  JsonDocument out;
  JsonArray skipped = out["skipped_past"].to<JsonArray>();
  time_t now = time(nullptr);
  for (JsonVariantConst v : arr) {
    String err;
    SchedEntry e;
    e.fired = false;
    if (!parseAlert(v, e.a, err)) return sendError(400, err);
    e.at = parseWhen(v, err);
    if (!e.at) return sendError(400, err);
    if (e.at < (int64_t)now - 60) { skipped.add(e.a.label); continue; }  // already past: not scheduled
    next[n++] = e;
  }
  memcpy(sched, next, sizeof(SchedEntry) * n);
  schedCount = n;
  saveSchedule();
  Serial.printf("[net] schedule replaced: %d alerts\n", n);
  out["ok"] = true;
  out["scheduled"] = n;
  sendJson(200, out);
}

void handleScheduleDelete() {
  if (!authed()) return;
  schedCount = 0;
  saveSchedule();
  JsonDocument out;
  out["ok"] = true;
  sendJson(200, out);
}

// ---- lifecycle
void netStart() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(NET_HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void netStop() {
  if (netUp) { server.stop(); MDNS.end(); }
  netUp = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// Wi-Fi on/off from the settings screen; remembered across restarts.
void setWifi(bool on) {
  wifiOn = on;
  prefs.putBool("wifi", on);
  if (on) netStart(); else netStop();
  Serial.printf("[net] wifi %s\n", on ? "on" : "off");
}

// Unix time of the next scheduled alert that has not fired, or 0.
int64_t nextSchedAt() {
  int64_t best = 0;
  for (int i = 0; i < schedCount; i++)
    if (!sched[i].fired && (!best || sched[i].at < best)) best = sched[i].at;
  return best;
}

void netSetup() {
  prefs.begin("timebox", false);
  loadSchedule();
  wifiOn = prefs.getBool("wifi", true);
  if (wifiOn) netStart();

  static const char *HEADERS[] = {"X-Token"};
  server.collectHeaders(HEADERS, 1);
  server.on("/", HTTP_GET, handleStatus);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/alert", HTTP_POST, handleAlertPost);
  server.on("/alert", HTTP_DELETE, handleAlertDelete);
  server.on("/schedule", HTTP_GET, handleStatus);
  server.on("/schedule", HTTP_PUT, handleSchedulePut);
  server.on("/schedule", HTTP_POST, handleSchedulePut);
  server.on("/schedule", HTTP_DELETE, handleScheduleDelete);
  server.onNotFound([]() { sendError(404, "not found"); });
}

void netLoop() {
  // not connected for 20 s (first attempt failed, or the network went away): start over
  static uint32_t lastOk = 0;
  if (!wifiOn || WiFi.status() == WL_CONNECTED) lastOk = millis();
  else if (millis() - lastOk > 20000) {
    Serial.println("[net] reconnecting");
    lastOk = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }

  if (wifiOn && WiFi.status() == WL_CONNECTED) {
    if (!netUp) {  // first connection: start the services
      netUp = true;
      configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");
      MDNS.begin(NET_HOSTNAME);
      MDNS.addService("http", "tcp", 80);
      server.begin();
      Serial.printf("[net] http://%s.local  (%s)\n", NET_HOSTNAME, WiFi.localIP().toString().c_str());
    }
    server.handleClient();
  }

  // fire scheduled alerts that are due (also with Wi-Fi off: the clock keeps running)
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < 500 || !clockSynced()) return;
  lastCheck = millis();
  int64_t now = time(nullptr);
  bool changed = false;
  for (int i = 0; i < schedCount; i++) {
    if (sched[i].fired || sched[i].at > now) continue;
    sched[i].fired = true;
    changed = true;
    if (now - sched[i].at < 30 * 60) showAlert(sched[i].a);  // older than 30 min (knob was off): dropped
  }
  if (changed) saveSchedule();
}
