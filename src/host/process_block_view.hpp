#pragma once

#include <cstdint>

namespace safevst3 {

// Non-owning protocol-neutral audio view. Buffer ownership and lifetime remain
// with the caller for the duration of process(); no transport layout is
// embedded here.
struct ProcessBlockView {
    float* const* input = nullptr;
    float* const* output = nullptr;
    std::uint32_t channels = 0;
    std::uint32_t frames = 0;
    std::uint64_t sequence = 0;
};

} // namespace safevst3
