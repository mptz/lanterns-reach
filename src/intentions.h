#pragma once
// The intention layer (sim/player-embodiment, input/*): raw input maps to
// INTENTIONS through a stack of mapping contexts. The base context is
// gameplay; an orientation context is pushed while X (the orientation
// modifier) is held, remapping the wheel to ghost rotation; a dev-UI
// context is pushed while ImGui wants the mouse and/or keyboard and swallows
// that device's presses. A press is matched against the topmost context
// that binds it. Releases always pass through
// (so nothing stays stuck "held"), but fire nothing.
//
// Edge intentions (use, select, scroll, bare, swap, pick, toggle) fire on
// press; continuous intentions (move, ascend, descend, fast, use-held) are
// sampled from what is held each frame. All bindings are in ONE table,
// bindings.cpp. Window management (Esc release/quit, the swallowed
// recapture click, mouse-look) stays outside this layer.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "hands.h"

enum class IntentionKind : std::uint8_t {
    Move, Ascend, Descend, Fast, OrientModifier,  // continuous
    Use, SelectSlot, Scroll, Bare, SwapHotbars, PickBlock, ToggleOverlay,
    TogglePreview,    // Q (input/preview-mode)
    CycleMode,        // M: the held container's placement mode (for now)
    RotateGhost,      // orientation context: arg +1 = wheel up, −1 = wheel down; secondary = Shift (tip axis)
};

struct Intention {
    IntentionKind kind;
    Hand hand = Hand::Right;  // physical hand (Use, SelectSlot, Scroll, Bare)
    int arg = 0;              // slot index / scroll ±1 / overlay F-key number
    bool secondary = false;   // Use: secondary action; ToggleOverlay: backward (Shift+F2)
};
std::string describe(const Intention& i);

enum class Device : std::uint8_t { Key, MouseButton, Wheel };

struct RawEvent {
    Device device;
    int code;     // GLFW key / mouse button; Wheel: +1 = wheel up, −1 = wheel down
    bool press;   // false = release (the wheel only presses)
    bool shift;   // Shift held at the time of the event
};

// One binding: (device, code, Shift requirement) → intention. Hands are
// written as main/off for hotbar bindings (resolved through handedness) and
// left/right for the mouse buttons.
enum class ShiftReq : std::uint8_t { Any, Without, With };
enum class HandRef : std::uint8_t { None, Left, Right, Main, Off };
enum class Context : std::uint8_t { Gameplay, Orientation };
struct Binding {
    Device device;
    int code;
    ShiftReq shift;
    IntentionKind kind;
    HandRef hand = HandRef::None;
    int arg = 0;
    bool secondary = false;
    Context context = Context::Gameplay;
};
std::span<const Binding> bindings();  // bindings.cpp

struct InputContextState {
    bool devUiWantsMouse = false;
    bool devUiWantsKeyboard = false;
};

struct IntentionFrame {
    std::vector<Intention> fired;  // edge intentions, in event order
    double forward = 0.0, right = 0.0, up = 0.0;  // move/ascend/descend, each in [−1, 1]
    bool fast = false;
    bool orientHeld = false;                  // X held: the orientation context is on the stack
    std::array<bool, kHandCount> useHeld{};  // primary use held, per physical hand
    std::vector<std::string> contexts;       // the context stack this frame, bottom → top
};

class IntentionLayer {
public:
    explicit IntentionLayer(Hand main = Hand::Right) : main_(main) {}

    void push(const RawEvent& e) { queue_.push_back(e); }
    // Drains the queued events through the context stack.
    IntentionFrame process(const InputContextState& ctx);

private:
    struct Held {
        Device device;
        int code;
        const Binding* binding;  // the binding matched at press time
    };
    Hand resolve(HandRef h) const;
    const Binding* match(const RawEvent& e, bool orientation) const;

    Hand main_;
    std::vector<RawEvent> queue_;
    std::vector<Held> held_;
};
