#include "game.h"

namespace {

// The harness loadout and tool defaults (v0.1.5–v0.1.8 build choices,
// arbitrary): the shovel fills with dirt; the container holds a palette —
// mostly brick, some stone, a little dirt (counts = weights, 60/30/10).
constexpr const char* kShovelFillDefault = "dirt";
constexpr std::array<std::pair<const char*, std::uint32_t>, 3> kContainerDefault{
    {{"brick", 60}, {"stone", 30}, {"dirt", 10}}};
constexpr const char* kMainHotbar[] = {"stone", "dirt", "gravel", "stone_brick", "log", "furnace", "lamp", "glass_blue"};

}  // namespace

Game makeGame(WorldQuery& world, const BlockRegistry& registry, const Scenario& scenario) {
    Game game{world, registry, Hands(Hand::Right), {}, {}, {}, false};
    std::vector<Item> main;
    for (const char* name : kMainHotbar)
        if (const auto t = registry.find(name)) main.push_back(Item::blockStack(*t));
    main.push_back(Item::container());
    game.hands.bar(Hand::Right) = Hotbar(main);
    game.hands.bar(Hand::Left) = Hotbar({Item::pick(), Item::wrench(), Item::shovel()});
    auto type = [&](const char* name) { return registry.find(name).value_or(kAirType); };
    game.settings.shovelFill = type(kShovelFillDefault);
    for (std::size_t i = 0; i < kContainerDefault.size(); ++i)
        game.settings.container.slots[i] = {type(kContainerDefault[i].first), kContainerDefault[i].second};
    game.settings.container.patch = scenario.patch;
    game.settings.container.consume = scenario.consume;
    game.preview = scenario.preview;
    return game;
}
