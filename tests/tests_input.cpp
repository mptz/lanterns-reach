// Tests: input. Registered through sandbox_tests.cpp.

#include "test_support.h"

namespace {

using namespace orient;

// --- hands, hotbars, use repeat --------------------------------------------------

Hotbar threeSlots() { return Hotbar({Item::blockStack(1), Item::blockStack(2), Item::blockStack(3)}); }

void testHotbar() {
    Hotbar h = threeSlots();
    CHECK(h.selected() == 0 && h.held() && h.held()->block == 1);
    h.scroll(-1);
    CHECK(h.selected() == 2);  // wraps backward
    h.scroll(+1);
    CHECK(h.selected() == 0);  // and forward
    h.select(1);
    h.setBare();
    CHECK(h.isBare() && !h.held() && h.lastSelected() == 1);
    h.scroll(+1);
    CHECK(h.selected() == 1);  // the wheel resumes at the last selected slot (bare is not a stop)
    h.setBare();
    h.select(2);
    CHECK(h.selected() == 2);  // selecting a slot leaves bare
    h.select(7);
    CHECK(h.selected() == 2);  // no slot 8: ignored
    h.pick(3);
    CHECK(h.selected() == 2);  // already on the hotbar: just select it
    h.pick(9);
    CHECK(h.selected() == 2 && h.slots()[2].block == 9);  // not on it: replaces the selected slot
    h.select(0);
    h.setBare();
    h.pick(8);
    CHECK(h.selected() == 0 && h.slots()[0].block == 8);  // bare: replaces the last selected slot
    CHECK(h.slots().size() == 3);

    Hands hands;  // right-handed
    CHECK(hands.main() == Hand::Right && hands.off() == Hand::Left);
    hands.bar(Hand::Right) = threeSlots();
    hands.bar(Hand::Right).select(2);
    hands.bar(Hand::Left) = Hotbar({Item::pick()});
    hands.swapHotbars();  // each hotbar keeps its selection
    CHECK(hands.bar(Hand::Left).selected() == 2 && hands.bar(Hand::Left).held()->block == 3);
    CHECK(hands.bar(Hand::Right).held()->kind == Item::Kind::Pick);
}

void testUseRepeater() {
    UseRepeater r;
    CHECK(r.update(true, true, 0.0) == 1);    // fires on press
    CHECK(r.update(true, false, 0.2) == 0);   // still inside the 250 ms delay
    CHECK(r.update(true, false, 0.25) == 1);  // first repeat at 250 ms
    CHECK(r.update(true, false, 0.40) == 0);
    CHECK(r.update(true, false, 0.45) == 1);  // then every 200 ms
    CHECK(r.update(true, false, 0.86) == 2);  // a long frame catches up (repeats due at 0.65 and 0.85)
    CHECK(r.update(false, false, 0.9) == 0);  // released
    CHECK(r.update(true, false, 1.5) == 0);   // held without a press we saw: nothing
}

// --- intention layer ------------------------------------------------------------------

// v0.1.8: F swaps the hotbars; R is unbound.
void testSwapKey() {
    IntentionLayer layer(Hand::Right);
    layer.push({Device::Key, GLFW_KEY_R, true, false});
    CHECK(layer.process({}).fired.empty());
    layer.push({Device::Key, GLFW_KEY_F, true, false});
    const IntentionFrame f = layer.process({});
    CHECK(f.fired.size() == 1 && f.fired[0].kind == IntentionKind::SwapHotbars);
}

// v0.1.8: free flight is camera-relative; C/V move along the camera's up
// (right × forward), perpendicular to the look direction.
void testCameraRelativeAscend() {
    Camera cam;
    cam.yaw = 0.0;      // looking along +x …
    cam.pitch = -1.0;   // … and down
    const glm::dvec3 u = cam.up();
    CHECK(std::abs(glm::dot(u, cam.forward())) < 1e-12 && std::abs(glm::dot(u, cam.right())) < 1e-12);
    CHECK(std::abs(glm::length(u) - 1.0) < 1e-12);
    const WorldPos start = cam.position;
    cam.update({.up = 1.0}, 0.5);
    const glm::dvec3 d = cam.position - start;
    CHECK(d.x > 0.0 && d.z > 0.0 && std::abs(d.y) < 1e-12);  // forward-and-up in the world, not straight up
    CHECK(std::abs(glm::dot(glm::normalize(d), cam.forward())) < 1e-12);
    cam.pitch = 0.0;  // level: up is world up
    CHECK(glm::length(cam.up() - glm::dvec3(0, 0, 1)) < 1e-12);
}

void testIntentions() {
    IntentionLayer layer(Hand::Right);
    auto press = [&](Device d, int code, bool shift = false) { layer.push({d, code, true, shift}); };
    auto release = [&](Device d, int code) { layer.push({d, code, false, false}); };

    // Shift routing: 1–9 and the wheel are main-hand; Shift+ is the off hand.
    press(Device::Key, GLFW_KEY_3);
    press(Device::Key, GLFW_KEY_3, true);
    press(Device::Wheel, -1);
    press(Device::Wheel, +1, true);
    press(Device::Key, GLFW_KEY_H, true);
    press(Device::MouseButton, GLFW_MOUSE_BUTTON_RIGHT);
    press(Device::MouseButton, GLFW_MOUSE_BUTTON_LEFT, true);
    press(Device::Key, GLFW_KEY_F2, true);
    press(Device::Key, GLFW_KEY_E);  // unbound
    IntentionFrame f = layer.process({});
    CHECK(f.fired.size() == 8);
    if (f.fired.size() == 8) {
        CHECK(f.fired[0].kind == IntentionKind::SelectSlot && f.fired[0].hand == Hand::Right && f.fired[0].arg == 2);
        CHECK(f.fired[1].kind == IntentionKind::SelectSlot && f.fired[1].hand == Hand::Left && f.fired[1].arg == 2);
        CHECK(f.fired[2].kind == IntentionKind::Scroll && f.fired[2].hand == Hand::Right && f.fired[2].arg == +1);
        CHECK(f.fired[3].kind == IntentionKind::Scroll && f.fired[3].hand == Hand::Left && f.fired[3].arg == -1);
        CHECK(f.fired[4].kind == IntentionKind::Bare && f.fired[4].hand == Hand::Left);
        CHECK(f.fired[5].kind == IntentionKind::Use && f.fired[5].hand == Hand::Right && !f.fired[5].secondary);
        CHECK(f.fired[6].kind == IntentionKind::Use && f.fired[6].hand == Hand::Left && f.fired[6].secondary);
        CHECK(f.fired[7].kind == IntentionKind::ToggleOverlay && f.fired[7].arg == 2 && f.fired[7].secondary);
    }
    CHECK(f.useHeld[1] && !f.useHeld[0]);  // RMB primary held; LMB was a secondary
    CHECK(f.contexts.size() == 1 && f.contexts[0] == "gameplay");

    // Continuous intentions from held keys; Shift doesn't block them.
    press(Device::Key, GLFW_KEY_W, true);
    press(Device::Key, GLFW_KEY_A);
    press(Device::Key, GLFW_KEY_C);
    press(Device::Key, GLFW_KEY_LEFT_CONTROL);
    f = layer.process({});
    CHECK(f.fired.empty() && f.forward == 1.0 && f.right == -1.0 && f.up == 1.0 && f.fast);

    // The dev-UI context: while ImGui wants the keyboard, held keys stop
    // driving movement and new key presses are swallowed; the mouse still works.
    press(Device::Key, GLFW_KEY_F);
    press(Device::MouseButton, GLFW_MOUSE_BUTTON_MIDDLE);
    f = layer.process({.devUiWantsMouse = false, .devUiWantsKeyboard = true});
    CHECK(f.forward == 0.0 && !f.fast && f.contexts.size() == 2 && f.contexts[1] == "dev-ui (keyboard)");
    CHECK(f.fired.size() == 1 && f.fired[0].kind == IntentionKind::PickBlock);
    // Releases pass through even while swallowed, so nothing stays held.
    release(Device::Key, GLFW_KEY_W);
    release(Device::MouseButton, GLFW_MOUSE_BUTTON_RIGHT);
    f = layer.process({.devUiWantsMouse = true, .devUiWantsKeyboard = true});
    f = layer.process({});
    CHECK(f.forward == 0.0 && f.right == -1.0 && !f.useHeld[1]);
    // A mouse press while ImGui wants the mouse fires nothing and isn't held.
    press(Device::MouseButton, GLFW_MOUSE_BUTTON_LEFT);
    f = layer.process({.devUiWantsMouse = true});
    CHECK(f.fired.empty());
    f = layer.process({});
    CHECK(!f.useHeld[0]);
}


}  // namespace

void runInputTests() {
    testHotbar();
    testUseRepeater();
    testSwapKey();
    testCameraRelativeAscend();
    testIntentions();
}
