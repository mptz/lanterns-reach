#include "intentions.h"

#include <algorithm>

std::string describe(const Intention& i) {
    auto hand = [&] { return std::string(handName(i.hand)); };
    switch (i.kind) {
        case IntentionKind::Move: return "move";
        case IntentionKind::Ascend: return "ascend";
        case IntentionKind::Descend: return "descend";
        case IntentionKind::Fast: return "fast";
        case IntentionKind::OrientModifier: return "orient_modifier";
        case IntentionKind::TogglePreview: return "toggle_preview";
        case IntentionKind::CycleMode: return "cycle_placement_mode";
        case IntentionKind::RotateGhost:
            return std::string(i.secondary ? "tip_ghost(" : "rotate_ghost(") + (i.arg > 0 ? "up)" : "down)");
        case IntentionKind::Use: return "use(" + hand() + (i.secondary ? ", secondary)" : ", primary)");
        case IntentionKind::SelectSlot: return "select_slot(" + hand() + ", " + std::to_string(i.arg + 1) + ")";
        case IntentionKind::Scroll: return "scroll(" + hand() + (i.arg > 0 ? ", +1)" : ", -1)");
        case IntentionKind::Bare: return "bare(" + hand() + ")";
        case IntentionKind::SwapHotbars: return "swap_hotbars";
        case IntentionKind::PickBlock: return "pick_block";
        case IntentionKind::ToggleOverlay:
            return "toggle_overlay(F" + std::to_string(i.arg) + (i.secondary ? ", back)" : ")");
    }
    return "?";
}

Hand IntentionLayer::resolve(HandRef h) const {
    switch (h) {
        case HandRef::Left: return Hand::Left;
        case HandRef::Right: return Hand::Right;
        case HandRef::Main: return main_;
        case HandRef::Off: return main_ == Hand::Right ? Hand::Left : Hand::Right;
        case HandRef::None: break;
    }
    return main_;
}

const Binding* IntentionLayer::match(const RawEvent& e, bool orientation) const {
    // Topmost context first: orientation (if X is held), then gameplay.
    for (const Context ctx : {Context::Orientation, Context::Gameplay}) {
        if (ctx == Context::Orientation && !orientation) continue;
        for (const Binding& b : bindings()) {
            if (b.context != ctx || b.device != e.device || b.code != e.code) continue;
            if (b.shift == ShiftReq::With && !e.shift) continue;
            if (b.shift == ShiftReq::Without && e.shift) continue;
            return &b;
        }
    }
    return nullptr;
}

IntentionFrame IntentionLayer::process(const InputContextState& ctx) {
    IntentionFrame out;
    out.contexts.push_back("gameplay");
    auto orientHeld = [&] {
        return std::any_of(held_.begin(), held_.end(),
                           [](const Held& h) { return h.binding->kind == IntentionKind::OrientModifier; });
    };
    if (ctx.devUiWantsMouse || ctx.devUiWantsKeyboard)
        out.contexts.push_back(std::string("dev-ui (") +
                               (ctx.devUiWantsMouse && ctx.devUiWantsKeyboard ? "mouse+keyboard"
                                : ctx.devUiWantsMouse                        ? "mouse"
                                                                             : "keyboard") +
                               ")");
    auto swallowed = [&](Device d) {
        return d == Device::Key ? ctx.devUiWantsKeyboard : ctx.devUiWantsMouse;  // mouse = buttons + wheel
    };

    for (const RawEvent& e : queue_) {
        if (!e.press) {  // releases always clear held state, fire nothing
            std::erase_if(held_, [&](const Held& h) { return h.device == e.device && h.code == e.code; });
            continue;
        }
        if (swallowed(e.device)) continue;  // the dev-UI context consumes it
        const Binding* b = match(e, orientHeld() && !ctx.devUiWantsKeyboard);
        if (!b) continue;
        switch (b->kind) {
            case IntentionKind::Move:
            case IntentionKind::Ascend:
            case IntentionKind::Descend:
            case IntentionKind::Fast:
            case IntentionKind::OrientModifier:
                break;  // continuous only
            default:
                out.fired.push_back({b->kind, resolve(b->hand), b->arg, b->secondary});
        }
        if (e.device != Device::Wheel &&
            std::none_of(held_.begin(), held_.end(), [&](const Held& h) { return h.device == e.device && h.code == e.code; }))
            held_.push_back({e.device, e.code, b});
    }
    queue_.clear();

    for (const Held& h : held_) {
        if (swallowed(h.device)) continue;  // e.g. typing into a dev window: movement keys don't move
        const Binding& b = *h.binding;
        switch (b.kind) {
            case IntentionKind::Move:
                if (b.arg == 0) out.forward += 1.0;
                if (b.arg == 1) out.forward -= 1.0;
                if (b.arg == 2) out.right += 1.0;
                if (b.arg == 3) out.right -= 1.0;
                break;
            case IntentionKind::Ascend: out.up += 1.0; break;
            case IntentionKind::Descend: out.up -= 1.0; break;
            case IntentionKind::Fast: out.fast = true; break;
            case IntentionKind::OrientModifier: out.orientHeld = true; break;
            case IntentionKind::Use:
                if (!b.secondary) out.useHeld[static_cast<std::size_t>(resolve(b.hand))] = true;
                break;
            default: break;
        }
    }
    if (out.orientHeld) out.contexts.insert(out.contexts.begin() + 1, "orientation (X)");
    out.forward = std::clamp(out.forward, -1.0, 1.0);
    out.right = std::clamp(out.right, -1.0, 1.0);
    out.up = std::clamp(out.up, -1.0, 1.0);
    return out;
}
