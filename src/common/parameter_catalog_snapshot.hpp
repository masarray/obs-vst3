#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace safevst3 {

struct ParameterCatalogProjection {
    std::uint32_t total_count = 0;
    std::size_t published_count = 0;
};

// Project one bounded parameter catalog directly into a caller-owned sink.
// Production uses a fixed shared-memory sink, so this helper performs no heap
// allocation while DSP is paused. Tests may choose a vector-backed sink.
template <typename Range, typename Sink>
ParameterCatalogProjection project_parameter_catalog(const Range& parameters,
                                                     std::size_t capacity,
                                                     Sink&& sink)
{
    ParameterCatalogProjection projection{};
    projection.total_count = static_cast<std::uint32_t>(std::min<std::size_t>(
        parameters.size(), std::numeric_limits<std::uint32_t>::max()));
    projection.published_count = std::min<std::size_t>(parameters.size(), capacity);
    for (std::size_t i = 0; i < projection.published_count; ++i)
        sink(i, parameters[i]);
    return projection;
}

} // namespace safevst3
