// Tests: blocks. Registered through sandbox_tests.cpp.

#include "test_support.h"

namespace {

using namespace orient;

// --- block definitions --------------------------------------------------------------

Image solid(std::uint32_t n) {
    Image img{n, n, std::vector<std::uint8_t>(static_cast<std::size_t>(n) * n * 4, 128)};
    return img;
}

void setPixel(Image& img, std::uint32_t x, std::uint32_t y, std::uint8_t v) {
    for (int c = 0; c < 3; ++c) img.rgba[(y * img.width + x) * 4 + static_cast<std::uint32_t>(c)] = v;
}

std::uint8_t pixel(const Image& img, std::uint32_t x, std::uint32_t y) { return img.rgba[(y * img.width + x) * 4]; }

void testRotateCcw() {
    Image img = solid(4);
    setPixel(img, 0, 0, 255);  // top-left marker
    CHECK(pixel(rotateCcw(img, 1), 0, 3) == 255);  // CCW: top-left → bottom-left
    CHECK(pixel(rotateCcw(img, 2), 3, 3) == 255);
    CHECK(pixel(rotateCcw(img, 3), 3, 0) == 255);
    CHECK(rotateCcw(img, 4) == img);
    CHECK(rotateCcw(rotateCcw(img, 1), 3) == img);
}

void testParseDefinition() {
    std::string err;
    const auto log = parseDefinition(R"({"name":"log","symmetry":"column","textures":{"+z":"e.png","-y":"s.png"}})", err);
    CHECK(log && log->symmetry == Symmetry::Column && log->textures.size() == 2);
    CHECK(log && log->material.roughness == 0.8f && log->material.metallic == 0.0f &&
          log->material.reflectance == 0.5f && log->material.emissive == glm::vec3(0.0f));  // defaults
    // Wrong key set: +x is in the side orbit, whose representative is −y.
    CHECK(!parseDefinition(R"({"name":"log","symmetry":"column","textures":{"+z":"e.png","+x":"s.png"}})", err));
    CHECK(err.find("orbit representatives") != std::string::npos);
    CHECK(!parseDefinition(R"({"name":"log","symmetry":"column","textures":{"+z":"e.png"}})", err));
    CHECK(!parseDefinition(R"({"name":"x","symmetry":"spherical","textures":{"+z":"a.png"}})", err));
    CHECK(!parseDefinition(R"({"name":"x","symmetry":"uniform","textures":{"+z":"a.png"},"extra":1})", err));
    CHECK(!parseDefinition("not json", err));
    // No assertions any more (v0.1.8: the loader checks structure only).
    CHECK(!parseDefinition(R"({"name":"x","symmetry":"uniform","textures":{"+z":"a.png"},"asserts":{"+z":["tileable"]}})", err));
    CHECK(err.find("unknown field") != std::string::npos);
    // Variant sets: three rules.
    const auto rnd = parseDefinition(R"({"name":"d","symmetry":"uniform","textures":{"+z":{"random":"d.png","tiles":8}}})", err);
    CHECK(rnd && rnd->textures[0].set == VariantSet::Random && rnd->textures[0].tiles == 8 && rnd->textures[0].file == "d.png");
    const auto ns = parseDefinition(R"({"name":"b","symmetry":"uniform","textures":{"+z":{"matched":"b.png","edges":"ns"}}})", err);
    CHECK(ns && ns->textures[0].set == VariantSet::MatchedNS && ns->textures[0].tiles == 4);
    const auto nsew = parseDefinition(R"({"name":"g","symmetry":"uniform","textures":{"+z":{"matched":"g.png","edges":"nsew"}}})", err);
    CHECK(nsew && nsew->textures[0].set == VariantSet::MatchedNSEW && nsew->textures[0].tiles == 16);
    const auto conn = parseDefinition(R"({"name":"g","symmetry":"uniform","textures":{"+z":{"connected":"g.png"}}})", err);
    CHECK(conn && conn->textures[0].set == VariantSet::Connected && conn->textures[0].tiles == 16);
    for (const char* bad : {R"({"random":"d.png"})", R"({"random":"d.png","tiles":0})", R"({"random":"d.png","tiles":33})",
                            R"({"matched":"b.png"})", R"({"matched":"b.png","edges":"ew"})", R"({"wang":"s.png"})",
                            R"({"connected":"g.png","tiles":4})"}) {
        const std::string text = std::string(R"({"name":"x","symmetry":"uniform","textures":{"+z":)") + bad + "}}";
        CHECK(!parseDefinition(text, err));
        CHECK(err.find("bad texture entry") != std::string::npos);
    }
    // Material fields.
    const auto lamp = parseDefinition(R"({"name":"lamp","symmetry":"uniform","textures":{"+z":"a.png"},
                                          "roughness":0.25,"metallic":1,"reflectance":0.75,"emissive":[2,1,0.5]})", err);
    CHECK(lamp && lamp->material.roughness == 0.25f && lamp->material.metallic == 1.0f &&
          lamp->material.reflectance == 0.75f && lamp->material.emissive == glm::vec3(2.0f, 1.0f, 0.5f));
    CHECK(!parseDefinition(R"({"name":"x","symmetry":"uniform","textures":{"+z":"a.png"},"roughness":1.5})", err));
    CHECK(!parseDefinition(R"({"name":"x","symmetry":"uniform","textures":{"+z":"a.png"},"emissive":[1,-1,0]})", err));

