#include "common/parameter_utils.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {
bool near(double a, double b)
{
    return std::abs(a - b) < 1e-12;
}
} // namespace

int main()
{
    using safevst3::normalize_parameter_value;

    assert(near(normalize_parameter_value(-0.25, 0), 0.0));
    assert(near(normalize_parameter_value(1.25, 0), 1.0));
    assert(near(normalize_parameter_value(0.375, 0), 0.375));

    assert(near(normalize_parameter_value(0.49, 1), 0.0));
    assert(near(normalize_parameter_value(0.51, 1), 1.0));

    assert(near(normalize_parameter_value(0.62, 4), 0.5));
    assert(near(normalize_parameter_value(0.63, 4), 0.75));

    std::cout << "parameter normalization semantics ok\n";
    return 0;
}
