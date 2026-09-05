#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

namespace safevst3::rack::ui {

struct RackBroadcastLoudnessSnapshot {
    std::uint64_t sequence = 0;
    float integrated_lufs = -120.0f;
    float true_peak_dbtp = -120.0f;
    bool integrated_valid = false;
    bool true_peak_valid = false;
};

// Live-broadcast loudness meter for the final Rack output.
//
// Integrated loudness follows the ITU-R BS.1770 / EBU R128 measurement shape:
// K-weighting, 400 ms blocks with 75% overlap, -70 LUFS absolute gating and a
// relative gate 10 LU below the absolute-gated mean. Stable Rack v0.6 is mono/
// stereo, so both supported channels use unity loudness weighting.
//
// True peak is reconstructed at quarter-sample positions with a 4x, 33-tap
// windowed-sinc polyphase interpolator. The integer sample itself is also tested.
// The meter is intentionally observation-only and owns no locks or allocations
// on the audio path after its rare sample-rate/channel reconfiguration.
class RackBroadcastLoudnessMeter {
public:
    void reset() noexcept
    {
        configured_sample_rate_ = 0;
        configured_channels_ = 0;
        reset_measurement_state();
        publish_snapshot(0);
    }

    void process(float* const* audio,
                 std::uint32_t channels,
                 std::uint32_t frames,
                 std::uint32_t sample_rate,
                 std::uint64_t sequence) noexcept
    {
        if (!audio || frames == 0 || sample_rate < 8000 || sample_rate > 384000 ||
            channels == 0 || channels > kSupportedChannels) {
            publish_snapshot(sequence);
            return;
        }

        if (sample_rate != configured_sample_rate_ || channels != configured_channels_)
            configure(sample_rate, channels);

        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            double weighted_square = 0.0;

            true_peak_write_index_ = (true_peak_write_index_ + 1u) % kTruePeakTaps;
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                const float* samples = audio[channel];
                const float sample = samples ? samples[frame] : 0.0f;
                const double filtered = k_weighting_[channel].process(sample);
                weighted_square += filtered * filtered;

                true_peak_history_[channel][true_peak_write_index_] = sample;
                true_peak_session_linear_ =
                    std::max(true_peak_session_linear_, std::fabs(static_cast<double>(sample)));
            }

            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                for (const auto& phase : kTruePeakPhases) {
                    double interpolated = 0.0;
                    for (std::size_t tap = 0; tap < kTruePeakTaps; ++tap) {
                        const std::size_t history_index =
                            (true_peak_write_index_ + kTruePeakTaps - tap) % kTruePeakTaps;
                        interpolated += phase[tap] *
                                        static_cast<double>(true_peak_history_[channel][history_index]);
                    }
                    true_peak_session_linear_ =
                        std::max(true_peak_session_linear_, std::fabs(interpolated));
                }
            }

            subblock_energy_sum_ += weighted_square;
            ++subblock_frame_count_;
            if (subblock_frame_count_ >= subblock_frames_)
                finish_100ms_subblock();
        }

        publish_snapshot(sequence);
    }

    RackBroadcastLoudnessSnapshot snapshot() const noexcept
    {
        RackBroadcastLoudnessSnapshot result{};
        result.sequence = sequence_.load(std::memory_order_acquire);

        const std::int32_t lufs = integrated_millilu_.load(std::memory_order_relaxed);
        const std::int32_t dbtp = true_peak_millidb_.load(std::memory_order_relaxed);
        result.integrated_valid = lufs != kMetricInvalid;
        result.true_peak_valid = dbtp != kMetricInvalid;
        if (result.integrated_valid)
            result.integrated_lufs = static_cast<float>(lufs) / 1000.0f;
        if (result.true_peak_valid)
            result.true_peak_dbtp = static_cast<float>(dbtp) / 1000.0f;
        return result;
    }

