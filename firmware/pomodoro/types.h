#pragma once
// Kept in a header so the Arduino preprocessor sees these before its auto-generated prototypes.

enum Mode { FOCUS, SHORT_BREAK, LONG_BREAK };
enum State { IDLE, RUNNING, PAUSED, DONE };

struct RGBf { float r, g, b; };
struct Bubble { float x, y, v, r; };

// An alert pushed by an agent: a label shown full screen, flashing in a colour, until the knob is pressed.
struct Alert { char label[64]; uint8_t r, g, b; bool beep; };
// A scheduled alert: fires when the clock reaches `at` (Unix seconds).
struct SchedEntry { int64_t at; Alert a; bool fired; };
