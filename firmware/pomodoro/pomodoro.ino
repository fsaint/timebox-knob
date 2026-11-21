// Pomodoro timer prototype for the Elecrow CrowPanel 1.28" ESP32-S3 Rotary Display.
// "Liquid" look: the round screen is a glass whose liquid level drains as time passes.
//
// Controls
//   press knob        start / pause (or skip the end-of-session celebration)
//   turn knob         +/- 1 minute (sets the length before start, adjusts time left while running)
//   hold knob 1 s     reset this session; hold again while reset to skip to the next session type
//
// Sound (piezo amp on GPIO 38): short beeps on the last 5 seconds, a long beep at 0, a short beep on reset.
// Low power: after 2 minutes idle or paused the screen and lights go off; press or turn the knob to wake.
// Settings: tap the gear at the top of the screen, or swipe down. Wi-Fi on/off and connection status.
// Alerts (needs secrets.h): an agent sends a label over Wi-Fi; the screen flashes it and beeps until
//                    the knob is pressed. See net.h and README.md.
//
// PROTOTYPE: no tests, no persistence.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Adafruit_NeoPixel.h>
#include "types.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <Wire.h>

// Wi-Fi alert service: built in only when a secrets.h exists (copy secrets.example.h).
#if __has_include("secrets.h")
#include "secrets.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#define HAVE_WIFI 1
#else
#define HAVE_WIFI 0
#endif
bool wifiOn = false;    // Wi-Fi radio wanted (settings screen); always false in the offline build
bool netUp = false;     // Wi-Fi connected and the web server started
bool screenOff = false; // standby with the processor awake (Wi-Fi builds)

// ---------------------------------------------------------------- pins (from Elecrow factory source)
#define PIN_LCD_PWR1 1
#define PIN_LCD_PWR2 2
#define PIN_BACKLIGHT 46
#define PIN_POWER_LED 40
#define PIN_RGB 48
#define NUM_RGB 5
#define PIN_ENC_A 45
#define PIN_ENC_B 42
#define PIN_ENC_SW 41
// Touch panel (CST816D) on its own I2C bus. This is NOT the 38/39 port the buzzer uses.
#define PIN_TP_SDA 6
#define PIN_TP_SCL 7
#define PIN_TP_RST 13
#define TP_ADDR 0x15
// Piezo amplifier input. GPIO38 is also SDA of the board's main I2C port (SCL is GPIO39),
// so never start I2C on 38/39 (no Wire.begin(38, 39)): the two uses would fight over the pin.
#define PIN_BUZZER 38

// ---------------------------------------------------------------- display driver
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;

 public:
  LGFX() {
    {
      auto c = _bus.config();
      c.spi_host = SPI2_HOST;
      c.spi_mode = 0;
      c.freq_write = 80000000;
      c.freq_read = 20000000;
      c.spi_3wire = true;
      c.use_lock = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = 10;
      c.pin_mosi = 11;
      c.pin_miso = -1;
      c.pin_dc = 3;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs = 9;
      c.pin_rst = 14;
      c.pin_busy = -1;
      c.memory_width = 240;
      c.memory_height = 240;
      c.panel_width = 240;
      c.panel_height = 240;
      c.offset_x = 0;
      c.offset_y = 0;
      c.offset_rotation = 0;
      c.dummy_read_pixel = 8;
      c.dummy_read_bits = 1;
      c.readable = false;
      c.invert = true;
      c.rgb_order = false;
      c.dlen_16bit = false;
      c.bus_shared = false;
      _panel.config(c);
    }
    setPanel(&_panel);
  }
};

LGFX lcd;
LGFX_Sprite fb(&lcd);
Adafruit_NeoPixel rgb(NUM_RGB, PIN_RGB, NEO_GRB + NEO_KHZ800);

static const int W = 240, H = 240, CX = 120, CY = 120;

