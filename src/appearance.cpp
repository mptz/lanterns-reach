#include "appearance.h"

#include <map>

#include "block_mesh.h"

AppearanceTable::AppearanceTable(const BlockRegistry& registry) {
    std::map<FacePayload, std::uint32_t> interned;
    const std::size_t types = registry.defs().size();
    ids_.resize(types * orient::kRotCount * orient::kFaceCount);
    for (std::size_t t = 0; t < types; ++t)
        for (int r = 0; r < orient::kRotCount; ++r)
            for (int f = 0; f < orient::kFaceCount; ++f) {
                const BlockState state{static_cast<BlockTypeId>(t), static_cast<orient::Rot>(r)};
                const FaceTexture tex = faceTexture(registry, state, static_cast<orient::Face>(f));
                const FacePayload payload{tex.layer, BlockRegistry::materialId(state.type), packTexInfo(tex)};
                const auto [it, fresh] = interned.emplace(payload, static_cast<std::uint32_t>(payloads_.size()));
                if (fresh) payloads_.push_back(payload);
                ids_[(t * orient::kRotCount + static_cast<std::size_t>(r)) * orient::kFaceCount + static_cast<std::size_t>(f)] = it->second;
            }
}
