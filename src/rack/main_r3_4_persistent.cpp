#ifdef _WIN32

// Reuse every qualified R3-4 implementation detail in this translation unit,
// but give the shipping product entrypoint a persistence-aware lifecycle.
// The shipping CMake target provides a generated R3-4 include whose only
// difference is a renamed legacy wmain, so this file can own the real product
// entrypoint without preprocessor collisions.
#include <rack/main_r3_4.cpp>

namespace {

bool restore_working_rack_session(
    safevst3::rack::RackSessionRuntime& runtime,
    DynamicRackState& state,
    MissingPresetStateStore& missing_states,
    GenerationStore& store,
    RackSharedAudioRegion& region) noexcept
{
    if (!runtime.enabled() || runtime.path().empty() ||
        !std::filesystem::exists(runtime.path()))
        return false;

    safevst3::rack::RackSessionSnapshot snapshot{};
    safevst3::rack::RackSessionLoadSource source =
        safevst3::rack::RackSessionLoadSource::None;
    std::string error;
    if (!safevst3::rack::load_rack_session_snapshot_lkg(
            runtime.path(), snapshot, source, error)) {
        if (!error.empty())
            std::cerr << "Rack session restore: " << error << '\n';
        return false;
    }

    RackPreset working{};
    working.slots = snapshot.slots;
    DynamicRackState candidate{};
    MissingPresetStateStore candidate_missing{};
    if (!materialize_preset_candidate(
            working, region, candidate, candidate_missing, error)) {
        // materialize_preset_candidate owns candidate cleanup on every failure.
        if (!error.empty())
            std::cerr << "Rack session materialize: " << error << '\n';
        return false;
    }

    state = std::move(candidate);
    missing_states = std::move(candidate_missing);
    runtime.adopt_loaded(snapshot);

    std::uint64_t chain_generation = snapshot.generation;
    if (chain_generation == 0 ||
        chain_generation == std::numeric_limits<std::uint64_t>::max())
        chain_generation = 1;
    build_active_generation(store.generations[0], state, chain_generation);
    store.generations[1] = RackChainGeneration{};
    store.readers[0].store(0, std::memory_order_relaxed);
    store.readers[1].store(0, std::memory_order_relaxed);
    store.published_index.store(0, std::memory_order_release);
    publish_dynamic_projection(region, state, chain_generation);

    std::cerr << "Rack session restored from "
              << (source == safevst3::rack::RackSessionLoadSource::Current
                      ? "primary"
                      : "last-known-good")
              << " snapshot with " << state.slot_count << " slot(s)\n";
    return true;
}

bool persist_working_rack_session(
    safevst3::rack::RackSessionRuntime& runtime,
    const DynamicRackState& state,
    const MissingPresetStateStore& missing_states,
    const char* reason) noexcept
{
    if (!runtime.enabled())
        return true;

    try {
        RackPreset working{};
        std::string error;
        if (!capture_current_rack(state, missing_states, working, error)) {
            std::cerr << "Rack session capture"
                      << (reason ? std::string(" (") + reason + ")" : std::string{})
                      << ": " << (error.empty() ? "failed" : error) << '\n';
            return false;
        }

        safevst3::rack::RackSessionSnapshot snapshot{};
        snapshot.rack_id = runtime.rack_id();
        snapshot.generation = runtime.allocate_generation();
        snapshot.slots = std::move(working.slots);
        if (!safevst3::rack::write_rack_session_snapshot_atomic(
                runtime.path(), snapshot, error)) {
            std::cerr << "Rack session write"
                      << (reason ? std::string(" (") + reason + ")" : std::string{})
                      << ": " << (error.empty() ? "failed" : error) << '\n';
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        std::cerr << "Rack session persistence exception: " << exception.what() << '\n';
        return false;
    } catch (...) {
        std::cerr << "Rack session persistence exception\n";
        return false;
    }
}

int run_r3_4_persistent_product(const Options& options)
{
    Endpoint endpoint;
    if (!endpoint.open(options)) {
        std::cerr << "R3-4 Rack helper could not open transport\n";
        return 3;
    }
    if (endpoint.region->magic != safevst3::rack::kRackProtocolMagic ||
        endpoint.region->version != safevst3::rack::kRackProtocolVersion ||
        endpoint.region->sample_rate == 0 || endpoint.region->channels == 0 ||
        endpoint.region->channels > kMaxChannels) {
        InterlockedExchange(&endpoint.region->host_status,
                            static_cast<long>(RackHostStatus::Error));
        SetEvent(endpoint.ready);
        return 4;
    }

    clear_rack_breadcrumb(*endpoint.region);

    safevst3::rack::RackSessionRuntime session_runtime;
    std::string session_error;
    if (!session_runtime.open_from_environment(session_error) && !session_error.empty())
        std::cerr << "Rack session runtime: " << session_error << '\n';

    GenerationStore store;
    initialize_empty_generation_store(store);
    DynamicRackState state;
    MissingPresetStateStore missing_states;

    CatalogRuntime catalog_runtime;
    const std::filesystem::path cache = safevst3::rack::ui::rack_catalog_cache_path();
    if (!cache.empty()) {
        std::lock_guard lock(catalog_runtime.mutex);
        (void)catalog_runtime.catalog.load_cache(cache);
    }

    PresetRuntime preset_runtime;
    preset_runtime.library = safevst3::rack::rack_preset_library_path();
    {
        std::string preset_error;
        if (!preset_runtime.library.empty() &&
            !safevst3::rack::list_rack_presets(
                preset_runtime.library, preset_runtime.entries, preset_error) &&
            !preset_error.empty())
            std::cerr << "R3-4 preset library: " << preset_error << '\n';
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
            return preset_failed_ack(command, preset_runtime);
        });

    // Catalog/preset data has no VST controller thread affinity and may be
    // published before the helper is Ready. The working Rack snapshot is
    // deliberately published by the control worker after session restore.
    {
        std::lock_guard lock(catalog_runtime.mutex);
        editor.publish_catalog(catalog_runtime.catalog.snapshot());
    }
    editor.publish_presets(build_preset_ui_snapshot(preset_runtime));

    std::mutex bootstrap_mutex;
    std::condition_variable bootstrap_cv;
    bool bootstrap_complete = false;

    std::thread command_worker([&] {
        // P9 control-thread bootstrap: a restored VST3 controller must be
        // created, initialized, state-restored, later saved, and asked to
        // create its native editor from the same Rack control thread. Before
        // this fix session materialization ran on the helper main thread while
        // OpenVendorEditor ran here; strict vendor controllers then appeared
        // Ready/audio-active but rejected or ignored native GUI creation after
        // an OBS restart.
        const bool restored = restore_working_rack_session(
            session_runtime, state, missing_states, store, *endpoint.region);
        if (!restored)
            publish_dynamic_projection(
                *endpoint.region, state, store.generations[0].number);

        const std::uint32_t initial_index =
            store.published_index.load(std::memory_order_acquire);
        editor.publish_snapshot(build_dynamic_ui_snapshot(
            state, store.generations[initial_index].number));

        {
            std::lock_guard bootstrap_lock(bootstrap_mutex);
            bootstrap_complete = true;
        }
        bootstrap_cv.notify_one();

        while (!command_queue.stopping() &&
               InterlockedCompareExchange(
                   &endpoint.region->shutdown_requested, 0, 0) == 0) {
            vendor_editors.pump_messages();

            // OBS save/scene-collection serialization requests a fresh snapshot
            // of every VST3 component/controller state. This stays completely
            // off the realtime audio callback and is bounded by the parent.
            if (session_runtime.take_save_request()) {
                if (persist_working_rack_session(
                        session_runtime, state, missing_states, "OBS save"))
                    session_runtime.signal_save_complete();
            }

            RackPresetUiCommand preset_command{};
            if (preset_queue.try_pop(preset_command)) {
                RackPresetUiAck preset_ack{};
                if (preset_replay.lookup(preset_command, preset_ack)) {
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
                        editor.publish_snapshot(
                            build_dynamic_ui_snapshot(state, rack_generation));
                        (void)persist_working_rack_session(
                            session_runtime, state, missing_states, "preset load");
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

            bool topology_changed = false;
            ack = execute_dynamic_command(command, state, store, *endpoint.region,
                                          catalog_runtime, editor, vendor_editors,
                                          retired, topology_changed);
            replay.remember(command, ack);
            editor.apply_ack(ack);
            if (ack.result == RackUiCommandResult::Accepted && topology_changed) {
                missing_states.prune(state);
                const std::uint32_t current =
                    store.published_index.load(std::memory_order_acquire);
                editor.publish_snapshot(build_dynamic_ui_snapshot(
                    state, store.generations[current].number));
                // Topology/bypass changes are durable immediately, so a crash
                // after Add/Move/Replace/Remove cannot return to an empty Rack.
                (void)persist_working_rack_session(
                    session_runtime, state, missing_states, "topology change");
            }
        }
        vendor_editors.close_all();
        vendor_editors.pump_messages();
    });

    // Do not advertise Ready or start DSP until the control thread has rebuilt
    // the persisted Rack. This preserves the old startup guarantee while also
    // preserving VST3 controller/native-editor thread ownership.
    {
        std::unique_lock bootstrap_lock(bootstrap_mutex);
        bootstrap_cv.wait(bootstrap_lock, [&] { return bootstrap_complete; });
    }

    InterlockedExchange(&endpoint.region->host_status,
                        static_cast<long>(RackHostStatus::Ready));
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

    InterlockedExchange(&endpoint.region->host_status,
                        static_cast<long>(RackHostStatus::ShuttingDown));
    editor.shutdown();
    reap_retired_plugins(store, retired);
    close_dynamic_state(state, retired);
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    Options options;
    if (!parse_options(argc, argv, options)) {
        std::cerr << "usage: obs-safe-vst3-rack-host --mapping <name> --request-event <name> "
                     "--response-event <name> --ready-event <name> "
                     "[--plugin-a <vst3> --plugin-b <vst3>] "
                     "[--topology-request-event <name> --topology-response-event <name> --plugin-c <vst3>] "
                     "[--ui-open-event <name>]\n";
        return 2;
    }

    if (options.fixture_plugins_enabled)
        return safevst3_r3_1_legacy_run(options);
    return run_r3_4_persistent_product(options);
}

#else
int main() { return 0; }
#endif