    // Image checks, structural only: 16×16 singles; a set's atlas is 64 wide
    // and exactly tall enough for its tiles. Asymmetric art is legal.
    auto rect = [](std::uint32_t w, std::uint32_t h) {
        return Image{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4, 128)};
    };
    Image end = solid(16), side = solid(16);
    setPixel(side, 0, 0, 255);
    const Image* sidePtr = &side;
    const Image* endPtr = &end;
    auto images = [&](const std::string& f) -> const Image* { return f == "e.png" ? endPtr : sidePtr; };
    CHECK(checkTextures(*log, images).empty());
    const Image big = solid(32);
    sidePtr = &big;
    CHECK(checkTextures(*log, images).find("block textures are 16×16") != std::string::npos);
    const Image a64x32 = rect(64, 32), a64x16 = rect(64, 16), a64x64 = rect(64, 64);
    sidePtr = &a64x32;
    CHECK(checkTextures(*rnd, images).empty());  // 8 tiles: two rows
    sidePtr = &a64x16;
    CHECK(checkTextures(*rnd, images).find("need 64×32") != std::string::npos);
    CHECK(checkTextures(*ns, images).empty());  // 4 tiles: one row
    sidePtr = &a64x64;
    CHECK(checkTextures(*ns, images).find("4 tiles of 16×16") != std::string::npos);
    CHECK(checkTextures(*nsew, images).empty() && checkTextures(*conn, images).empty());
}

// Variant sets: the atlas split (4 tiles per row, any count).
void testVariantSets() {
    Image atlas{64, 32, std::vector<std::uint8_t>(64 * 32 * 4, 128)};
    for (std::uint32_t i = 0; i < 8; ++i)  // mark each tile's top-left texel with its index
        setPixel(atlas, (i % 4) * 16, (i / 4) * 16, static_cast<std::uint8_t>(i));
    const std::vector<Image> tiles = splitSet(atlas, 8);
    CHECK(tiles.size() == 8 && tiles[0].width == 16 && tiles[0].height == 16);
    for (std::uint32_t i = 0; i < 8; ++i) CHECK(pixel(tiles[i], 0, 0) == i && pixel(tiles[i], 1, 0) == 128);
    CHECK(splitSet(atlas, 6).size() == 6);
    CHECK(setTileCount(VariantSet::Random, 5) == 5 && setTileCount(VariantSet::MatchedNS, 0) == 4 &&
          setTileCount(VariantSet::MatchedNSEW, 0) == 16 && setTileCount(VariantSet::Connected, 0) == 16 &&
          setTileCount(VariantSet::None, 0) == 1);
}

void testFaceTexturesForColumn() {
    const SymmetryInfo& col = symmetry(Symmetry::Column);
    for (Face side : {NegY, PosY, PosX, NegX}) {
        CHECK(col.repOf[side] == NegY);
        // Carried by turns about z or a half turn about x: bark stays along
        // the log's axis on every side.
        CHECK(col.repTurns[side] % 2 == 0);
    }
    CHECK(col.repOf[NegZ] == PosZ);
}


// A block mesh face shows the texture an independent derivation predicts:
// rendering a log rotated so its axis is along x puts its END texture on ±x.
void testMeshFaceTextures() {
    const auto log = registry().find("log");
    CHECK(log.has_value());
    if (!log) return;
    ChunkWorld g(registry());
    const orient::Rot alongX = registry().state(*log, rotationAbout({0, 1, 0}, 90)).orientation;
    g.apply({{{{1, 1, 1}, BlockState{*log, alongX}}}});
    const WorldMesh m = g.buildMesh(ChunkWorld::kChunk);
    const BlockDef& def = registry().def(*log);
    const std::uint32_t endLayer = def.faceLayer[PosZ], sideLayer = def.faceLayer[NegY];
    CHECK(endLayer != sideLayer);
    for (std::size_t q = 0; q + 3 < m.vertices.size(); q += 4) {
        const MeshVertex& v = m.vertices[q];
        const bool xFace = std::abs(v.normal.x) > 0.5f;
        CHECK(v.layer == (xFace ? endLayer : sideLayer));
    }
}


// The mip chain (render/block-textures): complete (16 → 1) for every
// shipped layer, each level a linear-light box filter, the 1×1 level the
// face's mean colour; alpha averages linearly.
void testMipChain() {
    for (const Image& layer : registry().layers()) {
        CHECK(layer.width == kTextureSize && layer.height == kTextureSize);
        const std::vector<Image> chain = buildMipChain(layer);
        CHECK(chain.size() == 5);
        for (std::size_t m = 0; m < chain.size(); ++m)
            CHECK(chain[m].width == (kTextureSize >> m) && chain[m].height == (kTextureSize >> m) &&
                  chain[m].rgba.size() == static_cast<std::size_t>(chain[m].width) * chain[m].height * 4);
    }
    // Half black, half white: the mean in linear light is 0.5, which is
    // sRGB 188 (not 128, the naive average of the encoded values).
    Image half = solid(16);
    for (std::uint32_t y = 0; y < 16; ++y)
        for (std::uint32_t x = 0; x < 16; ++x) {
            setPixel(half, x, y, x < 8 ? 0 : 255);
            half.rgba[(y * 16 + x) * 4 + 3] = x < 8 ? 0 : 255;
        }
    const Image last = buildMipChain(half).back();
    CHECK(last.width == 1 && (last.rgba[0] == 187 || last.rgba[0] == 188) && (last.rgba[3] == 127 || last.rgba[3] == 128));
}

}  // namespace

void runBlocksTests() {
    testRotateCcw();
    testParseDefinition();
    testVariantSets();
    testFaceTexturesForColumn();
    testMeshFaceTextures();
    testMipChain();
}
