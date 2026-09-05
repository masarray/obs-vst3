#include "rack/rack_broadcast_loudness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
constexpr std::uint32_t kSampleRate = 48000;
constexpr std::uint32_t kBlockFrames = 480;
constexpr double kPi = 3.1415926535897932384626433832795;

bool expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

void feed_sine(safevst3::rack::ui::RackBroadcastLoudnessMeter& meter,
               double frequency,
               double amplitude,
               double phase,
               double seconds,
               bool fade_in,
               std::uint64_t& sequence)
{
    std::array<float, kBlockFrames> left{};
    std::array<float, kBlockFrames> right{};
    float* channels[] = {left.data(), right.data()};
    const std::uint64_t total_frames = static_cast<std::uint64_t>(seconds * kSampleRate);
    std::uint64_t generated = 0;
    while (generated < total_frames) {
        const std::uint32_t frames = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(kBlockFrames, total_frames - generated));
        for (std::uint32_t i = 0; i < frames; ++i) {
            const std::uint64_t frame = generated + i;
            double gain = 1.0;
            if (fade_in && frame < kSampleRate / 10)
                gain = static_cast<double>(frame) / static_cast<double>(kSampleRate / 10);
            const float value = static_cast<float>(
                amplitude * gain * std::sin(2.0 * kPi * frequency *
                    static_cast<double>(frame) / kSampleRate + phase));
            left[i] = value;
            right[i] = value;
        }
        meter.process(channels, 2, frames, kSampleRate, ++sequence);
        generated += frames;
    }
}

void feed_silence(safevst3::rack::ui::RackBroadcastLoudnessMeter& meter,
                  double seconds,
                  std::uint64_t& sequence)
{
    std::array<float, kBlockFrames> left{};
    std::array<float, kBlockFrames> right{};
    float* channels[] = {left.data(), right.data()};
    const std::uint64_t blocks = static_cast<std::uint64_t>(
        seconds * kSampleRate / kBlockFrames);
    for (std::uint64_t block = 0; block < blocks; ++block)
        meter.process(channels, 2, kBlockFrames, kSampleRate, ++sequence);
}
} // namespace

int main()
{
    using safevst3::rack::ui::RackBroadcastLoudnessMeter;
    bool ok = true;
    std::uint64_t sequence = 0;

    // A 1 kHz stereo sine at -20 dBFS measures about -20.04 LUFS with this
    // BS.1770 K-weighting/gating path; FFmpeg ebur128 reports -20.0 LUFS.
    RackBroadcastLoudnessMeter loudness;
    feed_sine(loudness, 1000.0, std::pow(10.0, -20.0 / 20.0),
              0.0, 3.0, false, sequence);
    auto snapshot = loudness.snapshot();
    ok &= expect(snapshot.integrated_valid,
                 "LUFS-I must become valid after 400 ms");
    ok &= expect(std::fabs(snapshot.integrated_lufs - (-20.04f)) < 0.15f,
                 "1 kHz -20 dBFS stereo reference must measure near -20.04 LUFS");

    // Transition blocks are expected to move the result slightly; sustained
    // silence itself is rejected by the BS.1770 absolute/relative gates.
    feed_silence(loudness, 2.0, sequence);
    snapshot = loudness.snapshot();
    ok &= expect(std::fabs(snapshot.integrated_lufs - (-20.04f)) < 0.30f,
                 "trailing silence must remain gated out of programme LUFS-I");

    // 12 kHz at pi/4 phase has sample peaks ~2.99 dB below its continuous sine
    // peak. 4x polyphase reconstruction must recover that inter-sample peak.
    RackBroadcastLoudnessMeter true_peak;
    sequence = 0;
    feed_sine(true_peak, 12000.0, 0.8, kPi / 4.0,
              1.0, true, sequence);
    snapshot = true_peak.snapshot();
    const float expected_dbtp = static_cast<float>(20.0 * std::log10(0.8));
    ok &= expect(snapshot.true_peak_valid,
                 "dBTP must be valid after audio arrives");
    ok &= expect(std::fabs(snapshot.true_peak_dbtp - expected_dbtp) < 0.10f,
                 "4x true-peak reconstruction must recover the continuous sine peak");

    if (!ok)
        return 1;
    std::cout << "P5 BS.1770 LUFS-I gating + 4x dBTP reconstruction contract: PASS\n";
    return 0;
}
