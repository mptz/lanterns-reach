#pragma once
// Window management and raw input (outside the intention layer): mouse
// capture for mouse-look, Esc, the recapture click, and GLFW's callbacks
// feeding raw events to the intention layer.

struct GLFWwindow;
class Renderer;
class IntentionLayer;

struct App {
    Renderer* renderer = nullptr;
    IntentionLayer* intentions = nullptr;
    bool captured = false;
    bool swallowLeftRelease = false;  // the recapture click's release
    bool haveCursor = false;
    double lastX = 0.0, lastY = 0.0;
    double mouseDX = 0.0, mouseDY = 0.0;
};

// Installs the callbacks on `w` (window user pointer = `app`), before the dev
// UI so ImGui chains after them.
void installInputCallbacks(GLFWwindow* w, App* app);

// Captured: cursor hidden and locked, mouse motion drives the camera and the
// mouse buttons/wheel are game input. Released: normal cursor (dev windows,
// resizing), no mouse look, no mouse game input.
void setCapture(GLFWwindow* w, bool capture);
