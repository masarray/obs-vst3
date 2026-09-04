#ifdef _WIN32

// R3-5 is the stable production composition layer. The checked-in R3-4 source
// remains the qualified preset/editor implementation; CMake supplies an
// include-only copy with its entrypoint renamed so this translation unit can
// add zero-action Session Snapshot restore/checkpoint semantics without
// rewriting the proven Rack DSP/control seams.
#include "rack/main_r3_4.cpp"

#include <chrono>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace {

using safevst3::rack::RackSessionLoadSource;
using safevst3::rack::RackSessionSnapshot;

constexpr auto kSessionDirtyDebounce = std::chrono::milliseconds(250);
constexpr auto kSessionFallbackCheckpoint = std::chrono::seconds(15);

struct SessionLaunchOptions {
    std::filesystem::path snapshot_path;
    std::array<std::uint8_t, 16> rack_id{};
    std::string rack_id_text;
    bool enabled = false;
};

bool is_hex_digit(wchar_t ch) noexcept
{
    return (ch >= L'0' && ch <= L'9') ||
           (ch >= L'a' && ch <= L'f') ||
           (ch >= L'A' && ch <= L'F');
}

std::uint8_t hex_value(wchar_t ch) noexcept
{
    if (ch >= L'0' && ch <= L'9')
        return static_cast<std::uint8_t>(ch - L'0');
    if (ch >= L'a' && ch <= L'f')
        return static_cast<std::uint8_t>(10 + ch - L'a');
    return static_cast<std::uint8_t>(10 + ch - L'A');
}

bool decode_rack_session_id(const std::wstring& text,
                            std::array<std::uint8_t, 16>& destination,
                            std::string& narrow_text) noexcept
{
    if (text.size() != destination.size() * 2u)
        return false;
    narrow_text.clear();
    narrow_text.reserve(text.size());
    for (std::size_t i = 0; i < destination.size(); ++i) {
        const wchar_t hi = text[i * 2u];
        const wchar_t lo = text[i * 2u + 1u];
        if (!is_hex_digit(hi) || !is_hex_digit(lo))
            return false;
        destination[i] = static_cast<std::uint8_t>(
            (hex_value(hi) << 4u) | hex_value(lo));
        narrow_text.push_back(static_cast<char>(hi));
        narrow_text.push_back(static_cast<char>(lo));
    }
    return true;
}

bool parse_r3_5_options(int argc, wchar_t** argv,
                        Options& options, SessionLaunchOptions& session)
{
    std::wstring snapshot_path;
    std::wstring session_id;
    std::vector<wchar_t*> forwarded;
    forwarded.reserve(static_cast<std::size_t>(argc));
    forwarded.push_back(argv[0]);

    for (int index = 1; index < argc; ++index) {
        const std::wstring_view arg(argv[index]);
        if (arg == L"--session-snapshot" || arg == L"--session-id") {
            if (index + 1 >= argc)
                return false;
            wchar_t* value = argv[++index];
            if (arg == L"--session-snapshot")
                snapshot_path = value;
            else
                session_id = value;
            continue;
        }
        forwarded.push_back(argv[index]);
    }

    if (snapshot_path.empty() != session_id.empty())
        return false;
    if (!snapshot_path.empty()) {
        if (!decode_rack_session_id(session_id, session.rack_id, session.rack_id_text))
            return false;
        session.snapshot_path = std::filesystem::path(snapshot_path);
        session.enabled = !session.snapshot_path.empty();
        if (!session.enabled)
            return false;
    }

    return parse_options(static_cast<int>(forwarded.size()), forwarded.data(), options);
}

RackUiSnapshot build_r3_5_ui_snapshot(const DynamicRackState& state,
                                      std::uint64_t generation) noexcept
{
    RackUiSnapshot snapshot = build_dynamic_ui_snapshot(state, generation);
    for (std::uint32_t index = 0; index < state.slot_count; ++index) {
        const RackUiSlotHealth health = state.slots[index].health;
        if (health == RackUiSlotHealth::Missing ||
            health == RackUiSlotHealth::Quarantined ||
            health == RackUiSlotHealth::NeedsAttention ||
            health == RackUiSlotHealth::Recovering ||
            health == RackUiSlotHealth::Loading)
            snapshot.slots[index].health = health;
    }
    return snapshot;
}

