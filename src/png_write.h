#pragma once
// Minimal PNG writer for --capture (8-bit RGB, stored/uncompressed deflate:
// no zlib dependency). Output decodes with any PNG reader, including
// Python's zlib in tools/imgdiff.py.

#include <cstdint>
#include <string>
#include <vector>

// `rgb` is width·height·3 bytes, rows top to bottom. Returns false on I/O error.
bool writePngRgb(const std::string& path, std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& rgb);
