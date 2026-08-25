#pragma once

#include "common/protocol.hpp"

#include <array>
#include <cstdint>

namespace safevst3 {

struct ParameterCatalogEntry {
    std::uint32_t id = 0;
    std::int32_t step_count = 0;
    std::uint32_t flags = 0;
    double default_normalized = 0.0;
    double current_normalized = 0.0;
    std::array<char, kParameterTitleBytes> title{};
    std::array<char, kParameterUnitsBytes> units{};
};

struct ParameterCatalogSnapshot {
    std::uint32_t count = 0;
    std::uint32_t total_count = 0;
    std::array<ParameterCatalogEntry, kMaxParameters> entries{};
};

class ParameterCatalogSource {
public:
    virtual ~ParameterCatalogSource() = default;

    virtual bool begin_read(std::uint32_t& generation) const noexcept = 0;
    virtual void end_read() const noexcept = 0;
    virtual std::uint32_t generation() const noexcept = 0;
    virtual std::uint32_t exposed_count() const noexcept = 0;
    virtual std::uint32_t total_count() const noexcept = 0;
    virtual bool read_entry(std::uint32_t index,
                            ParameterCatalogEntry& destination) const noexcept = 0;
};

inline constexpr std::uint32_t kParameterCatalogReadAttempts = 3;

[[nodiscard]] bool read_parameter_catalog_snapshot(
    const ParameterCatalogSource& source,
    ParameterCatalogSnapshot& destination) noexcept;

} // namespace safevst3
