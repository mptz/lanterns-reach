#pragma once
// Frame and GPU timing for the dev UI (F1) and --gpu-report, and the world
// mesh's rebuild stats.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "renderer.h"

// Frame rate / frame time averaged over half-second windows, so the F1
// numbers are readable rather than flickering.
struct FrameClock {
    static constexpr double kWindowSeconds = 0.5;  // readable, still responsive; chosen by eye
    double windowStart = 0.0;
    long windowFrames = 0;
    double fps = 0.0, frameMs = 0.0;

    void tick(double now) {
        ++windowFrames;
        const double span = now - windowStart;
        if (span < kWindowSeconds) return;
        fps = static_cast<double>(windowFrames) / span;
        frameMs = 1000.0 * span / static_cast<double>(windowFrames);
        windowStart = now;
        windowFrames = 0;
    }
};

// Per-pass GPU times, averaged (F1 and --gpu-report): F1 shows each pass's
// mean over the last half-second window, so the numbers can be read; the
// report is the mean over the whole run after a warm-up.
struct GpuTimeStats {
    static constexpr double kWindowSeconds = FrameClock::kWindowSeconds;
    static constexpr long kWarmupFrames = 120;     // skip startup (uploads, first pipelines); arbitrary

    std::vector<std::string> names;  // in pass order, as the renderer reports them
    std::vector<double> windowSum, runSum, shown;
    long windowFrames = 0, runFrames = 0, seen = 0;
    double windowStart = 0.0;

    void add(const std::vector<PassTime>& times, double now) {
        if (times.empty()) return;
        if (names.size() != times.size()) {
            names.clear();
            for (const PassTime& t : times) names.push_back(t.name);
            windowSum.assign(names.size(), 0.0);
            runSum.assign(names.size(), 0.0);
            shown.assign(names.size(), 0.0);
            windowFrames = runFrames = 0;
        }
        for (std::size_t i = 0; i < times.size(); ++i) {
            windowSum[i] += times[i].ms;
            if (seen >= kWarmupFrames) runSum[i] += times[i].ms;
        }
        ++windowFrames;
        if (++seen > kWarmupFrames) ++runFrames;
        if (now - windowStart >= kWindowSeconds) {
            for (std::size_t i = 0; i < names.size(); ++i) shown[i] = windowSum[i] / static_cast<double>(windowFrames);
            windowSum.assign(names.size(), 0.0);
            windowFrames = 0;
            windowStart = now;
        }
    }

    void report() const {
        if (runFrames == 0) {
            std::fprintf(stderr, "[sandbox] gpu report: no timed frames after the %ld-frame warm-up\n", kWarmupFrames);
            return;
        }
        double total = 0.0;
        std::fprintf(stderr, "[sandbox] gpu report: mean over %ld frames (after %ld warm-up)\n", runFrames, kWarmupFrames);
        for (std::size_t i = 0; i < names.size(); ++i) {
            const double ms = runSum[i] / static_cast<double>(runFrames);
            total += ms;
            std::fprintf(stderr, "[sandbox]   %-12s %7.3f ms\n", names[i].c_str(), ms);
        }
        std::fprintf(stderr, "[sandbox]   %-12s %7.3f ms\n", "total", total);
    }
};

struct MeshStats {
    double rebuildMs = 0.0;  // buildMesh + upload, last rebuild
    std::size_t faces = 0;
    std::uint64_t rebuilds = 0;
};
