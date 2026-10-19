#pragma once
#include <cstddef>
#include <algorithm>

namespace ric::core {

// Slices arbitrary host processBlock sizes into deterministic micro-blocks (kSubBlockSize = 24)
// matching Mutable Instruments Plaits / Rings control-rate cadence (~2kHz at 48kHz).
struct SubBlockSlice {
    int startSample = 0;
    int numSamples = 0;
};

class SubBlockAdapter {
public:
    static constexpr int kDefaultSubBlockSize = 24;

    template <typename Callback>
    static void processInSubBlocks(int totalSamples, Callback&& fn, int maxSubBlock = kDefaultSubBlockSize) {
        int offset = 0;
        while (offset < totalSamples) {
            const int chunk = std::min(maxSubBlock, totalSamples - offset);
            fn(offset, chunk);
            offset += chunk;
        }
    }
};

} // namespace ric::core
