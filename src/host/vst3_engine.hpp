#pragma once

#ifdef _WIN32

#include "common/protocol.hpp"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include <memory>
#include <string>

namespace safevst3 {

class Vst3Engine {
public:
    Vst3Engine() = default;
    ~Vst3Engine();

    Vst3Engine(const Vst3Engine&) = delete;
    Vst3Engine& operator=(const Vst3Engine&) = delete;

    bool open(const std::string& path,
              const std::string& class_id,
              std::uint32_t sample_rate,
              std::uint32_t channels,
              std::string& error);
    void close() noexcept;
    bool process(AudioSlot& slot) noexcept;

    const std::string& plugin_name() const noexcept { return plugin_name_; }
    std::uint32_t latency_samples() const noexcept { return latency_samples_; }

private:
    bool configure_buses(std::uint32_t channels, std::string& error);

    Steinberg::IPtr<Steinberg::Vst::HostApplication> host_;
    std::shared_ptr<Steinberg::VST3::Hosting::Module> module_;
    Steinberg::IPtr<Steinberg::Vst::PlugProvider> provider_;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component_;
    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> processor_;
    Steinberg::Vst::HostProcessData process_data_;
    Steinberg::Vst::ProcessSetup process_setup_{};
    Steinberg::Vst::ProcessContext process_context_{};
    Steinberg::int32 main_input_bus_ = -1;
    Steinberg::int32 main_output_bus_ = -1;
    std::string plugin_name_;
    std::uint32_t sample_rate_ = 0;
    std::uint32_t channels_ = 0;
    std::uint32_t latency_samples_ = 0;
    Steinberg::int64 sample_position_ = 0;
};

} // namespace safevst3

#endif
