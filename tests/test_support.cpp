#include "test_support.h"

const BlockRegistry& registry() {
    static const BlockRegistry reg = BlockRegistry::load(SANDBOX_ASSET_DIR "/blocks");
    return reg;
}

BlockState solidState() { return {*registry().find("stone"), orient::kIdentity}; }

BlockTypeId typeOf(const char* name) { return *registry().find(name); }

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

ChunkWorld worldWith(std::initializer_list<CellPos> solid) {
    ChunkWorld w(registry());
    EditSet e;
    for (const CellPos& c : solid) e.edits.push_back({c, solidState()});
    w.apply(e);
    return w;
}

std::optional<ToolTarget> aim(const WorldQuery& w, WorldPos eye, WorldPos at) {
    const auto hit = w.raycast(eye, at - eye, 64.0);
    if (!hit) return std::nullopt;
    return ToolTarget{*hit, eye};
}
