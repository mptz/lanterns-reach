#pragma once
// Hands and hotbars (input/per-hand-use, input/hotbars), harness subset.
// One hotbar per hand, at most 9 slots and no empty slots; a hotbar's
// selection is a slot or `bare`. Handedness picks the main hand (right in
// v0.1.5). Pure logic; main.cpp applies intentions to it.

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "blocks.h"

enum class Hand : std::uint8_t { Left = 0, Right = 1 };
inline constexpr int kHandCount = 2;
inline const char* handName(Hand h) { return h == Hand::Left ? "left" : "right"; }

struct Item {
    // Tools (tools/catalog): pick breaks, wrench rotates, shovel levels
    // soft ground; a container in hand places from its contents
    // (inventory/hand-pointers). What each does is tools.h's planEdit.
    enum class Kind : std::uint8_t { Block, Pick, Wrench, Shovel, Container } kind = Kind::Block;
    BlockTypeId block = kAirType;  // for Kind::Block (creative: an infinite stack)

    static Item blockStack(BlockTypeId t) { return {Kind::Block, t}; }
    static Item pick() { return {Kind::Pick, kAirType}; }
    static Item wrench() { return {Kind::Wrench, kAirType}; }
    static Item shovel() { return {Kind::Shovel, kAirType}; }
    static Item container() { return {Kind::Container, kAirType}; }  // the one hardcoded container
    bool operator==(const Item&) const = default;
};

class Hotbar {
public:
    static constexpr int kMaxSlots = 9;

    Hotbar() = default;
    explicit Hotbar(std::vector<Item> slots);  // at most 9; selects slot 0 (bare if empty)

    const std::vector<Item>& slots() const { return slots_; }
    bool isBare() const { return selected_ < 0; }
    int selected() const { return selected_; }      // −1 = bare
    int lastSelected() const { return last_; }      // where the wheel resumes from bare
    const Item* held() const { return isBare() ? nullptr : &slots_[static_cast<std::size_t>(selected_)]; }

    // Number key i (0-based): selects slot i if it exists; leaves bare.
    void select(int i);
    // Wheel: ±1 with wrap-around; bare is not a wheel stop. From bare, the
    // first tick returns to the last selected slot.
    void scroll(int delta);
    void setBare() { selected_ = -1; }
    // Pick-block (creative): select a slot already holding `t`, otherwise
    // replace the selected slot (or, when bare, the last selected one).
    void pick(BlockTypeId t);

private:
    std::vector<Item> slots_;
    int selected_ = -1;
    int last_ = 0;
};

class Hands {
public:
    explicit Hands(Hand main = Hand::Right) : main_(main) {}

    Hand main() const { return main_; }
    Hand off() const { return main_ == Hand::Right ? Hand::Left : Hand::Right; }
    Hotbar& bar(Hand h) { return bars_[static_cast<std::size_t>(h)]; }
    const Hotbar& bar(Hand h) const { return bars_[static_cast<std::size_t>(h)]; }
    // R: the two hotbars change hands, each keeping its selection.
    void swapHotbars() { std::swap(bars_[0], bars_[1]); }

private:
    Hand main_;
    std::array<Hotbar, kHandCount> bars_;
};

// A use fires on press, then repeats every `period` while held after an
// initial `delay` (v0.1.5: 250 ms, then every 200 ms).
class UseRepeater {
public:
    // Seconds (versions.md v0.1.5): close to common key-repeat feel; chosen
    // by eye, tune in play.
    static constexpr double kDelay = 0.250, kPeriod = 0.200;

    // Call once per frame with whether the use is held, a fresh press this
    // frame, and the time; returns how many uses fire this frame.
    int update(bool held, bool pressed, double now);

private:
    bool active_ = false;
    double next_ = 0.0;
};
