#pragma once

#include <cstdint>
#include <string>

namespace safevst3 {

// Protocol-neutral metadata owned by the hosted VST3 lifecycle seam. These
// types intentionally contain no Single transport or shared-memory fields.
struct EngineParameter {
    std::uint32_t id = 0;
    std::int32_t step_count = 0;
    std::uint32_t flags = 0;
    double default_normalized = 0.0;
    double current_normalized = 0.0;
    std::string title;
    std::string units;
};

struct EngineParameterUpdate {
    std::uint32_t id = 0;
    double normalized = 0.0;
};

} // namespace safevst3
