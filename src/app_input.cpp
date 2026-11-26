#include "app_input.h"

#include <GLFW/glfw3.h>

#include "dev_ui.h"
#include "intentions.h"
#include "renderer.h"

namespace {

App* app(GLFWwindow* w) { return static_cast<App*>(glfwGetWindowUserPointer(w)); }

bool shiftHeld(GLFWwindow* w) {
    return glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
}

}  // namespace

void setCapture(GLFWwindow* w, bool capture) {
    App* a = app(w);
    a->captured = capture;
    a->haveCursor = false;  // re-seed lastX/Y so the switch causes no jump
    a->mouseDX = a->mouseDY = 0.0;
    glfwSetInputMode(w, GLFW_CURSOR, capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(w, GLFW_RAW_MOUSE_MOTION, capture ? GLFW_TRUE : GLFW_FALSE);
    if (a->renderer) DevUi::setMouseEnabled(!capture);
    if (!capture && a->intentions) {  // nothing stays held across a release of the mouse
        for (int b : {GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT, GLFW_MOUSE_BUTTON_MIDDLE})
            a->intentions->push({Device::MouseButton, b, false, false});
    }
}

namespace {

void onKey(GLFWwindow* w, int key, int, int action, int mods) {
    App* a = app(w);
    if (key == GLFW_KEY_ESCAPE) {  // window management, not an intention
        if (action == GLFW_PRESS && !DevUi::wantsKeyboard()) {
            if (a->captured) setCapture(w, false);
            else glfwSetWindowShouldClose(w, GLFW_TRUE);
        }
        return;
    }
    if (action == GLFW_REPEAT || !a->intentions) return;
    a->intentions->push({Device::Key, key, action == GLFW_PRESS, (mods & GLFW_MOD_SHIFT) != 0});
}

void onMouseButton(GLFWwindow* w, int button, int action, int mods) {
    App* a = app(w);
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE && a->swallowLeftRelease) {
        a->swallowLeftRelease = false;
        return;
    }
    if (!a->captured) {
        // Released: a left-click on the scene (not on a dev window) only
        // recaptures. It is swallowed — press and release — so it never
        // reaches the intention layer (it can't also break a block).
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && !DevUi::wantsMouse()) {
            setCapture(w, true);
            a->swallowLeftRelease = true;
        }
        return;
    }
    if (a->intentions)
        a->intentions->push({Device::MouseButton, button, action == GLFW_PRESS, (mods & GLFW_MOD_SHIFT) != 0});
}

void onScroll(GLFWwindow* w, double, double dy) {
    App* a = app(w);
    if (!a->captured || !a->intentions || dy == 0.0) return;
    a->intentions->push({Device::Wheel, dy > 0.0 ? +1 : -1, true, shiftHeld(w)});
}

void onFocus(GLFWwindow* w, int focused) {
    if (!focused && app(w)->captured) setCapture(w, false);
}

void onCursor(GLFWwindow* w, double x, double y) {
    App* a = app(w);
    if (!a->captured) return;
    if (a->haveCursor) {
        a->mouseDX += x - a->lastX;
        a->mouseDY += y - a->lastY;
    }
    a->haveCursor = true;
    a->lastX = x;
    a->lastY = y;
}

void onFramebufferSize(GLFWwindow* w, int, int) {
    if (app(w)->renderer) app(w)->renderer->notifyResized();
}

}  // namespace

void installInputCallbacks(GLFWwindow* w, App* a) {
    glfwSetWindowUserPointer(w, a);
    glfwSetKeyCallback(w, onKey);
    glfwSetCursorPosCallback(w, onCursor);
    glfwSetFramebufferSizeCallback(w, onFramebufferSize);
    glfwSetMouseButtonCallback(w, onMouseButton);
    glfwSetScrollCallback(w, onScroll);
    glfwSetWindowFocusCallback(w, onFocus);
}
