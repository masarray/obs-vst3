#include "common/parameter_catalog_snapshot.hpp"

#include <algorithm>

namespace safevst3 {

bool read_parameter_catalog_snapshot(
    const ParameterCatalogSource& source,
    ParameterCatalogSnapshot& destination) noexcept
{
    destination = {};
    for (std::uint32_t attempt = 0; attempt < kParameterCatalogReadAttempts; ++attempt) {
        std::uint32_t generation_before = 0;
        if (!source.begin_read(generation_before))
            continue;

        ParameterCatalogSnapshot candidate{};
        candidate.count = std::min(source.exposed_count(), kMaxParameters);
        candidate.total_count = source.total_count();

        bool copied = true;
        for (std::uint32_t i = 0; i < candidate.count; ++i) {
            if (source.read_entry(i, candidate.entries[i]))
                continue;
            copied = false;
            break;
        }
        const std::uint32_t generation_after = source.generation();
        source.end_read();
        if (!copied)
            continue;
        if (generation_before != generation_after || (generation_after & 1u) != 0)
            continue;

        destination = candidate;
        return true;
    }
    return false;
}

} // namespace safevst3
