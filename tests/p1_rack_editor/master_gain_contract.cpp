#include "rack/rack_master_controls.hpp"

#include <cmath>
#include <iostream>

namespace {
bool near(float a, float b, float eps = 1.0e-4f)
{
    return std::fabs(a - b) <= eps;
}
}

int main()
{
    using namespace safevst3::rack::ui;

    RackMasterControlBus bus;
    bus.set_input_db(3.0f);
    bus.set_output_db(-6.0f);
    const RackMasterControlSnapshot snapshot = bus.snapshot();
    if (!near(snapshot.input_db, 3.0f) || !near(snapshot.output_db, -6.0f)) {
        std::cerr << "FAIL: lock-free master control round-trip\n";
        return 1;
    }

    float left[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float right[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    float out_left[4]{};
    float out_right[4]{};
    float* source[2] = {left, right};
    float* destination[2] = {out_left, out_right};

    float current = 1.0f;
    rack_apply_smoothed_gain(source, destination, 2, 4, -6.0f, current);
    const float target = rack_master_db_to_linear(-6.0f);
    if (!near(current, target) || !near(out_left[3], target) ||
        !near(out_right[3], target * 0.5f)) {
        std::cerr << "FAIL: smoothed gain must land exactly on target at block end\n";
        return 2;
    }
    if (!(out_left[0] > out_left[1] && out_left[1] > out_left[2] && out_left[2] > out_left[3])) {
        std::cerr << "FAIL: gain ramp must be monotonic and bounded\n";
        return 3;
    }

    bus.set_input_db(99.0f);
    bus.set_output_db(-99.0f);
    const auto clamped = bus.snapshot();
    if (!near(clamped.input_db, kRackMasterMaxDb) ||
        !near(clamped.output_db, kRackMasterMinDb) ||
        rack_master_db_to_linear(kRackMasterMinDb) != 0.0f) {
        std::cerr << "FAIL: master range/mute floor contract\n";
        return 4;
    }

    std::cout << "P6 master gain controls: PASS\n";
    return 0;
}