// ---------------------------------------------------------------- encoder (quadrature decode in ISR)
volatile int8_t encQuarter = 0;
volatile int32_t encDetents = 0;
volatile uint8_t encLast = 0;
portMUX_TYPE encMux = portMUX_INITIALIZER_UNLOCKED;
DRAM_ATTR static const int8_t encTable[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

void IRAM_ATTR encoderISR() {
  uint8_t s = ((uint8_t)digitalRead(PIN_ENC_A) << 1) | (uint8_t)digitalRead(PIN_ENC_B);
  int8_t m = encTable[(encLast << 2) | s];
  portENTER_CRITICAL_ISR(&encMux);
  encLast = s;
  encQuarter += m;
  if (encQuarter >= 4) { encDetents++; encQuarter -= 4; }
  else if (encQuarter <= -4) { encDetents--; encQuarter += 4; }
  portEXIT_CRITICAL_ISR(&encMux);
}

int32_t takeDetents() {
  portENTER_CRITICAL(&encMux);
  int32_t d = encDetents;
  encDetents = 0;
  portEXIT_CRITICAL(&encMux);
  return d;
}

// ---------------------------------------------------------------- beeper
// A beep is a 2.7 kHz square wave: a tenth of a second for a short one. Non-blocking:
// beep() switches the wave on, updateBeeper() switches it off from loop().
const uint32_t BEEP_HZ = 2700, BEEP_SHORT_MS = 100, BEEP_LONG_MS = 800;
bool beepOn = false;
uint32_t beepEnd = 0;

void beep(uint32_t ms = BEEP_SHORT_MS) {
  ledcWrite(PIN_BUZZER, 512);  // 50% duty of 10 bits: square wave
  beepOn = true;
  beepEnd = millis() + ms;
}

void updateBeeper() {
  if (beepOn && (int32_t)(millis() - beepEnd) >= 0) {
    ledcWrite(PIN_BUZZER, 0);  // stop: pin held low
    beepOn = false;
  }
}

// ---------------------------------------------------------------- timer model
const char *MODE_LABEL[] = {"FOCUS", "BREAK", "LONG BREAK"};
int modeMinutes[] = {25, 5, 15};

// liquid colours: surface (top) and depth (bottom) per mode
const RGBf MODE_TOP[] = {{96, 165, 250}, {94, 234, 212}, {251, 191, 36}};
const RGBf MODE_BOT[] = {{124, 58, 237}, {13, 148, 136}, {234, 88, 12}};

// state colours that override the mode colour
const RGBf PAUSED_TOP = {148, 163, 184}, PAUSED_BOT = {51, 65, 85};    // slate: frozen
const RGBf URGENT_TOP = {251, 113, 133}, URGENT_BOT = {220, 38, 38};   // coral-red: almost done
const int32_t URGENT_MS = 3 * 60000;                                    // "last few minutes"

Mode mode = FOCUS;
State state = IDLE;
int32_t totalMs = 25 * 60000;
int32_t remainMs = 25 * 60000;
uint32_t lastTick = 0;
uint32_t doneAt = 0;
int focusDone = 0;  // completed focus sessions in the current set of 4
const uint32_t CELEBRATE_MS = 6000;
const uint32_t SLEEP_AFTER_MS = 2 * 60000;  // idle or paused this long without knob input: low power
uint32_t lastActivity = 0;                  // last knob input or session change
// Compare idle time as a signed value: lastActivity can be set later in the same loop() pass than `now`
// was read, and an unsigned difference would wrap around to a huge idle time.
const int COUNTDOWN_SECS = 5;  // short beep on each of the last seconds
int lastBeepSec = 0;           // last second of the countdown that has beeped

void enterIdle(Mode m) {
  mode = m;
  state = IDLE;
  lastBeepSec = 0;
  lastActivity = millis();
  totalMs = remainMs = modeMinutes[m] * 60000;
  Serial.printf("[pomo] idle %s %d min\n", MODE_LABEL[m], modeMinutes[m]);
}

Mode nextMode() {
  if (mode == FOCUS) return (focusDone % 4 == 0 && focusDone > 0) ? LONG_BREAK : SHORT_BREAK;
  return FOCUS;
}

void finishSession() {
  if (mode == FOCUS) focusDone++;
  state = DONE;
  doneAt = millis();
  remainMs = 0;
  beep(BEEP_LONG_MS);
  Serial.printf("[pomo] done %s (focus sessions: %d)\n", MODE_LABEL[mode], focusDone);
}

void leaveDone() {
  Mode n = nextMode();
  if (mode == LONG_BREAK) focusDone = 0;
  enterIdle(n);
}

// ---------------------------------------------------------------- alerts
// One alert is shown at a time; more wait in a small queue. Pressing the knob stops the current one.
const int ALERT_QUEUE = 8;
Alert curAlert, alertQueue[ALERT_QUEUE];
int alertQueued = 0;
bool alertActive = false;
uint32_t alertSince = 0;
int alertBeepSlot = -1;

void startAlert(const Alert &a) {
  curAlert = a;
  alertActive = true;
  alertSince = millis();
  alertBeepSlot = -1;
  lastActivity = alertSince;
}

bool showAlert(const Alert &a) {
  if (!alertActive) { startAlert(a); return true; }
  if (alertQueued >= ALERT_QUEUE) return false;
  alertQueue[alertQueued++] = a;
  return true;
}

void dismissAlert() {
  Serial.printf("[pomo] alert stopped: %s\n", curAlert.label);
  lastActivity = millis();
  if (alertQueued > 0) {
    Alert next = alertQueue[0];
    for (int i = 1; i < alertQueued; i++) alertQueue[i - 1] = alertQueue[i];
    alertQueued--;
    startAlert(next);
  } else {
    alertActive = false;
  }
}

// two short beeps every 1.5 s while an alert is showing
void alertBeeps(uint32_t now) {
  if (!alertActive || !curAlert.beep) return;
  uint32_t e = now - alertSince;
  int slot = (e / 1500) * 2 + ((e % 1500) >= 250 ? 1 : 0);
  if (slot != alertBeepSlot) { alertBeepSlot = slot; beep(); }
}

// 1 while the alert flash is bright, 0.25 while dim (about 1.7 flashes per second)
float alertPulse(uint32_t now) { return ((now - alertSince) % 600) < 300 ? 1.0f : 0.25f; }

// ---------------------------------------------------------------- settings state
bool settingsOpen = false;
int settingsSel = 0;  // 0 = Wi-Fi row, 1 = Back row
const uint32_t SETTINGS_CLOSE_MS = 15000;

// ---------------------------------------------------------------- visuals state
float level = 0.0f;  // displayed fill 0..1 (eased toward target)
RGBf curTop = MODE_TOP[0], curBot = MODE_BOT[0];
float slosh = 0.0f;  // extra wave energy from knob turns / events
float holdProg = 0.0f;

Bubble bubbles[12];

inline uint16_t c565(float r, float g, float b) {
  return lcd.color565((uint8_t)constrain(r, 0, 255), (uint8_t)constrain(g, 0, 255), (uint8_t)constrain(b, 0, 255));
}
inline RGBf lerpC(const RGBf &a, const RGBf &b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

void spawnBubble(Bubble &b, bool anywhere) {
  b.x = random(30, 210);
  b.y = anywhere ? random(120, 240) : 240 + random(0, 40);
  b.v = 0.35f + random(0, 100) / 160.0f;
  b.r = 1.0f + random(0, 100) / 45.0f;
}

float waveY(float base, float x, float t, float amp, float phase) {
  return base + amp * sinf(x * 0.040f + t * 1.9f + phase) + amp * 0.55f * sinf(x * 0.093f - t * 2.7f + phase * 1.7f);
}

void drawLiquid(float t) {
  // background: very dark, slight vertical gradient
  for (int y = 0; y < H; y += 4) {
    float k = y / (float)H;
    fb.fillRect(0, y, W, 4, c565(4 + 6 * k, 6 + 8 * k, 12 + 14 * k));
  }

  float surf = 232.0f - level * 206.0f;  // full ~26, empty ~232
  bool calm = (state != RUNNING);
  float amp = (calm ? 2.2f : 4.0f) + slosh * 7.0f;

  // two layers: a dim one behind, the bright one in front
  for (int layer = 0; layer < 2; layer++) {
    float base = layer == 0 ? surf - 4 : surf;
    float ph = layer == 0 ? 2.1f : 0.0f;
    float dim = layer == 0 ? 0.45f : 1.0f;
    float a = layer == 0 ? amp * 0.8f : amp;
    int ys[W];
    int yMin = H, yMax = 0;
    for (int x = 0; x < W; x++) {
      int y = (int)waveY(base, x, t * (layer == 0 ? 0.8f : 1.0f), a, ph);
      if (y < 0) y = 0;
      ys[x] = y;
      if (y < yMin) yMin = y;
      if (y > yMax) yMax = y;
    }
    float span = max(1.0f, (float)H - yMin);
    // solid body below the deepest trough: whole rows
    for (int y = yMax + 1; y < H; y++) {
      RGBf c = lerpC(curTop, curBot, (y - yMin) / span);
      fb.drawFastHLine(0, y, W, c565(c.r * dim, c.g * dim, c.b * dim));
    }
    // the wavy band
    for (int x = 0; x < W; x++) {
      for (int y = ys[x]; y <= yMax && y < H; y++) {
        RGBf c = lerpC(curTop, curBot, (y - yMin) / span);
        fb.drawPixel(x, y, c565(c.r * dim, c.g * dim, c.b * dim));
      }
      if (layer == 1 && ys[x] < H) {  // bright meniscus line on the surface
        fb.drawPixel(x, ys[x], c565(curTop.r * 0.5f + 128, curTop.g * 0.5f + 128, curTop.b * 0.5f + 128));
      }
    }
  }

  // bubbles rising through the liquid while running
  for (auto &b : bubbles) {
    if (state == RUNNING) b.y -= b.v;
    float top = waveY(surf, b.x, t, amp, 0) + 4;
    if (b.y < top) { spawnBubble(b, false); continue; }
    if (b.y < H) {
      b.x += 0.25f * sinf(t * 2 + b.y * 0.05f);
      RGBf c = lerpC(curTop, {255, 255, 255}, 0.55f);
      fb.drawCircle((int)b.x, (int)b.y, (int)b.r + 1, c565(c.r, c.g, c.b));
    }
  }

  // glass rim + highlight
  fb.drawCircle(CX, CY, 119, c565(40, 46, 60));
  fb.drawArc(CX, CY, 116, 113, 200, 250, c565(70, 80, 100));
}

void drawText(float t) {
  int32_t shownMs = state == DONE ? 0 : remainMs;
  int secs = (shownMs + 999) / 1000;
  char buf[8];
  snprintf(buf, sizeof buf, "%02d:%02d", secs / 60, secs % 60);

  fb.setTextDatum(textdatum_t::middle_center);
  fb.setFont(&fonts::FreeSansBold24pt7b);
  fb.setTextSize(1.25f);
  bool blinkOff = state == PAUSED && fmodf(t, 1.2f) > 0.8f;
  if (state == DONE) snprintf(buf, sizeof buf, "DONE");
  if (!blinkOff) {
    fb.setTextColor(c565(0, 0, 0));
    fb.drawString(buf, CX + 2, 104 + 3);
    fb.setTextColor(TFT_WHITE);
    fb.drawString(buf, CX, 104);
  }

  // mode label
  fb.setFont(&fonts::FreeSansBold9pt7b);
  fb.setTextSize(1.0f);
  const char *label = MODE_LABEL[mode];
  char sub[24];
  if (state == DONE) snprintf(sub, sizeof sub, "%s NEXT", MODE_LABEL[nextMode()]);
  fb.setTextColor(c565(0, 0, 0));
  fb.drawString(state == DONE ? sub : label, CX + 1, 146 + 1);
  fb.setTextColor(c565(235, 240, 255));
  fb.drawString(state == DONE ? sub : label, CX, 146);

  // session dots (completed focus sessions in this set of 4)
  int filled = focusDone % 4;
  if (mode == LONG_BREAK || (state == DONE && focusDone % 4 == 0 && focusDone > 0)) filled = 4;
  for (int i = 0; i < 4; i++) {
    int x = CX - 21 + i * 14;
    if (i < filled) fb.fillCircle(x, 170, 4, TFT_WHITE);
    else fb.drawCircle(x, 170, 4, c565(200, 205, 220));
  }

  // hint line
  fb.setFont(&fonts::Font2);
  const char *hint = state == IDLE ? "press to start" : state == PAUSED ? "paused" : nullptr;
  if (hint) {
    fb.setTextColor(c565(0, 0, 0));
    fb.drawString(hint, CX + 1, 192 + 1);
    fb.setTextColor(c565(220, 225, 240));
    fb.drawString(hint, CX, 192);
  }

  // settings gear at the top while idle or paused (tap it, or swipe down)
  if (state == IDLE || state == PAUSED) {
    uint16_t gc = c565(150, 158, 180);
    for (int k = 0; k < 8; k++) fb.fillCircle(CX + (int)roundf(9 * cosf(k * 0.7854f)), 22 + (int)roundf(9 * sinf(k * 0.7854f)), 2, gc);
    fb.fillCircle(CX, 22, 7, gc);
    fb.fillCircle(CX, 22, 3, c565(16, 20, 32));
  }

#if HAVE_WIFI
  // network address while idle, so you know where to send alerts
  if (state == IDLE) {
    fb.setFont(&fonts::Font0);
    fb.setTextColor(c565(150, 158, 180));
    fb.drawString(netUp ? WiFi.localIP().toString().c_str() : "wifi...", CX, 212);
  }
#endif

  // hold-to-reset progress ring
  if (holdProg > 0.02f) {
    fb.drawArc(CX, CY, 119, 113, 270, 270 + (int)(360 * holdProg), TFT_WHITE);
  }
}

// Greedy word wrap with the current font. Returns the number of lines, or -1 if it does not fit.
int wrapLabel(const char *text, int maxW, int maxLines, char lines[][64]) {
  int n = 0;
  lines[0][0] = 0;
  char word[64];
  const char *p = text;
  while (*p) {
    while (*p == ' ') p++;
    int w = 0;
    while (*p && *p != ' ' && w < 63) word[w++] = *p++;
    word[w] = 0;
    if (!w) break;
    char trial[130];
    snprintf(trial, sizeof trial, lines[n][0] ? "%s %s" : "%s%s", lines[n], word);
    if (strlen(trial) < 64 && fb.textWidth(trial) <= maxW) { strcpy(lines[n], trial); continue; }
    if (!lines[n][0] || n + 1 >= maxLines) return -1;  // one word too wide, or out of lines
    strcpy(lines[++n], word);
    if (fb.textWidth(word) > maxW) return -1;
  }
  return n + 1;
}

// Full-screen alert: the whole screen flashes in the alert colour with the label in the middle.
void drawAlert(uint32_t now) {
  float p = alertPulse(now);
  float r = curAlert.r * p, g = curAlert.g * p, b = curAlert.b * p;
  fb.fillScreen(c565(r, g, b));
  bool dark = (0.3f * r + 0.59f * g + 0.11f * b) > 150;  // bright background: dark text
  uint16_t ink = dark ? c565(10, 10, 14) : TFT_WHITE;

  static const lgfx::IFont *FONTS[] = {&fonts::FreeSansBold18pt7b, &fonts::FreeSansBold12pt7b, &fonts::FreeSansBold9pt7b};
  static const int MAX_LINES[] = {3, 4, 5};
  static char lines[5][64];
  int n = -1;
  fb.setTextSize(1.0f);
  fb.setTextDatum(textdatum_t::middle_center);
  for (int f = 0; f < 3 && n < 0; f++) {
    fb.setFont(FONTS[f]);
    n = wrapLabel(curAlert.label, 188, MAX_LINES[f], lines);
  }
  if (n < 0) { strlcpy(lines[0], curAlert.label, 20); n = 1; }  // hopeless: one clipped line, smallest font
  int lh = fb.fontHeight() + 2;
  int y0 = 112 - (n - 1) * lh / 2;
  fb.setTextColor(ink);
  for (int i = 0; i < n; i++) fb.drawString(lines[i], CX, y0 + i * lh);

  fb.setFont(&fonts::Font2);
  fb.drawString("press to stop", CX, 214);
}

// ---------------------------------------------------------------- LED ring
void updateLeds(float t) {
  RGBf c = curTop;
  if (alertActive) {
    float p = alertPulse(millis());
    for (int i = 0; i < NUM_RGB; i++) rgb.setPixelColor(i, rgb.Color(curAlert.r * p, curAlert.g * p, curAlert.b * p));
  } else if (state == DONE) {
    float e = millis() - doneAt;
    float p = e < CELEBRATE_MS ? 0.5f + 0.5f * sinf(t * 9.0f) : 0.35f + 0.25f * sinf(t * 2.0f);
    for (int i = 0; i < NUM_RGB; i++) rgb.setPixelColor(i, rgb.Color(c.r * p, c.g * p, c.b * p));
  } else {
    float f = level * NUM_RGB;
    float breathe = state == RUNNING ? 1.0f : 0.55f + 0.25f * sinf(t * 2.0f);
    for (int i = 0; i < NUM_RGB; i++) {
      float k = constrain(f - i, 0.0f, 1.0f) * breathe;
      rgb.setPixelColor(i, rgb.Color(c.r * k, c.g * k, c.b * k));
    }
  }
  rgb.show();
}

// ---------------------------------------------------------------- input
const uint32_t HOLD_MS = 1000;
bool btnDown = false, holdFired = false;
uint32_t btnSince = 0, lastBtnChange = 0;
uint32_t ignoreTurnsUntil = 0;  // knob turns are discarded until then (the turn that woke us)

void onClick() {
  if (alertActive) { dismissAlert(); return; }  // a press only stops the alert
  if (settingsOpen) { settingsPress(); return; }
  slosh = 0.6f;
  switch (state) {
    case IDLE: state = RUNNING; lastTick = millis(); Serial.println("[pomo] start"); break;
    case RUNNING: state = PAUSED; Serial.println("[pomo] pause"); break;
    case PAUSED: state = RUNNING; lastTick = millis(); Serial.println("[pomo] resume"); break;
    case DONE: leaveDone(); break;
  }
}

void onHold() {
  if (alertActive) { dismissAlert(); return; }
  if (settingsOpen) { settingsOpen = false; return; }
  slosh = 1.0f;
  beep();
  if (state == IDLE) {  // already reset: skip to the next session type
    Mode n = mode == FOCUS ? SHORT_BREAK : FOCUS;
    enterIdle(n);
  } else if (state == DONE) {
    leaveDone();
  } else {
    enterIdle(mode);
  }
}

void pollInput() {
  bool down = digitalRead(PIN_ENC_SW) == LOW;
  uint32_t now = millis();
  if (down != btnDown && now - lastBtnChange > 25) {
    lastBtnChange = now;
    lastActivity = now;
    btnDown = down;
    if (down) { btnSince = now; holdFired = false; }
    else if (!holdFired) onClick();
  }
  holdProg = 0;
  if (btnDown && !holdFired) {
    uint32_t held = now - btnSince;
    if (held > 250) holdProg = min(1.0f, (held - 250) / (float)(HOLD_MS - 250));
    if (held >= HOLD_MS) { holdFired = true; onHold(); }
  }

  int32_t d = takeDetents();
  if (d != 0) lastActivity = now;
  if ((int32_t)(now - ignoreTurnsUntil) < 0 || alertActive) d = 0;
  if (d != 0 && settingsOpen) { settingsSel = constrain(settingsSel + (d > 0 ? 1 : -1), 0, 1); d = 0; }
  if (d != 0 && state != DONE) {
    slosh = min(1.0f, slosh + 0.25f * abs(d));
    if (state == IDLE) {
      int m = constrain(modeMinutes[mode] + d, 1, 90);
      modeMinutes[mode] = m;
      totalMs = remainMs = m * 60000;
    } else {
      remainMs = constrain(remainMs + d * 60000, 1000, 90 * 60000);
      if (remainMs > totalMs) totalMs = remainMs;
    }
  }
}

#if HAVE_WIFI
#include "net.h"
#endif

// ---------------------------------------------------------------- touch
// Minimal CST816D reader: one finger, polled from loop(). Taps and vertical swipes are detected here
// rather than with the chip's gesture register, which differs between CST816 variants.
bool touchSwallow = false;  // ignore the touch in progress until the finger lifts

void touchWake() {
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);
  delay(10);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(50);
  Wire.beginTransmission(TP_ADDR);  // keep the chip awake so polling always gets an answer
  Wire.write(0xFE);
  Wire.write(0x01);
  Wire.endTransmission();
}

void touchSleep() {
  Wire.beginTransmission(TP_ADDR);
  Wire.write(0xE5);
  Wire.write(0x03);
  Wire.endTransmission();
}

void touchSetup() {
  Wire.begin(PIN_TP_SDA, PIN_TP_SCL, 400000);
  touchWake();
}

bool touchRead(int &x, int &y) {
  Wire.beginTransmission(TP_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(TP_ADDR, 5) != 5) return false;
  uint8_t n = Wire.read(), xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  if (n == 0) return false;
  x = ((xh & 0x0F) << 8) | xl;
  y = ((yh & 0x0F) << 8) | yl;
  return true;
}

void openSettings() {
  settingsOpen = true;
  settingsSel = 0;
  lastActivity = millis();
}

void settingsPress() {
  lastActivity = millis();
  if (settingsSel == 1) { settingsOpen = false; return; }
#if HAVE_WIFI
  setWifi(!wifiOn);
#endif
}

void onTap(int x, int y) {
  Serial.printf("[touch] tap %d,%d\n", x, y);
  if (alertActive) return;  // alerts are stopped with the knob only
  if (settingsOpen) {
    if (y >= 60 && y < 98) { settingsSel = 0; settingsPress(); }
    else if (y >= 178 && y < 214) settingsOpen = false;
    return;
  }
  if ((state == IDLE || state == PAUSED) && y < 50 && abs(x - CX) < 45) openSettings();  // the gear
}

void onSwipe(bool down, int startY) {
  Serial.printf("[touch] swipe %s from y=%d\n", down ? "down" : "up", startY);
  if (alertActive) return;
  if (settingsOpen) { if (!down) settingsOpen = false; return; }
  if (down && startY < 110 && state != DONE) openSettings();
}

void pollTouch(uint32_t now) {
  static bool down = false;
  static int x0, y0, x1, y1, misses = 0;
  int x, y;
  if (touchRead(x, y)) {
    misses = 0;
    if (!down) { down = true; x0 = x; y0 = y; }
    x1 = x; y1 = y;
    lastActivity = now;
  } else if (down && ++misses >= 2) {  // finger lifted
    down = false;
    if (touchSwallow) { touchSwallow = false; return; }
    int dx = x1 - x0, dy = y1 - y0;
    if (abs(dx) < 20 && abs(dy) < 20) onTap(x1, y1);
    else if (abs(dy) > abs(dx) && abs(dy) > 35) onSwipe(dy > 0, y0);
  } else if (!down) {
    touchSwallow = false;
  }
}

// ---------------------------------------------------------------- settings screen
// Opened with the gear or a swipe down. Turn = move, press = act, hold or swipe up = close.
void settingsRow(int y, int w, const char *text, bool selected) {
  if (selected) fb.fillRoundRect(CX - w / 2, y - 16, w, 32, 12, c565(60, 110, 235));
  else fb.drawRoundRect(CX - w / 2, y - 16, w, 32, 12, c565(70, 80, 105));
  fb.setFont(&fonts::FreeSansBold9pt7b);
  fb.setTextColor(TFT_WHITE);
  fb.drawString(text, CX, y);
}

void drawSettings() {
  fb.fillScreen(c565(10, 12, 20));
  fb.setTextSize(1.0f);
  fb.setTextDatum(textdatum_t::middle_center);
  fb.setFont(&fonts::FreeSansBold9pt7b);
  fb.setTextColor(c565(150, 158, 180));
  fb.drawString("SETTINGS", CX, 40);

  char l1[40] = "", l2[40] = "", l3[40] = "";
#if HAVE_WIFI
  settingsRow(79, 150, wifiOn ? "Wi-Fi: On" : "Wi-Fi: Off", settingsSel == 0);
  if (!wifiOn) {
    snprintf(l1, sizeof l1, "Radio off");
    snprintf(l2, sizeof l2, "No alerts over Wi-Fi");
  } else if (WiFi.status() == WL_CONNECTED) {
    snprintf(l1, sizeof l1, "Connected: %.24s", WiFi.SSID().c_str());
    snprintf(l2, sizeof l2, "%s  %d dBm", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  } else {
    snprintf(l1, sizeof l1, "Connecting...");
    snprintf(l2, sizeof l2, "%.30s", WIFI_SSID);
  }
  time_t tt = time(nullptr);
  if (tt > 1700000000) {
    struct tm tmv;
    localtime_r(&tt, &tmv);
    strftime(l3, sizeof l3, "Clock %H:%M", &tmv);
  } else {
    snprintf(l3, sizeof l3, "Clock not synced");
  }
#else
  settingsRow(79, 190, "Wi-Fi: not set up", settingsSel == 0);
  snprintf(l1, sizeof l1, "Add secrets.h and reflash");
  snprintf(l2, sizeof l2, "to enable Wi-Fi alerts");
#endif
  fb.setFont(&fonts::Font2);
  fb.setTextColor(c565(205, 212, 230));
  fb.drawString(l1, CX, 114);
  fb.drawString(l2, CX, 132);
  fb.drawString(l3, CX, 150);

  settingsRow(196, 96, "Back", settingsSel == 1);
}

// ---------------------------------------------------------------- low power
// Standby: screen, backlight and LED ring off until the knob is pressed or turned (or an alert arrives).
// The press or turn that wakes the timer is swallowed: it never starts a session or changes the minutes.
//  - Wi-Fi off (or offline build): the processor goes into light sleep. The USB serial port disappears
//    while asleep, so wake it with the knob before flashing. It wakes itself for a scheduled alert.
//  - Wi-Fi on: the processor stays awake so alerts can still arrive; only the screen and lights sleep.
void displayOff() {
  rgb.clear();
  rgb.show();
  ledcWrite(PIN_BACKLIGHT, 0);
  ledcWrite(PIN_BUZZER, 0);
  beepOn = false;
  lcd.sleep();
  screenOff = true;
}

void displayOn() {
  uint32_t now = millis();
  lastActivity = now;
  ignoreTurnsUntil = now + 400;
  takeDetents();
  if (digitalRead(PIN_ENC_SW) == LOW) {  // woken by a press: its release is not a click, its hold not a reset
    btnDown = true;
    holdFired = true;
    lastBtnChange = now;
  }
  lcd.wakeup();
  settingsOpen = false;
  touchSwallow = true;  // a touch that woke the screen does nothing else
  if (alertActive) drawAlert(now);
  else { drawLiquid(now / 1000.0f); drawText(now / 1000.0f); }
  fb.pushSprite(0, 0);
  ledcWrite(PIN_BACKLIGHT, 200);  // backlight last, once the frame is on the panel
  screenOff = false;
  Serial.println("[pomo] wake");
}

void standby() {
  Serial.println("[pomo] standby");
  Serial.flush();
  displayOff();
  if (wifiOn) return;  // stay awake for alerts; loop() idles with the screen off

  // light sleep; wake on button down, or on either encoder line leaving its current level
  touchSleep();
#if HAVE_WIFI
  // also wake in time for the next scheduled alert
  int64_t next = nextSchedAt();
  if (next && clockSynced()) {
    int64_t secs = next - (int64_t)time(nullptr);
    esp_sleep_enable_timer_wakeup((uint64_t)(secs < 1 ? 1 : secs) * 1000000ULL);
  } else {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  }
#endif
  detachInterrupt(digitalPinToInterrupt(PIN_ENC_A));
  detachInterrupt(digitalPinToInterrupt(PIN_ENC_B));
  gpio_wakeup_enable((gpio_num_t)PIN_ENC_SW, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)PIN_ENC_A, digitalRead(PIN_ENC_A) ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  gpio_wakeup_enable((gpio_num_t)PIN_ENC_B, digitalRead(PIN_ENC_B) ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();

  gpio_wakeup_disable((gpio_num_t)PIN_ENC_SW);
  gpio_wakeup_disable((gpio_num_t)PIN_ENC_A);
  gpio_wakeup_disable((gpio_num_t)PIN_ENC_B);
  // the wakeup setting replaced the encoder pins' interrupt type: attach the decoder again,
  // then decode the step that happened while it was detached so detents stay aligned
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encoderISR, CHANGE);
  uint8_t s = ((uint8_t)digitalRead(PIN_ENC_A) << 1) | (uint8_t)digitalRead(PIN_ENC_B);
  portENTER_CRITICAL(&encMux);
  encQuarter += encTable[(encLast << 2) | s];
  encLast = s;
  portEXIT_CRITICAL(&encMux);
  touchWake();
  displayOn();
}

// ---------------------------------------------------------------- main
void setup() {
  Serial.begin(115200);
  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, LOW);
  pinMode(PIN_LCD_PWR1, OUTPUT);
  digitalWrite(PIN_LCD_PWR1, HIGH);
  pinMode(PIN_LCD_PWR2, OUTPUT);
  digitalWrite(PIN_LCD_PWR2, HIGH);

  lcd.init();
  lcd.initDMA();
  lcd.fillScreen(TFT_BLACK);

  ledcAttach(PIN_BACKLIGHT, 5000, 8);
  ledcWrite(PIN_BACKLIGHT, 200);

  // different frequency from the backlight, so the core gives the buzzer its own LEDC timer
  ledcAttach(PIN_BUZZER, BEEP_HZ, 10);
  ledcWrite(PIN_BUZZER, 0);

  fb.setColorDepth(16);
  fb.setPsram(true);
  if (!fb.createSprite(W, H)) Serial.println("[pomo] sprite alloc FAILED");

  rgb.begin();
  rgb.setBrightness(60);
  rgb.clear();
  rgb.show();

  pinMode(PIN_ENC_A, INPUT);
  pinMode(PIN_ENC_B, INPUT);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  encLast = ((uint8_t)digitalRead(PIN_ENC_A) << 1) | (uint8_t)digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encoderISR, CHANGE);

  touchSetup();
#if HAVE_WIFI
  netSetup();
#endif

  for (auto &b : bubbles) spawnBubble(b, true);
  enterIdle(FOCUS);
  level = 0.0f;  // fill up on boot
  Serial.println("[pomo] ready");
}