bool restore_session_snapshot(const SessionLaunchOptions& session,
                              DynamicRackState& state,
                              MissingPresetStateStore& missing_states,
                              GenerationStore& store,
                              RackSharedAudioRegion& region,
                              std::string& error)
{
    if (!session.enabled)
        return false;

    RackSessionSnapshot snapshot{};
    RackSessionLoadSource source = RackSessionLoadSource::None;
    if (!safevst3::rack::load_rack_session_snapshot_lkg(
            session.snapshot_path, snapshot, source, error))
        return false;
    if (snapshot.rack_id != session.rack_id) {
        error = "Rack Session Snapshot identity does not match this OBS Rack";
        return false;
    }
    if (snapshot.generation == std::numeric_limits<std::uint64_t>::max()) {
        error = "Rack Session Snapshot generation cannot be advanced";
        return false;
    }

    RackPreset materialization{};
    materialization.slots = snapshot.slots;
    DynamicRackState candidate{};
    MissingPresetStateStore candidate_missing{};
    if (!materialize_preset_candidate(
            materialization, region, candidate, candidate_missing, error))
        return false;

    state = std::move(candidate);
    missing_states = std::move(candidate_missing);
    store.generations[0] = RackChainGeneration{};
    build_active_generation(store.generations[0], state, snapshot.generation);
    store.generations[1] = RackChainGeneration{};
    store.readers[0].store(0, std::memory_order_relaxed);
    store.readers[1].store(0, std::memory_order_relaxed);
    store.published_index.store(0, std::memory_order_release);
    publish_dynamic_projection(region, state, snapshot.generation);
    return true;
}

bool checkpoint_session_snapshot(const SessionLaunchOptions& session,
                                 const DynamicRackState& state,
                                 const MissingPresetStateStore& missing_states,
                                 const GenerationStore& store,
                                 std::string& error)
{
    if (!session.enabled)
        return true;

    const std::uint32_t published = store.published_index.load(std::memory_order_acquire);
    const std::uint64_t generation = store.generations[published].number;
    if (generation == 0) {
        error = "Rack Session Snapshot cannot persist generation zero";
        return false;
    }

    RackPreset captured{};
    if (!capture_current_rack(state, missing_states, captured, error))
        return false;

    RackSessionSnapshot snapshot{};
    snapshot.rack_id = session.rack_id;
    snapshot.generation = generation;
    snapshot.slots = std::move(captured.slots);
    return safevst3::rack::write_rack_session_snapshot_atomic(
        session.snapshot_path, snapshot, error);
}

bool take_rack_state_dirty(DynamicRackState& state) noexcept
{
    bool dirty = false;
    for (std::uint32_t index = 0; index < state.slot_count; ++index) {
        if (state.slots[index].plugin && state.slots[index].plugin->take_state_dirty())
            dirty = true;
    }
    return dirty;
}

RackPresetUiAck queue_full_preset_ack(const RackPresetUiCommand& command) noexcept
{
    RackPresetUiAck ack{};
    ack.command_id = command.command_id;
    ack.result = RackPresetUiCommandResult::Failed;
    ack.preset_id = command.preset_id;
    return ack;
}

bool reserve_retired_capacity(std::vector<RetiredDynamicPlugin>& retired,
                              std::size_t additional) noexcept
{
    try {
        retired.reserve(retired.size() + additional);
        return true;
    } catch (...) {
        return false;
    }
}

