#include "hands.h"

#include <stdexcept>

Hotbar::Hotbar(std::vector<Item> slots) : slots_(std::move(slots)) {
    if (slots_.size() > static_cast<std::size_t>(kMaxSlots)) throw std::invalid_argument("hotbar holds at most 9 slots");
    selected_ = slots_.empty() ? -1 : 0;
}

void Hotbar::select(int i) {
    if (i < 0 || i >= static_cast<int>(slots_.size())) return;
    selected_ = last_ = i;
}

void Hotbar::scroll(int delta) {
    if (slots_.empty() || delta == 0) return;
    if (isBare()) {
        selected_ = last_;
        return;
    }
    const int n = static_cast<int>(slots_.size());
    selected_ = last_ = ((selected_ + delta) % n + n) % n;
}

void Hotbar::pick(BlockTypeId t) {
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].kind == Item::Kind::Block && slots_[i].block == t) {
            select(static_cast<int>(i));
            return;
        }
    if (slots_.empty()) {
        slots_.push_back(Item::blockStack(t));
        selected_ = last_ = 0;
        return;
    }
    const int slot = isBare() ? last_ : selected_;
    slots_[static_cast<std::size_t>(slot)] = Item::blockStack(t);
    selected_ = last_ = slot;
}

int UseRepeater::update(bool held, bool pressed, double now) {
    if (pressed) {
        active_ = true;
        next_ = now + kDelay;
        return 1;
    }
    if (!held) {
        active_ = false;
        return 0;
    }
    if (!active_) return 0;  // held without a press we saw (e.g. pressed while a dev window had the mouse)
    int fires = 0;
    while (now >= next_) {
        ++fires;
        next_ += kPeriod;
    }
    return fires;
}
