#pragma once
// Before/after kick waveform capture shared between DSP and UI (same process, direct access)
#include <atomic>
#include <cstring>
#include <cstdint>

struct KickScope {
    static constexpr int kBins = 600;         // display columns
    static constexpr float kMaxSeconds = 0.8f; // longest kick captured
    float inMin[kBins], inMax[kBins], outMin[kBins], outMax[kBins];
    int used = 0;                              // filled bins
    std::atomic<uint32_t> version{0};
    KickScope() { std::memset(inMin, 0, sizeof(inMin)); std::memset(inMax, 0, sizeof(inMax));
                  std::memset(outMin, 0, sizeof(outMin)); std::memset(outMax, 0, sizeof(outMax)); }
};

struct ScopeProvider {
    virtual ~ScopeProvider() {}
    virtual const KickScope& getScope() const = 0;
};
