#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

namespace safevst3 {

// Generic bounded seqlock reader used by OBS-side control data. The writer
// publishes odd generations while mutating a snapshot and even generations
// when stable. Readers never spin indefinitely: if a coherent generation is
// not observed within max_attempts they fail closed and return no snapshot.
template <typename Snapshot, typename ReadGeneration, typename CopySnapshot>
bool read_coherent_generation(Snapshot& destination,
                              ReadGeneration&& read_generation,
                              CopySnapshot&& copy_snapshot,
                              std::size_t max_attempts = 3)
{
    for (std::size_t attempt = 0; attempt < max_attempts; ++attempt) {
        const std::uint32_t before = static_cast<std::uint32_t>(read_generation());
        if ((before & 1u) != 0)
            continue;

        Snapshot candidate{};
        if (!copy_snapshot(candidate))
            continue;

        const std::uint32_t after = static_cast<std::uint32_t>(read_generation());
        if (before == after && (after & 1u) == 0) {
            destination = std::move(candidate);
            return true;
        }
    }

    destination = Snapshot{};
    return false;
}

} // namespace safevst3
