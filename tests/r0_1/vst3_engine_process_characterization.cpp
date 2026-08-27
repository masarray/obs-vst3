#include "host/vst3_engine.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#ifdef _WIN32

namespace {

using safevst3::AudioSlot;
using safevst3::Vst3Engine;

bool close_enough(float actual, float expected) {
    return std::fabs(actual - expected) <= 1.0e-6f;
}

bool expect(bool condition, const char* message) {
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool expect_sample(float actual, float expected, const char* message) {
    if (close_enough(actual, expected))
        return true;
    std::cerr << "FAIL: " << message << " expected=" << expected
              << " actual=" << actual << '\n';
    return false;
}

bool open_engine(Vst3Engine& engine,
                 const char* module_path,
                 std::uint32_t channels) {
    std::string error;
    if (engine.open(module_path, "", 48000, channels, nullptr, error))
        return true;
    std::cerr << "FAIL: open(" << module_path << ", channels=" << channels
              << "): " << error << '\n';
    return false;
}

bool characterize_invalid_blocks(const char* mono_path) {
    bool ok = true;

    AudioSlot unopened_slot{};
    unopened_slot.frames = 1;
    unopened_slot.channels = 1;
    Vst3Engine unopened;
    ok &= expect(!unopened.process(unopened_slot),
                 "unopened engine must reject processing");

    Vst3Engine engine;
    if (!open_engine(engine, mono_path, 1))
        return false;

    AudioSlot slot{};
    slot.channels = 1;
    slot.frames = 0;
    ok &= expect(!engine.process(slot), "zero-frame block must be rejected");

    slot.frames = safevst3::kMaxFrames + 1;
    ok &= expect(!engine.process(slot), "oversized block must be rejected");

    slot.frames = 1;
    slot.channels = 2;
    ok &= expect(!engine.process(slot), "channel-count mismatch must be rejected");
    return ok;
}

bool characterize_direct_mono_and_position(const char* mono_path) {
    Vst3Engine engine;
    if (!open_engine(engine, mono_path, 1))
        return false;

    bool ok = true;
    AudioSlot first{};
    first.channels = 1;
    first.frames = 3;
    first.input[0][0] = 1.0f;
    first.input[0][1] = 2.0f;
    first.input[0][2] = 3.0f;
    ok &= expect(engine.process(first), "first mono block must process");
    ok &= expect_sample(first.output[0][0], 2.0f, "mono frame 0");
    ok &= expect_sample(first.output[0][1], 4.0f, "mono frame 1");
    ok &= expect_sample(first.output[0][2], 6.0f, "mono frame 2");

    AudioSlot second{};
    second.channels = 1;
    second.frames = 2;
    second.input[0][0] = 0.5f;
    second.input[0][1] = 1.5f;
    ok &= expect(engine.process(second), "second mono block must process");
    // projectTimeSamples starts at the previous successful block length (3).
    ok &= expect_sample(second.output[0][0], 4.0f,
                        "second block must observe projectTimeSamples=3");
    ok &= expect_sample(second.output[0][1], 6.0f,
                        "second block frame 1 must preserve block position");

    AudioSlot failed{};
    failed.channels = 1;
    failed.frames = 2;
    failed.input[0][0] = -1000.0f;
    failed.input[0][1] = 0.0f;
    ok &= expect(!engine.process(failed),
                 "fixture process error must propagate as false");

    AudioSlot after_failure{};
    after_failure.channels = 1;
    after_failure.frames = 1;
    after_failure.input[0][0] = 1.0f;
    ok &= expect(engine.process(after_failure),
                 "processing must remain callable after process error");
    // Only the 3-frame and 2-frame successful blocks advanced position.
    ok &= expect_sample(after_failure.output[0][0], 7.0f,
                        "failed block must not advance projectTimeSamples");
    return ok;
}

bool characterize_stereo_host_to_mono_plugin(const char* mono_path) {
    Vst3Engine engine;
    if (!open_engine(engine, mono_path, 2))
        return false;

    bool ok = true;
    AudioSlot slot{};
    slot.channels = 2;
    slot.frames = 2;
    slot.input[0][0] = 1.0f;
    slot.input[0][1] = 3.0f;
    slot.input[1][0] = 3.0f;
    slot.input[1][1] = 5.0f;

    ok &= expect(engine.process(slot),
                 "stereo host -> fixed-mono fixture must process");
    // Engine averages host stereo to [2,4], fixture applies x2, then engine
    // duplicates the mono result back to both host output channels.
    ok &= expect_sample(slot.output[0][0], 4.0f, "stereo->mono L frame 0");
    ok &= expect_sample(slot.output[1][0], 4.0f, "stereo->mono R frame 0");
    ok &= expect_sample(slot.output[0][1], 8.0f, "stereo->mono L frame 1");
    ok &= expect_sample(slot.output[1][1], 8.0f, "stereo->mono R frame 1");
    return ok;
}

bool characterize_mono_host_to_stereo_plugin(const char* stereo_path) {
    Vst3Engine engine;
    if (!open_engine(engine, stereo_path, 1))
        return false;

    bool ok = true;
    AudioSlot slot{};
    slot.channels = 1;
    slot.frames = 2;
    slot.input[0][0] = 2.0f;
    slot.input[0][1] = 4.0f;

    ok &= expect(engine.process(slot),
                 "mono host -> fixed-stereo fixture must process");
    // Engine duplicates mono input. Fixture produces x2 on L and x4 on R;
    // engine averages those stereo outputs back to mono => x3.
    ok &= expect_sample(slot.output[0][0], 6.0f, "mono->stereo frame 0");
    ok &= expect_sample(slot.output[0][1], 12.0f, "mono->stereo frame 1");
    return ok;
}

bool characterize_direct_stereo(const char* stereo_path) {
    Vst3Engine engine;
    if (!open_engine(engine, stereo_path, 2))
        return false;

    bool ok = true;
    AudioSlot slot{};
    slot.channels = 2;
    slot.frames = 2;
    slot.input[0][0] = 1.0f;
    slot.input[0][1] = 2.0f;
    slot.input[1][0] = 3.0f;
    slot.input[1][1] = 4.0f;

    ok &= expect(engine.process(slot), "direct stereo block must process");
    ok &= expect_sample(slot.output[0][0], 2.0f, "direct stereo L frame 0");
    ok &= expect_sample(slot.output[0][1], 4.0f, "direct stereo L frame 1");
    ok &= expect_sample(slot.output[1][0], 12.0f, "direct stereo R frame 0");
    ok &= expect_sample(slot.output[1][1], 16.0f, "direct stereo R frame 1");
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: r0-1-vst3-engine-process-test <mono.vst3> <stereo.vst3>\n";
        return 2;
    }

    bool ok = true;
    ok &= characterize_invalid_blocks(argv[1]);
    ok &= characterize_direct_mono_and_position(argv[1]);
    ok &= characterize_stereo_host_to_mono_plugin(argv[1]);
    ok &= characterize_mono_host_to_stereo_plugin(argv[2]);
    ok &= characterize_direct_stereo(argv[2]);

    if (!ok)
        return 1;
    std::cout << "R0-1 current AudioSlot engine seam characterized successfully\n";
    return 0;
}

#else

int main() { return 0; }

#endif