private:
    static constexpr std::uint32_t kSupportedChannels = 2;
    static constexpr double kLoudnessOffset = -0.691;
    static constexpr double kAbsoluteGateLufs = -70.0;
    static constexpr double kHistogramFloorLufs = -70.0;
    static constexpr double kHistogramStepLu = 0.1;
    static constexpr std::size_t kHistogramBins = 1000; // -70 to +30 LUFS
    static constexpr std::size_t kTruePeakTaps = 33;
    static constexpr std::int32_t kMetricInvalid = std::numeric_limits<std::int32_t>::min();

    struct Biquad {
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
        double z1 = 0.0;
        double z2 = 0.0;

        void set(double nb0, double nb1, double nb2, double na1, double na2) noexcept
        {
            b0 = nb0;
            b1 = nb1;
            b2 = nb2;
            a1 = na1;
            a2 = na2;
            z1 = 0.0;
            z2 = 0.0;
        }

        double process(double sample) noexcept
        {
            const double output = b0 * sample + z1;
            z1 = b1 * sample - a1 * output + z2;
            z2 = b2 * sample - a2 * output;
            return output;
        }
    };

    struct KWeightingChannel {
        Biquad shelf{};
        Biquad high_pass{};

        double process(double sample) noexcept
        {
            return high_pass.process(shelf.process(sample));
        }
    };

    struct HistogramBin {
        std::uint64_t count = 0;
        double energy_sum = 0.0;
    };

    inline static constexpr std::array<std::array<double, kTruePeakTaps>, 3>
        kTruePeakPhases{{
            {{
                1.985198320313e-19, 5.373209305810e-05, -2.397008164151e-04,
                6.163520826091e-04, -1.274087258937e-03, 2.338949029091e-03,
                -3.976624468650e-03, 6.399709744791e-03, -9.884197825162e-03,
                1.480764677528e-02, -2.173761147078e-02, 3.164516773411e-02,
                -4.647541117047e-02, 7.097814418565e-02, -1.208270820637e-01,
                2.956866799879e-01, 9.012062722942e-01, -1.774120079927e-01,
                9.397661938291e-02, -6.005842969555e-02, 4.100771573865e-02,
                -2.863134223563e-02, 1.999860255311e-02, -1.378642975629e-02,
                9.285155532728e-03, -6.053779488316e-03, 3.782642787252e-03,
                -2.234995738909e-03, 1.222083697347e-03, -5.930935134540e-04,
                2.312902614531e-04, -5.197038508898e-05, -1.924115295072e-19,
            }},
            {{
                2.861246663475e-19, 7.752830989740e-05, -3.462908234782e-04,
                8.917263084721e-04, -1.846467802897e-03, 3.396584810228e-03,
                -5.788927773946e-03, 9.344411275421e-03, -1.448719841647e-02,
                2.181117199879e-02, -3.223439455889e-02, 4.737956161623e-02,
                -7.062996715501e-02, 1.107439131661e-01, -1.999464130056e-01,
                6.291085104499e-01, 6.391411066091e-01, -2.097028368166e-01,
                1.199678478033e-01, -7.910279511868e-02, 5.493441889834e-02,
                -3.876509586782e-02, 2.727525693444e-02, -1.890301573229e-02,
                1.278282213218e-02, -8.360789035903e-03, 5.237601319284e-03,
                -3.101229609339e-03, 1.698750378666e-03, -8.256725078446e-04,
                3.224086977211e-04, -7.252648345241e-05, -2.687837774780e-19,
            }},
            {{
                2.066564808387e-19, 5.605899790770e-05, -2.507210218249e-04,
                6.466025006094e-04, -1.341278406557e-03, 2.472519768079e-03,
                -4.224855692171e-03, 6.841460088213e-03, -1.064975313603e-02,
                1.611922723318e-02, -2.399687997782e-02, 3.564892987494e-02,
                -5.405122777677e-02, 8.743981532123e-02, -1.705009248970e-01,
                8.941027521648e-01, 3.027870989655e-01, -1.277289645950e-01,
                7.750042040771e-02, -5.246388919274e-02, 3.698241900516e-02,
                -2.634920903800e-02, 1.866423998275e-02, -1.299937680095e-02,
                8.824081169852e-03, -5.788927766949e-03, 3.635340944426e-03,
                -2.156878946622e-03, 1.183480946962e-03, -5.760640459975e-04,
                2.252239687580e-04, -5.072004572601e-05, -1.881499303158e-19,
            }},
        }};

    static double energy_to_lufs(double energy) noexcept
    {
        if (!(energy > 1.0e-30))
            return -120.0;
        return kLoudnessOffset + 10.0 * std::log10(energy);
    }

    static std::int32_t encode_metric(double value) noexcept
    {
        if (!std::isfinite(value))
            return kMetricInvalid;
        value = std::clamp(value, -120.0, 30.0);
        return static_cast<std::int32_t>(std::llround(value * 1000.0));
    }

    void configure(std::uint32_t sample_rate, std::uint32_t channels) noexcept
    {
        configured_sample_rate_ = sample_rate;
        configured_channels_ = channels;
        reset_measurement_state();
        subblock_frames_ = std::max<std::uint32_t>(1u,
            static_cast<std::uint32_t>(std::llround(static_cast<double>(sample_rate) * 0.100)));

        constexpr double pi = 3.1415926535897932384626433832795;
        const double fs = static_cast<double>(sample_rate);

        // BS.1770 K-weighting stage 1: +4 dB high-frequency shelving filter.
        const double shelf_f0 = 1681.974450955533;
        const double shelf_gain_db = 3.999843853973347;
        const double shelf_q = 0.7071752369554196;
        const double shelf_k = std::tan(pi * shelf_f0 / fs);
        const double vh = std::pow(10.0, shelf_gain_db / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double shelf_den = 1.0 + shelf_k / shelf_q + shelf_k * shelf_k;
        const double sb0 = (vh + vb * shelf_k / shelf_q + shelf_k * shelf_k) / shelf_den;
        const double sb1 = 2.0 * (shelf_k * shelf_k - vh) / shelf_den;
        const double sb2 = (vh - vb * shelf_k / shelf_q + shelf_k * shelf_k) / shelf_den;
        const double sa1 = 2.0 * (shelf_k * shelf_k - 1.0) / shelf_den;
        const double sa2 = (1.0 - shelf_k / shelf_q + shelf_k * shelf_k) / shelf_den;

        // BS.1770 K-weighting stage 2: RLB high-pass filter.
        const double hp_f0 = 38.13547087602444;
        const double hp_q = 0.5003270373238773;
        const double hp_k = std::tan(pi * hp_f0 / fs);
        const double hp_den = 1.0 + hp_k / hp_q + hp_k * hp_k;
        const double hb0 = 1.0 / hp_den;
        const double hb1 = -2.0 / hp_den;
        const double hb2 = 1.0 / hp_den;
        const double ha1 = 2.0 * (hp_k * hp_k - 1.0) / hp_den;
        const double ha2 = (1.0 - hp_k / hp_q + hp_k * hp_k) / hp_den;

        for (std::uint32_t channel = 0; channel < kSupportedChannels; ++channel) {
            k_weighting_[channel].shelf.set(sb0, sb1, sb2, sa1, sa2);
            k_weighting_[channel].high_pass.set(hb0, hb1, hb2, ha1, ha2);
        }
    }

    void reset_measurement_state() noexcept
    {
        for (auto& channel : k_weighting_)
            channel = KWeightingChannel{};
        for (auto& channel : true_peak_history_)
            channel.fill(0.0f);
        true_peak_write_index_ = 0;
        true_peak_session_linear_ = 0.0;

        subblock_energy_sum_ = 0.0;
        subblock_frame_count_ = 0;
        subblock_frames_ = 1;
        subblock_energies_.fill(0.0);
        subblock_write_index_ = 0;
        completed_subblocks_ = 0;
        for (auto& bin : histogram_)
            bin = HistogramBin{};
        integrated_lufs_ = -120.0;
        integrated_valid_ = false;
    }

    void finish_100ms_subblock() noexcept
    {
        const double energy = subblock_energy_sum_ /
                              static_cast<double>(std::max<std::uint32_t>(1u, subblock_frame_count_));
        subblock_energy_sum_ = 0.0;
        subblock_frame_count_ = 0;

        subblock_energies_[subblock_write_index_] = energy;
        subblock_write_index_ = (subblock_write_index_ + 1u) % subblock_energies_.size();
        completed_subblocks_ = std::min<std::uint32_t>(
            completed_subblocks_ + 1u,
            static_cast<std::uint32_t>(subblock_energies_.size()));
        if (completed_subblocks_ < subblock_energies_.size())
            return;

        double block_energy = 0.0;
        for (const double subblock : subblock_energies_)
            block_energy += subblock;
        block_energy /= static_cast<double>(subblock_energies_.size());

        const double block_lufs = energy_to_lufs(block_energy);
        if (block_lufs < kAbsoluteGateLufs)
            return;

        const double bounded_lufs = std::clamp(
            block_lufs,
            kHistogramFloorLufs,
            kHistogramFloorLufs + kHistogramStepLu *
                static_cast<double>(kHistogramBins - 1));
        const std::size_t bin_index = std::min<std::size_t>(
            kHistogramBins - 1,
            static_cast<std::size_t>((bounded_lufs - kHistogramFloorLufs) /
                                     kHistogramStepLu));
        ++histogram_[bin_index].count;
        histogram_[bin_index].energy_sum += block_energy;
        update_integrated_loudness();
    }

    void update_integrated_loudness() noexcept
    {
        std::uint64_t absolute_count = 0;
        double absolute_energy = 0.0;
        for (const auto& bin : histogram_) {
            absolute_count += bin.count;
            absolute_energy += bin.energy_sum;
        }
        if (absolute_count == 0)
            return;

        const double absolute_mean = absolute_energy / static_cast<double>(absolute_count);
        const double relative_gate = std::max(
            kAbsoluteGateLufs,
            energy_to_lufs(absolute_mean) - 10.0);

        std::uint64_t gated_count = 0;
        double gated_energy = 0.0;
        for (std::size_t index = 0; index < histogram_.size(); ++index) {
            const double bin_lufs = kHistogramFloorLufs +
                                    (static_cast<double>(index) + 0.5) * kHistogramStepLu;
            if (bin_lufs < relative_gate)
                continue;
            gated_count += histogram_[index].count;
            gated_energy += histogram_[index].energy_sum;
        }
        if (gated_count == 0)
            return;

        integrated_lufs_ = energy_to_lufs(gated_energy / static_cast<double>(gated_count));
        integrated_valid_ = true;
    }

    void publish_snapshot(std::uint64_t sequence) noexcept
    {
        const double true_peak_db = true_peak_session_linear_ > 1.0e-12
            ? 20.0 * std::log10(true_peak_session_linear_)
            : -120.0;
        integrated_millilu_.store(
            integrated_valid_ ? encode_metric(integrated_lufs_) : kMetricInvalid,
            std::memory_order_relaxed);
        true_peak_millidb_.store(encode_metric(true_peak_db), std::memory_order_relaxed);
        sequence_.store(sequence, std::memory_order_release);
    }

    std::uint32_t configured_sample_rate_ = 0;
    std::uint32_t configured_channels_ = 0;
    std::uint32_t subblock_frames_ = 1;

    std::array<KWeightingChannel, kSupportedChannels> k_weighting_{};
    double subblock_energy_sum_ = 0.0;
    std::uint32_t subblock_frame_count_ = 0;
    std::array<double, 4> subblock_energies_{};
    std::size_t subblock_write_index_ = 0;
    std::uint32_t completed_subblocks_ = 0;
    std::array<HistogramBin, kHistogramBins> histogram_{};
    double integrated_lufs_ = -120.0;
    bool integrated_valid_ = false;

    std::array<std::array<float, kTruePeakTaps>, kSupportedChannels> true_peak_history_{};
    std::size_t true_peak_write_index_ = 0;
    double true_peak_session_linear_ = 0.0;

    std::atomic<std::int32_t> integrated_millilu_{kMetricInvalid};
    std::atomic<std::int32_t> true_peak_millidb_{kMetricInvalid};
    std::atomic<std::uint64_t> sequence_{0};
};

inline RackBroadcastLoudnessMeter g_rack_broadcast_loudness{};

} // namespace safevst3::rack::ui
