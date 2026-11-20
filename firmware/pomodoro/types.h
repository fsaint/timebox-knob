#pragma once
// Kept in a header so the Arduino preprocessor sees these before its auto-generated prototypes.

enum Mode { FOCUS, SHORT_BREAK, LONG_BREAK };
enum State { IDLE, RUNNING, PAUSED, DONE };

struct RGBf { float r, g, b; };
struct Bubble { float x, y, v, r; };
