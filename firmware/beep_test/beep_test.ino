// Timebox Knob - beeper smoke test
// Elecrow CrowPanel 1.28" ESP32-S3 rotary display + Adafruit PAM8904 piezo amp.
// Amp signal wire is on the I2C port's SDA pin (GPIO 38). Do NOT call Wire.begin()
// on 38/39 in the same sketch; the pin is used here as a plain PWM output.
// Board: "ESP32S3 Dev Module". Flash via the MX1.25-to-USB cable, with the
// battery power lead unplugged.

const int BEEP_PIN  = 38;    // I2C port SDA
const int BEEP_HZ   = 2700;  // near the 30 mm piezo's resonance = loudest
const int BEEP_RES  = 8;     // 8-bit duty

void setup() {
  ledcAttach(BEEP_PIN, BEEP_HZ, BEEP_RES);   // ESP32 Arduino core 3.x API
}

void beep(int ms) {
  ledcWrite(BEEP_PIN, 128);   // 50 % duty square wave
  delay(ms);
  ledcWrite(BEEP_PIN, 0);
}

void loop() {
  beep(120);
  delay(880);
}
