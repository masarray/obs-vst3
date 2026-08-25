#pragma once

#include <cstddef>
#include <cstdint>

namespace safevst3 {

inline bool prepare_input_channels(const float* transport0,
                                   const float* transport1,
                                   std::uint32_t transport_channels,
                                   std::uint32_t plugin_channels,
                                   std::size_t frames,
                                   float* scratch0,
                                   float* scratch1,
                                   float* (&plugin)[2]) noexcept
{
    if (!transport0 || !scratch0 || !scratch1 || frames == 0 ||
        (transport_channels != 1 && transport_channels != 2) ||
        (plugin_channels != 1 && plugin_channels != 2))
        return false;

    if (transport_channels == 1 && plugin_channels == 1) {
        plugin[0] = const_cast<float*>(transport0);
        plugin[1] = nullptr;
        return true;
    }
    if (transport_channels == 2 && plugin_channels == 2) {
        if (!transport1)
            return false;
        plugin[0] = const_cast<float*>(transport0);
        plugin[1] = const_cast<float*>(transport1);
        return true;
    }
    if (transport_channels == 2 && plugin_channels == 1) {
        if (!transport1)
            return false;
        for (std::size_t i = 0; i < frames; ++i)
            scratch0[i] = 0.5f * (transport0[i] + transport1[i]);
        plugin[0] = scratch0;
        plugin[1] = nullptr;
        return true;
    }

    for (std::size_t i = 0; i < frames; ++i) {
        scratch0[i] = transport0[i];
        scratch1[i] = transport0[i];
    }
    plugin[0] = scratch0;
    plugin[1] = scratch1;
    return true;
}

inline bool prepare_output_channels(float* transport0,
                                    float* transport1,
                                    std::uint32_t transport_channels,
                                    std::uint32_t plugin_channels,
                                    float* scratch0,
                                    float* scratch1,
                                    float* (&plugin)[2]) noexcept
{
    if (!transport0 || !scratch0 || !scratch1 ||
        (transport_channels != 1 && transport_channels != 2) ||
        (plugin_channels != 1 && plugin_channels != 2))
        return false;

    if (transport_channels == plugin_channels) {
        plugin[0] = transport0;
        plugin[1] = plugin_channels == 2 ? transport1 : nullptr;
        return plugin_channels == 1 || transport1 != nullptr;
    }

    plugin[0] = scratch0;
    plugin[1] = plugin_channels == 2 ? scratch1 : nullptr;
    return true;
}

inline bool finalize_output_channels(float* transport0,
                                     float* transport1,
                                     std::uint32_t transport_channels,
                                     std::uint32_t plugin_channels,
                                     std::size_t frames,
                                     const float* plugin0,
                                     const float* plugin1) noexcept
{
    if (!transport0 || !plugin0 || frames == 0 ||
        (transport_channels != 1 && transport_channels != 2) ||
        (plugin_channels != 1 && plugin_channels != 2))
        return false;

    if (transport_channels == plugin_channels)
        return true;

    if (transport_channels == 2 && plugin_channels == 1) {
        if (!transport1)
            return false;
        for (std::size_t i = 0; i < frames; ++i) {
            transport0[i] = plugin0[i];
            transport1[i] = plugin0[i];
        }
        return true;
    }

    if (!plugin1)
        return false;
    for (std::size_t i = 0; i < frames; ++i)
        transport0[i] = 0.5f * (plugin0[i] + plugin1[i]);
    return true;
}

} // namespace safevst3
