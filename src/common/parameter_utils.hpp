#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace safevst3 {

inline double normalize_parameter_value(double value, std::int32_t step_count) noexcept
{
    value = std::clamp(value, 0.0, 1.0);
    if (step_count <= 0)
        return value;

    const double steps = static_cast<double>(step_count);
    return std::clamp(std::round(value * steps) / steps, 0.0, 1.0);
}

} // namespace safevst3
