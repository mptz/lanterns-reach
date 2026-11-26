#include "png_write.h"

#include <array>
#include <cstdio>

namespace {

std::uint32_t crc32(const std::uint8_t* data, std::size_t n, std::uint32_t crc = 0xFFFFFFFFu) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

void be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::uint8_t>(v >> s));
}

void chunk(std::vector<std::uint8_t>& out, const char* tag, const std::vector<std::uint8_t>& data) {
    be32(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), tag, tag + 4);
    out.insert(out.end(), data.begin(), data.end());
    be32(out, crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu);
}

}  // namespace

bool writePngRgb(const std::string& path, std::uint32_t w, std::uint32_t h, const std::vector<std::uint8_t>& rgb) {
    // Raw scanlines, each prefixed with filter type 0.
    std::vector<std::uint8_t> raw;
    raw.reserve((static_cast<std::size_t>(w) * 3 + 1) * h);
    for (std::uint32_t y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * w * 3),
                   rgb.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y + 1) * w * 3));
    }
    // zlib stream of stored deflate blocks (≤ 65535 bytes each) + Adler-32.
    std::vector<std::uint8_t> z = {0x78, 0x01};
    for (std::size_t pos = 0; pos < raw.size() || raw.empty();) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
        const bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n));
        z.push_back(static_cast<std::uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
        if (last) break;
    }
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t c : raw) {
        a = (a + c) % 65521u;
        b = (b + a) % 65521u;
    }
    be32(z, (b << 16) | a);

    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    be32(ihdr, w);
    be32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit, RGB, deflate, no filter method extras, no interlace
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    return std::fclose(f) == 0 && ok;
}