int run_r3_5_product(const Options& options, const SessionLaunchOptions& session)
{
    Endpoint endpoint;
    if (!endpoint.open(options)) {
        std::cerr << "Stable Rack helper could not open transport\n";
        return 3;
    }
    if (endpoint.region->magic != safevst3::rack::kRackProtocolMagic ||
        endpoint.region->version != safevst3::rack::kRackProtocolVersion ||
        endpoint.region->sample_rate == 0 || endpoint.region->channels == 0 ||
        endpoint.region->channels > kMaxChannels) {
        InterlockedExchange(&endpoint.region->host_status, static_cast<long>(RackHostStatus::Error));
        SetEvent(endpoint.ready);
        return 4;
    }

    clear_rack_breadcrumb(*endpoint.region);

    GenerationStore store;
    initialize_empty_generation_store(store);
    DynamicRackState state;
    MissingPresetStateStore missing_states;
    publish_dynamic_projection(*endpoint.region, state, store.generations[0].number);

    if (session.enabled) {
        std::string restore_error;
        if (!restore_session_snapshot(
                session, state, missing_states, store, *endpoint.region, restore_error)) {
            std::error_code exists_error;
            if (std::filesystem::exists(session.snapshot_path, exists_error) &&
                !restore_error.empty())
                std::cerr << "Stable Rack Session restore: " << restore_error << '\n';
        }
    }

    CatalogRuntime catalog_runtime;
    const std::filesystem::path cache = safevst3::rack::ui::rack_catalog_cache_path();
    if (!cache.empty()) {
        try {
            std::lock_guard lock(catalog_runtime.mutex);
            (void)catalog_runtime.catalog.load_cache(cache);
        } catch (...) {
            std::cerr << "Stable Rack catalog cache could not be loaded\n";
        }
    }

    PresetRuntime preset_runtime;
    preset_runtime.library = safevst3::rack::rack_preset_library_path();
    try {
        preset_runtime.entries.reserve(safevst3::rack::kRackPresetLibraryMaxEntries);
        preset_runtime.active_name.reserve(128);
        std::string preset_error;
        if (!preset_runtime.library.empty() &&
            !safevst3::rack::list_rack_presets(
                preset_runtime.library, preset_runtime.entries, preset_error) &&
            !preset_error.empty())
            std::cerr << "Stable Rack preset library: " << preset_error << '\n';
    } catch (const std::exception& exception) {
        preset_runtime.entries.clear();
        std::cerr << "Stable Rack preset library exception: " << exception.what() << '\n';
    } catch (...) {
        preset_runtime.entries.clear();
        std::cerr << "Stable Rack preset library exception\n";
    }

    DynamicCommandQueue command_queue;
    PresetCommandQueue preset_queue;
    std::vector<RetiredDynamicPlugin> retired;
    RackUiCommandReplayGuard replay;
    PresetReplayGuard preset_replay;
    RackVendorEditorManager vendor_editors;

    RackEditorWindow editor(
        [&](const RackUiCommand& command) {
            if (command_queue.push(command))
                return RackUiCommandAck{};
            return failed_ack(command, store);
        },
        [&](const RackPresetUiCommand& command) {
            if (preset_queue.push(command))
                return RackPresetUiAck{};
            return queue_full_preset_ack(command);
        });

    const std::uint32_t initial_index = store.published_index.load(std::memory_order_acquire);
    editor.publish_snapshot(build_r3_5_ui_snapshot(
        state, store.generations[initial_index].number));
    {
        std::lock_guard lock(catalog_runtime.mutex);
        editor.publish_catalog(catalog_runtime.catalog.snapshot());
    }
    editor.publish_presets(build_preset_ui_snapshot(preset_runtime));

    if (session.enabled) {
        std::string checkpoint_error;
        if (!checkpoint_session_snapshot(
                session, state, missing_states, store, checkpoint_error) &&
            !checkpoint_error.empty())
            std::cerr << "Stable Rack initial Session checkpoint: " << checkpoint_error << '\n';
    }

    std::thread command_worker([&] {
        auto next_periodic = std::chrono::steady_clock::now() + kSessionFallbackCheckpoint;
        std::optional<std::chrono::steady_clock::time_point> dirty_due;

        auto checkpoint = [&](const char* reason) {
            if (!session.enabled)
                return true;
            std::string checkpoint_error;
            if (checkpoint_session_snapshot(
                    session, state, missing_states, store, checkpoint_error)) {
                dirty_due.reset();
                next_periodic = std::chrono::steady_clock::now() + kSessionFallbackCheckpoint;
                return true;
            }
            if (!checkpoint_error.empty())
                std::cerr << "Stable Rack Session checkpoint (" << reason << "): "
                          << checkpoint_error << '\n';
            dirty_due = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            return false;
        };

        while (!command_queue.stopping() &&
               InterlockedCompareExchange(&endpoint.region->shutdown_requested, 0, 0) == 0) {
            vendor_editors.pump_messages();

            const auto now = std::chrono::steady_clock::now();
            if (session.enabled && take_rack_state_dirty(state) && !dirty_due)
                dirty_due = now + kSessionDirtyDebounce;
            if (session.enabled &&
                ((dirty_due && now >= *dirty_due) || now >= next_periodic))
                (void)checkpoint(dirty_due ? "native edit" : "periodic");

            RackPresetUiCommand preset_command{};
            if (preset_queue.try_pop(preset_command)) {
                RackPresetUiAck preset_ack{};
                if (preset_replay.lookup(preset_command, preset_ack)) {
                    editor.apply_preset_ack(preset_ack);
                    continue;
                }

                if (!reserve_retired_capacity(retired, safevst3::rack::kRackMaxSlots)) {
                    preset_ack = queue_full_preset_ack(preset_command);
                    editor.apply_preset_ack(preset_ack);
                    continue;
                }

                bool rack_changed = false;
                std::uint64_t rack_generation = 0;
                preset_ack = execute_preset_command(
                    preset_command, preset_runtime, state, missing_states,
                    store, *endpoint.region, vendor_editors, retired,
                    rack_changed, rack_generation);
                preset_replay.remember(preset_command, preset_ack);
                editor.apply_preset_ack(preset_ack);
                if (preset_ack.result == RackPresetUiCommandResult::Accepted) {
                    editor.publish_presets(build_preset_ui_snapshot(preset_runtime));
                    if (rack_changed) {
                        editor.publish_snapshot(build_r3_5_ui_snapshot(state, rack_generation));
                        (void)checkpoint("preset load");
                    }
                }
                continue;
            }

            RackUiCommand command{};
            if (!command_queue.pop(command))
                continue;

            RackUiCommandAck ack{};
            if (replay.lookup(command, ack)) {
                editor.apply_ack(ack);
                continue;
            }

            if (!reserve_retired_capacity(retired, safevst3::rack::kRackMaxSlots)) {
                ack = failed_ack(command, store);
                editor.apply_ack(ack);
                continue;
            }

            bool topology_changed = false;
            ack = execute_dynamic_command(command, state, store, *endpoint.region,
                                          catalog_runtime, editor, vendor_editors,
                                          retired, topology_changed);
            replay.remember(command, ack);
            editor.apply_ack(ack);
            if (ack.result == RackUiCommandResult::Accepted && topology_changed) {
                missing_states.prune(state);
                const std::uint32_t current = store.published_index.load(std::memory_order_acquire);
                editor.publish_snapshot(build_r3_5_ui_snapshot(
                    state, store.generations[current].number));
                (void)checkpoint("Rack edit");
            }
        }
        vendor_editors.close_all();
        vendor_editors.pump_messages();
    });

    InterlockedExchange(&endpoint.region->host_status, static_cast<long>(RackHostStatus::Ready));
    MemoryBarrier();
    SetEvent(endpoint.ready);

    std::thread dsp([&] { dsp_loop(endpoint, store); });
    std::thread ui_control;
    if (options.ui_enabled)
        ui_control = std::thread([&] { ui_open_loop(endpoint, editor); });

    dsp.join();
    preset_queue.stop();
    command_queue.stop();
    if (command_worker.joinable())
        command_worker.join();
    if (ui_control.joinable())
        ui_control.join();

    catalog_runtime.scanner_thread.request_stop();
    catalog_runtime.scanner_thread = std::jthread{};

    InterlockedExchange(&endpoint.region->host_status, static_cast<long>(RackHostStatus::ShuttingDown));
    editor.shutdown();
    reap_retired_plugins(store, retired);
    close_dynamic_state(state, retired);
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    Options options;
    SessionLaunchOptions session;
    if (!parse_r3_5_options(argc, argv, options, session)) {
        std::cerr << "usage: obs-safe-vst3-rack-host --mapping <name> --request-event <name> "
                     "--response-event <name> --ready-event <name> "
                     "[--plugin-a <vst3> --plugin-b <vst3>] "
                     "[--topology-request-event <name> --topology-response-event <name> --plugin-c <vst3>] "
                     "[--ui-open-event <name>] "
                     "[--session-snapshot <file> --session-id <32-hex>]\n";
        return 2;
    }

    if (options.fixture_plugins_enabled)
        return safevst3_r3_1_legacy_run(options);
    return run_r3_5_product(options, session);
}

#else
int main() { return 0; }
#endif
