#include "chunk.h"

ChunkData::ChunkData() : brick_(std::make_unique<Brick<BlockState>>(BlockState::air())) {}
ChunkData::ChunkData(const ChunkData& o) : brick_(std::make_unique<Brick<BlockState>>(*o.brick_)) {}
ChunkData& ChunkData::operator=(const ChunkData& o) {
    if (this != &o) brick_ = std::make_unique<Brick<BlockState>>(*o.brick_);
    return *this;
}

bool ChunkData::set(const glm::ivec3& local, const BlockState& state) {
    if (brick_->at(local) == state) return false;
    using Index = Brick<BlockState>::Index;
    std::size_t found = brick_->paletteSize();
    for (std::size_t i = 0; i < brick_->paletteSize(); ++i)
        if (brick_->entry(static_cast<Index>(i)) == state) {
            found = i;
            break;
        }
    const Index index = found < brick_->paletteSize() ? static_cast<Index>(found) : brick_->append(state);
    brick_->setIndex(local, index);
    return true;
}