void loop() {
  uint32_t now = millis();
  float t = now / 1000.0f;

#if HAVE_WIFI
  netLoop();
#endif

  if (screenOff) {  // Wi-Fi standby: wake for an alert or the knob, otherwise idle
    int tx, ty;
    if (alertActive || digitalRead(PIN_ENC_SW) == LOW || encDetents != 0 || touchRead(tx, ty)) displayOn();
    else { delay(20); return; }
  }

  pollInput();
  pollTouch(now);
  updateBeeper();
  if (settingsOpen && (alertActive || (int32_t)(now - lastActivity) > (int32_t)SETTINGS_CLOSE_MS)) settingsOpen = false;

  if (!alertActive && !settingsOpen && (state == IDLE || state == PAUSED) && !btnDown && !beepOn && (int32_t)(now - lastActivity) > (int32_t)SLEEP_AFTER_MS) {
    standby();
    return;
  }

  if (state == RUNNING) {
    remainMs -= (int32_t)(now - lastTick);
    lastTick = now;
    if (remainMs <= 0) { finishSession(); slosh = 1.0f; }
    else {
      // countdown: one short beep as the display reaches 5, 4, 3, 2, 1
      int secs = (remainMs + 999) / 1000;
      if (secs > COUNTDOWN_SECS) lastBeepSec = 0;
      else if (secs != lastBeepSec) { lastBeepSec = secs; beep(); }
    }
  }
  // after the celebration, refill with the next session's colour and wait for a press
  if (state == DONE && now - doneAt > CELEBRATE_MS) leaveDone();

  // ease visuals toward their targets
  float target = state == DONE ? 0.0f : (float)remainMs / (float)totalMs;
  level += (target - level) * 0.06f;
  RGBf wantTop = MODE_TOP[mode], wantBot = MODE_BOT[mode];
  if (state == PAUSED) { wantTop = PAUSED_TOP; wantBot = PAUSED_BOT; }
  else if (state == RUNNING && remainMs <= URGENT_MS) { wantTop = URGENT_TOP; wantBot = URGENT_BOT; }
  curTop = lerpC(curTop, wantTop, 0.05f);
  curBot = lerpC(curBot, wantBot, 0.05f);
  slosh *= 0.97f;

  if (alertActive) drawAlert(now);
  else if (settingsOpen) drawSettings();
  else { drawLiquid(t); drawText(t); }
  alertBeeps(now);
  fb.pushSprite(0, 0);
  updateBeeper();
  updateLeds(t);
}
