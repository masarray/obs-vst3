from pathlib import Path
import subprocess


def fail(message):
    raise SystemExit(message)


def require(condition, message):
    if not condition:
        fail(message)


def replace_once(text, old, new, label):
    count = text.count(old)
    require(count == 1, f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


root = Path('.')
engine_cpp_path = root / 'src/host/vst3_engine.cpp'
engine_hpp_path = root / 'src/host/vst3_engine.hpp'
hosted_cpp_path = root / 'src/host/hosted_plugin.cpp'
hosted_hpp_path = root / 'src/host/hosted_plugin.hpp'

blob = subprocess.check_output(['git', 'hash-object', str(engine_cpp_path)], text=True).strip()
require(blob == '9e92d2a74847db407d7a354d668b0cf921ef3e2b',
        f'unexpected vst3_engine.cpp blob {blob}; refusing mechanical extraction')

old_cpp = engine_cpp_path.read_text(encoding='utf-8')
old_hpp = engine_hpp_path.read_text(encoding='utf-8')

contract = '''#pragma once

#include <cstddef>
#include <cstdint>

namespace safevst3 {

inline constexpr std::uint32_t kMaxChannels = 2;
inline constexpr std::uint32_t kMaxFrames = 2048;
inline constexpr std::uint32_t kMaxParameters = 256;
inline constexpr std::size_t kMaxStateBytes = 16u * 1024u * 1024u;

enum ParameterFlags : std::uint32_t {
    ParameterCanAutomate = 1u << 0,
    ParameterReadOnly = 1u << 1,
    ParameterHidden = 1u << 2,
    ParameterList = 1u << 3,
    ParameterProgramChange = 1u << 4,
    ParameterBypass = 1u << 5,
};

} // namespace safevst3
'''
(root / 'src/common/vst3_host_contract.hpp').write_text(contract, encoding='utf-8')

protocol_path = root / 'src/common/protocol.hpp'
protocol = protocol_path.read_text(encoding='utf-8')
protocol = replace_once(protocol, '#pragma once\n\n',
                        '#pragma once\n\n#include "common/vst3_host_contract.hpp"\n\n',
                        'protocol host-contract include')
for line in (
    'inline constexpr std::uint32_t kMaxChannels = 2;\n',
    'inline constexpr std::uint32_t kMaxFrames = 2048;\n',
    'inline constexpr std::uint32_t kMaxParameters = 256;\n',
    'inline constexpr std::size_t kMaxStateBytes = 16u * 1024u * 1024u;\n',
):
    protocol = replace_once(protocol, line, '', f'remove duplicate {line.strip()}')
flags = '''enum ParameterFlags : std::uint32_t {
    ParameterCanAutomate = 1u << 0,
    ParameterReadOnly = 1u << 1,
    ParameterHidden = 1u << 2,
    ParameterList = 1u << 3,
    ParameterProgramChange = 1u << 4,
    ParameterBypass = 1u << 5,
};

'''
protocol = replace_once(protocol, flags, '', 'remove duplicate ParameterFlags')
protocol_path.write_text(protocol, encoding='utf-8')

state_path = root / 'src/common/state_snapshot.hpp'
state = state_path.read_text(encoding='utf-8')
state = replace_once(state, '#include "common/protocol.hpp"',
                     '#include "common/vst3_host_contract.hpp"',
                     'state snapshot host-contract include')
state_path.write_text(state, encoding='utf-8')

deep_hpp = old_hpp.replace('#include "common/protocol.hpp"',
                           '#include "common/vst3_host_contract.hpp"')
deep_hpp = deep_hpp.replace('Vst3Engine', 'HostedPlugin')
deep_hpp = replace_once(deep_hpp,
                        'class HostedPlugin final : public LatencyRestartTarget, public IoRestartLifecycleTarget {',
                        'class HostedPlugin : public LatencyRestartTarget, public IoRestartLifecycleTarget {',
                        'remove final from deep HostedPlugin')
deep_hpp = replace_once(deep_hpp, '    bool process(AudioSlot& slot) noexcept;\n', '',
                        'remove Single AudioSlot method from HostedPlugin')
require('AudioSlot' not in deep_hpp, 'HostedPlugin header still references AudioSlot')
require('common/protocol.hpp' not in deep_hpp, 'HostedPlugin header still references Single protocol')
hosted_hpp_path.write_text(deep_hpp, encoding='utf-8')

adapter_block = '''bool Vst3Engine::process(AudioSlot& slot) noexcept
{
    float* input[kMaxChannels] = {slot.input[0], slot.input[1]};
    float* output[kMaxChannels] = {slot.output[0], slot.output[1]};
    const ProcessBlockView block{
        input,
        output,
        slot.channels,
        slot.frames,
        slot.sequence,
    };
    return process(block);
}

'''
deep_cpp = replace_once(old_cpp, adapter_block, '', 'remove AudioSlot implementation from deep owner')
deep_cpp = replace_once(deep_cpp, '#include "host/vst3_engine.hpp"',
                        '#include "host/hosted_plugin.hpp"',
                        'deep source include')
deep_cpp = deep_cpp.replace('Vst3Engine', 'HostedPlugin')
require('AudioSlot' not in deep_cpp, 'HostedPlugin source still references AudioSlot')
require('Vst3Engine' not in deep_cpp, 'HostedPlugin source still references Vst3Engine')
hosted_cpp_path.write_text(deep_cpp, encoding='utf-8')

thin_hpp = '''#pragma once

#ifdef _WIN32

#include "common/protocol.hpp"
#include "host/hosted_plugin.hpp"

namespace safevst3 {

// Single-transport compatibility adapter. All VST3 lifecycle/state/process
// ownership lives in HostedPlugin; this class only maps AudioSlot buffers into
// the protocol-neutral ProcessBlockView seam.
class Vst3Engine final : public HostedPlugin {
public:
    Vst3Engine() = default;
    ~Vst3Engine() = default;

    Vst3Engine(const Vst3Engine&) = delete;
    Vst3Engine& operator=(const Vst3Engine&) = delete;

    using HostedPlugin::process;
    bool process(AudioSlot& slot) noexcept;
};

} // namespace safevst3

#endif
'''
engine_hpp_path.write_text(thin_hpp, encoding='utf-8')

thin_cpp = '''#ifdef _WIN32

#include "host/vst3_engine.hpp"

namespace safevst3 {

bool Vst3Engine::process(AudioSlot& slot) noexcept
{
    float* input[kMaxChannels] = {slot.input[0], slot.input[1]};
    float* output[kMaxChannels] = {slot.output[0], slot.output[1]};
    const ProcessBlockView block{
        input,
        output,
        slot.channels,
        slot.frames,
        slot.sequence,
    };
    return HostedPlugin::process(block);
}

} // namespace safevst3

#endif
'''
engine_cpp_path.write_text(thin_cpp, encoding='utf-8')

root_cmake_path = root / 'CMakeLists.txt'
root_cmake = root_cmake_path.read_text(encoding='utf-8')
root_cmake = replace_once(root_cmake,
    '-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/host/vst3_engine.cpp',
    '-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/host/hosted_plugin.cpp',
    'process-context source-contract target')
root_cmake = replace_once(root_cmake,
    '-DENGINE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/host/vst3_engine.cpp',
    '-DENGINE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/host/hosted_plugin.cpp',
    'strict-lifecycle source-contract target')
root_cmake_path.write_text(root_cmake, encoding='utf-8')

strict_path = root / 'tests/strict_lifecycle_source_contract.cmake'
strict = strict_path.read_text(encoding='utf-8')
strict = replace_once(strict,
    'file(READ "${ENGINE_DIR}/vst3_engine.hpp" ENGINE_HEADER)',
    'file(READ "${ENGINE_DIR}/hosted_plugin.hpp" ENGINE_HEADER)',
    'strict lifecycle deep header')
strict = strict.replace('bool Vst3Engine::configure_buses', 'bool HostedPlugin::configure_buses')
strict = strict.replace('bool Vst3Engine::activate_configured_buses', 'bool HostedPlugin::activate_configured_buses')
strict_path.write_text(strict, encoding='utf-8')

context_path = root / 'tests/process_context_source_contract.cmake'
context = context_path.read_text(encoding='utf-8')
context = context.replace('Vst3Engine::open', 'HostedPlugin::open')
context = context.replace('bool Vst3Engine::flush_parameter_changes() noexcept',
                          'bool HostedPlugin::flush_parameter_changes() noexcept')
context = context.replace('bool Vst3Engine::process(AudioSlot& slot) noexcept',
                          'bool HostedPlugin::process(const ProcessBlockView& block) noexcept')
context_path.write_text(context, encoding='utf-8')

r01_path = root / 'tests/r0_1/CMakeLists.txt'
r01 = r01_path.read_text(encoding='utf-8')
r01 = replace_once(r01,
    '    "${SAFEVST3_ROOT}/src/host/vst3_engine.cpp"\n',
    '    "${SAFEVST3_ROOT}/src/host/hosted_plugin.cpp"\n    "${SAFEVST3_ROOT}/src/host/vst3_engine.cpp"\n',
    'R0-1 deep+adapter sources')
r01_path.write_text(r01, encoding='utf-8')

r02_path = root / 'tests/r0_2/CMakeLists.txt'
r02 = r02_path.read_text(encoding='utf-8')
r02 = replace_once(r02, '    "${SAFEVST3_ROOT}/src/host/vst3_engine.cpp"\n', '',
                   'R0-2 must not link Single adapter')
r02 = replace_once(r02,
    '        -DHEADER=${SAFEVST3_ROOT}/src/host/hosted_plugin.hpp\n',
    '        -DHEADER=${SAFEVST3_ROOT}/src/host/hosted_plugin.hpp\n        -DSOURCE=${SAFEVST3_ROOT}/src/host/hosted_plugin.cpp\n',
    'R0-2 deep source contract input')
r02_path.write_text(r02, encoding='utf-8')

neutral_contract = '''if(NOT DEFINED HEADER OR NOT EXISTS "${HEADER}")
    message(FATAL_ERROR "R0-2 HostedPlugin header is missing")
endif()
if(NOT DEFINED SOURCE OR NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR "R0-2 HostedPlugin source is missing")
endif()

file(READ "${HEADER}" header_source)
file(READ "${SOURCE}" implementation_source)

foreach(forbidden IN ITEMS "AudioSlot" "SharedAudioRegion" "common/protocol.hpp" "Vst3Engine")
    string(FIND "${header_source}" "${forbidden}" header_position)
    if(NOT header_position EQUAL -1)
        message(FATAL_ERROR "HostedPlugin public seam must remain protocol-neutral; header contains forbidden token: ${forbidden}")
    endif()
    string(FIND "${implementation_source}" "${forbidden}" source_position)
    if(NOT source_position EQUAL -1)
        message(FATAL_ERROR "HostedPlugin deep implementation must remain outside Single transport; source contains forbidden token: ${forbidden}")
    endif()
endforeach()

string(FIND "${header_source}" "class HostedPlugin" class_position)
if(class_position EQUAL -1)
    message(FATAL_ERROR "HostedPlugin public seam must declare class HostedPlugin")
endif()
string(FIND "${header_source}" "ProcessBlockView" process_view_position)
if(process_view_position EQUAL -1)
    message(FATAL_ERROR "HostedPlugin public seam must expose ProcessBlockView processing")
endif()
string(FIND "${implementation_source}" "bool HostedPlugin::process(const ProcessBlockView& block) noexcept" deep_process_position)
if(deep_process_position EQUAL -1)
    message(FATAL_ERROR "HostedPlugin must own the deep ProcessBlockView implementation")
endif()

message(STATUS "R0-2 HostedPlugin deep seam is protocol-neutral and independent of the Single adapter")
'''
(root / 'tests/r0_2/hosted_plugin_protocol_neutral.cmake').write_text(neutral_contract, encoding='utf-8')

readme_path = root / 'tests/r0_2/README.md'
readme = readme_path.read_text(encoding='utf-8')
readme = readme.replace(
    '- public-header source contract rejecting Single transport types/layout includes.',
    '- deep header/source contract rejecting Single transport types/layout includes and `Vst3Engine` dependency;\n- focused target links `HostedPlugin` without the Single `vst3_engine.cpp` adapter.')
readme_path.write_text(readme, encoding='utf-8')

for bootstrap in (
    Path('.github/workflows/r0-2-deep-extract-bootstrap.yml'),
    Path('.github/workflows/r0-2-deep-extract-push.yml'),
    Path('scripts/r0_2_deep_extract.py'),
):
    if bootstrap.exists():
        bootstrap.unlink()

require('AudioSlot' not in hosted_hpp_path.read_text(), 'final HostedPlugin header contains AudioSlot')
require('AudioSlot' not in hosted_cpp_path.read_text(), 'final HostedPlugin source contains AudioSlot')
require('Vst3Engine' not in hosted_cpp_path.read_text(), 'final HostedPlugin source contains Vst3Engine')
require('src/host/vst3_engine.cpp' not in r02_path.read_text(), 'R0-2 focused target still links Single adapter')
subprocess.run(['git', 'diff', '--check'], check=True)
