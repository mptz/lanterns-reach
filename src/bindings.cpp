// The harness key map — the ONE table of bindings, mirroring input/bindings
// (the subset the v0.1 harness uses). No rebinding UI. Window management (Esc, the recapture
// click, mouse-look) is not here: it is outside the intention layer.

#include <GLFW/glfw3.h>

#include "intentions.h"

namespace {

using D = Device;
using S = ShiftReq;
using K = IntentionKind;
using H = HandRef;

constexpr Binding kBindings[] = {
    // Movement (continuous). Shift is only a modifier, so it doesn't block these.
    {D::Key, GLFW_KEY_W, S::Any, K::Move, H::None, 0},  // forward +
    {D::Key, GLFW_KEY_S, S::Any, K::Move, H::None, 1},  // forward −
    {D::Key, GLFW_KEY_D, S::Any, K::Move, H::None, 2},  // right +
    {D::Key, GLFW_KEY_A, S::Any, K::Move, H::None, 3},  // right −
    {D::Key, GLFW_KEY_C, S::Any, K::Ascend},
    {D::Key, GLFW_KEY_V, S::Any, K::Descend},
    {D::Key, GLFW_KEY_LEFT_CONTROL, S::Any, K::Fast},
    {D::Key, GLFW_KEY_RIGHT_CONTROL, S::Any, K::Fast},

    // Per-hand use: LMB = left hand, RMB = right hand; Shift+click = secondary.
    {D::MouseButton, GLFW_MOUSE_BUTTON_LEFT, S::Without, K::Use, H::Left},
    {D::MouseButton, GLFW_MOUSE_BUTTON_LEFT, S::With, K::Use, H::Left, 0, true},
    {D::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, S::Without, K::Use, H::Right},
    {D::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, S::With, K::Use, H::Right, 0, true},
    {D::MouseButton, GLFW_MOUSE_BUTTON_MIDDLE, S::Any, K::PickBlock},

    // Hotbars: 1–9 / wheel = main hand, Shift+ = off hand. Wheel down = next slot.
    {D::Key, GLFW_KEY_1, S::Without, K::SelectSlot, H::Main, 0}, {D::Key, GLFW_KEY_1, S::With, K::SelectSlot, H::Off, 0},
    {D::Key, GLFW_KEY_2, S::Without, K::SelectSlot, H::Main, 1}, {D::Key, GLFW_KEY_2, S::With, K::SelectSlot, H::Off, 1},
    {D::Key, GLFW_KEY_3, S::Without, K::SelectSlot, H::Main, 2}, {D::Key, GLFW_KEY_3, S::With, K::SelectSlot, H::Off, 2},
    {D::Key, GLFW_KEY_4, S::Without, K::SelectSlot, H::Main, 3}, {D::Key, GLFW_KEY_4, S::With, K::SelectSlot, H::Off, 3},
    {D::Key, GLFW_KEY_5, S::Without, K::SelectSlot, H::Main, 4}, {D::Key, GLFW_KEY_5, S::With, K::SelectSlot, H::Off, 4},
    {D::Key, GLFW_KEY_6, S::Without, K::SelectSlot, H::Main, 5}, {D::Key, GLFW_KEY_6, S::With, K::SelectSlot, H::Off, 5},
    {D::Key, GLFW_KEY_7, S::Without, K::SelectSlot, H::Main, 6}, {D::Key, GLFW_KEY_7, S::With, K::SelectSlot, H::Off, 6},
    {D::Key, GLFW_KEY_8, S::Without, K::SelectSlot, H::Main, 7}, {D::Key, GLFW_KEY_8, S::With, K::SelectSlot, H::Off, 7},
    {D::Key, GLFW_KEY_9, S::Without, K::SelectSlot, H::Main, 8}, {D::Key, GLFW_KEY_9, S::With, K::SelectSlot, H::Off, 8},
    {D::Wheel, +1, S::Without, K::Scroll, H::Main, -1}, {D::Wheel, +1, S::With, K::Scroll, H::Off, -1},
    {D::Wheel, -1, S::Without, K::Scroll, H::Main, +1}, {D::Wheel, -1, S::With, K::Scroll, H::Off, +1},
    {D::Key, GLFW_KEY_H, S::Without, K::Bare, H::Main}, {D::Key, GLFW_KEY_H, S::With, K::Bare, H::Off},
    {D::Key, GLFW_KEY_F, S::Any, K::SwapHotbars},

    // Preview mode (Q), the held container's placement mode (M, for now), and
    // the orientation modifier (X, held): while it's held the orientation
    // context remaps the WHEEL to turning the placement ghost — plain wheel
    // turns it about the clicked (support) face's normal (up = CW, down =
    // CCW), Shift + wheel tips it about screen-right made perpendicular to
    // that normal (up = top away, down = top toward you). The mouse buttons
    // still place, so a row is placed with X held. RotateGhost's `secondary`
    // = the tip axis.
    {D::Key, GLFW_KEY_Q, S::Any, K::TogglePreview},
    {D::Key, GLFW_KEY_M, S::Any, K::CycleMode},
    {D::Key, GLFW_KEY_X, S::Any, K::OrientModifier},
    {D::Wheel, +1, S::Without, K::RotateGhost, H::None, +1, false, Context::Orientation},
    {D::Wheel, -1, S::Without, K::RotateGhost, H::None, -1, false, Context::Orientation},
    {D::Wheel, +1, S::With, K::RotateGhost, H::None, +1, true, Context::Orientation},
    {D::Wheel, -1, S::With, K::RotateGhost, H::None, -1, true, Context::Orientation},

    // Debug overlays (input/debug-keys). Shift+F2 cycles the buffer view backward.
    {D::Key, GLFW_KEY_F1, S::Any, K::ToggleOverlay, H::None, 1},
    {D::Key, GLFW_KEY_F2, S::Without, K::ToggleOverlay, H::None, 2},
    {D::Key, GLFW_KEY_F2, S::With, K::ToggleOverlay, H::None, 2, true},
    {D::Key, GLFW_KEY_F3, S::Any, K::ToggleOverlay, H::None, 3},
    {D::Key, GLFW_KEY_F4, S::Any, K::ToggleOverlay, H::None, 4},
    {D::Key, GLFW_KEY_F5, S::Any, K::ToggleOverlay, H::None, 5},
    {D::Key, GLFW_KEY_F6, S::Any, K::ToggleOverlay, H::None, 6},
    {D::Key, GLFW_KEY_F7, S::Any, K::ToggleOverlay, H::None, 7},
    {D::Key, GLFW_KEY_F8, S::Any, K::ToggleOverlay, H::None, 8},
    {D::Key, GLFW_KEY_F9, S::Any, K::ToggleOverlay, H::None, 9},
    {D::Key, GLFW_KEY_F10, S::Any, K::ToggleOverlay, H::None, 10},
    // Unbound for now: 0, E, G, R, T, Tab.
};

}  // namespace

std::span<const Binding> bindings() { return kBindings; }
