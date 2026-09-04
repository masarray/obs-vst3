#ifdef _WIN32

// Preserve the exact R3-2/R3-3 product implementation as a callable baseline
// in this translation unit. R3-4 only replaces the production empty-Rack entry
// path; deterministic fixture launches remain on the previously qualified path.
#define run_r3_2_product safevst3_r3_2_legacy_product
#define wmain safevst3_r3_2_legacy_wmain
#include "rack/main_r3_2.cpp"
#undef wmain
#undef run_r3_2_product

#include "rack/rack_preset_management.hpp"
#include "rack/rack_preset_ui_contract.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

using safevst3::rack::RackPersistedSlotHealth;
using safevst3::rack::RackPreset;
using safevst3::rack::RackPresetId;
using safevst3::rack::RackPresetLoadSource;
using safevst3::rack::RackPresetSummary;
using safevst3::rack::RackSessionSlotSnapshot;
using safevst3::rack::ui::RackPresetUiAck;
using safevst3::rack::ui::RackPresetUiCommand;
using safevst3::rack::ui::RackPresetUiCommandResult;
using safevst3::rack::ui::RackPresetUiCommandType;
using safevst3::rack::ui::RackPresetUiSnapshot;

constexpr std::size_t kPresetCommandQueueCapacity = 16;
constexpr std::size_t kPresetReplayCapacity = 32;

class PresetCommandQueue {
public:
    bool push(const RackPresetUiCommand& command) noexcept
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || count_ >= commands_.size())
            return false;
        commands_[(head_ + count_) % commands_.size()] = command;
        ++count_;
        return true;
    }

    bool try_pop(RackPresetUiCommand& command) noexcept
    {
        std::lock_guard lock(mutex_);
        if (count_ == 0)
            return false;
        command = commands_[head_];
        head_ = (head_ + 1) % commands_.size();
        --count_;
        return true;
    }

    void stop() noexcept
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }

private:
    std::mutex mutex_;
    std::array<RackPresetUiCommand, kPresetCommandQueueCapacity> commands_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    bool stopping_ = false;
};

class PresetReplayGuard {
public:
    bool lookup(const RackPresetUiCommand& command, RackPresetUiAck& ack) const noexcept
    {
        if (command.command_id == 0)
            return false;
        for (const auto& entry : entries_) {
            if (entry.command_id == command.command_id && entry.command_id != 0) {
                ack = entry.ack;
                return true;
            }
        }
        return false;
    }

    void remember(const RackPresetUiCommand& command, const RackPresetUiAck& ack) noexcept
    {
        if (command.command_id == 0 || ack.command_id != command.command_id ||
            ack.result == RackPresetUiCommandResult::Idle)
            return;
        for (auto& entry : entries_) {
            if (entry.command_id == command.command_id) {
                entry.ack = ack;
                return;
            }
        }
        entries_[next_] = {command.command_id, ack};
        next_ = (next_ + 1) % entries_.size();
    }

private:
    struct Entry {
        std::uint64_t command_id = 0;
        RackPresetUiAck ack{};
    };
    std::array<Entry, kPresetReplayCapacity> entries_{};
    std::size_t next_ = 0;
};

struct MissingPresetStateStore {
    struct Entry {
        bool occupied = false;
        RackSessionSlotSnapshot snapshot{};
    };

    const RackSessionSlotSnapshot* find(RackSlotId slot_id) const noexcept
    {
        for (const auto& entry : entries) {
            if (entry.occupied && entry.snapshot.slot_id == slot_id)
                return &entry.snapshot;
        }
        return nullptr;
    }

    bool put(const RackSessionSlotSnapshot& snapshot)
    {
        for (auto& entry : entries) {
            if (entry.occupied && entry.snapshot.slot_id == snapshot.slot_id) {
                entry.snapshot = snapshot;
                return true;
            }
        }
        for (auto& entry : entries) {
            if (!entry.occupied) {
                entry.occupied = true;
                entry.snapshot = snapshot;
                return true;
            }
        }
        return false;
    }

    void prune(const DynamicRackState& state) noexcept
    {
        for (auto& entry : entries) {
            if (!entry.occupied)
                continue;
            const std::uint32_t index = find_dynamic_slot(state, entry.snapshot.slot_id);
            if (index >= state.slot_count || state.slots[index].plugin)
                entry = {};
        }
    }

    std::array<Entry, safevst3::rack::kRackMaxSlots> entries{};
};

struct PresetRuntime {
    std::filesystem::path library;
    std::vector<RackPresetSummary> entries;
    std::uint64_t generation = 1;
    RackPresetId active_id{};
    std::string active_name;
};

bool preset_id_equal(const RackPresetId& left, const RackPresetId& right) noexcept
{
    return left == right;
}

bool preset_id_nonzero(const RackPresetId& id) noexcept
{
    return safevst3::rack::ui::rack_preset_id_nonzero(id);
}

std::string preset_command_name(const RackPresetUiCommand& command)
{
    return std::string(safevst3::rack::ui::rack_preset_ui_name_view(command.name));
}

void sort_preset_entries(std::vector<RackPresetSummary>& entries)
{
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        std::string left_name = left.name;
        std::string right_name = right.name;
        std::transform(left_name.begin(), left_name.end(), left_name.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        std::transform(right_name.begin(), right_name.end(), right_name.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (left_name != right_name)
            return left_name < right_name;
        return left.preset_id < right.preset_id;
    });
}

RackPresetSummary* find_preset_summary(PresetRuntime& runtime,
                                       const RackPresetId& preset_id) noexcept
{
    for (auto& entry : runtime.entries) {
        if (entry.preset_id == preset_id)
            return &entry;
    }
    return nullptr;
}

const RackPresetSummary* find_preset_summary(const PresetRuntime& runtime,
                                             const RackPresetId& preset_id) noexcept
{
    for (const auto& entry : runtime.entries) {
        if (entry.preset_id == preset_id)
            return &entry;
    }
    return nullptr;
}

bool advance_preset_generation(PresetRuntime& runtime) noexcept
{
    if (runtime.generation == std::numeric_limits<std::uint64_t>::max())
        return false;
    ++runtime.generation;
    return true;
}

RackPresetUiSnapshot build_preset_ui_snapshot(const PresetRuntime& runtime) noexcept
{
    RackPresetUiSnapshot snapshot{};
    snapshot.generation = runtime.generation;
    snapshot.entry_count = static_cast<std::uint32_t>(
        std::min<std::size_t>(runtime.entries.size(), snapshot.entries.size()));
    for (std::uint32_t index = 0; index < snapshot.entry_count; ++index) {
        snapshot.entries[index].preset_id = runtime.entries[index].preset_id;
        (void)safevst3::rack::ui::rack_preset_ui_copy_name(
            snapshot.entries[index].name, runtime.entries[index].name);
    }
    if (preset_id_nonzero(runtime.active_id) &&
        find_preset_summary(runtime, runtime.active_id)) {
        snapshot.active_preset_id = runtime.active_id;
        (void)safevst3::rack::ui::rack_preset_ui_copy_name(
            snapshot.active_preset_name, runtime.active_name);
    }
    return snapshot;
}

RackPresetUiAck preset_failed_ack(const RackPresetUiCommand& command,
                                  const PresetRuntime& runtime) noexcept
{
    RackPresetUiAck ack{};
    ack.command_id = command.command_id;
    ack.result = RackPresetUiCommandResult::Failed;
    ack.committed_generation = runtime.generation;
    ack.preset_id = command.preset_id;
    return ack;
}

RackPresetUiAck preset_rejected_ack(const RackPresetUiCommand& command,
                                    const PresetRuntime& runtime) noexcept
{
    RackPresetUiAck ack = preset_failed_ack(command, runtime);
    ack.result = RackPresetUiCommandResult::Rejected;
    return ack;
}

RackPresetUiAck preset_accepted_ack(const RackPresetUiCommand& command,
                                    const PresetRuntime& runtime,
                                    const RackPresetId& preset_id) noexcept
{
    RackPresetUiAck ack{};
    ack.command_id = command.command_id;
    ack.result = RackPresetUiCommandResult::Accepted;
    ack.committed_generation = runtime.generation;
    ack.preset_id = preset_id;
    return ack;
}

RackPersistedSlotHealth persisted_health_for(const DynamicSlot& slot) noexcept
{
    switch (slot.health) {
    case RackUiSlotHealth::Missing:
        return RackPersistedSlotHealth::Missing;
    case RackUiSlotHealth::Quarantined:
        return RackPersistedSlotHealth::Quarantined;
    case RackUiSlotHealth::NeedsAttention:
        return RackPersistedSlotHealth::Suspect;
    case RackUiSlotHealth::Loading:
    case RackUiSlotHealth::Recovering:
        return RackPersistedSlotHealth::Failed;
    case RackUiSlotHealth::Ready:
    case RackUiSlotHealth::Bypassed:
        return RackPersistedSlotHealth::Ready;
    }
    return RackPersistedSlotHealth::Ready;
}

bool capture_current_rack(const DynamicRackState& state,
                          const MissingPresetStateStore& missing_states,
                          RackPreset& preset,
                          std::string& error)
{
    preset.slots.clear();
    preset.slots.reserve(state.slot_count);
    for (std::uint32_t index = 0; index < state.slot_count; ++index) {
        const DynamicSlot& slot = state.slots[index];
        RackSessionSlotSnapshot captured{};
        if (slot.plugin) {
            if (!safevst3::rack::capture_rack_session_slot(
                    *slot.plugin, slot.id, slot.path, slot.bypass,
                    persisted_health_for(slot), captured, error))
                return false;
        } else {
            const RackSessionSlotSnapshot* preserved = missing_states.find(slot.id);
            if (!preserved) {
                error = "Missing Rack slot has no reusable persisted state";
                return false;
            }
            captured = *preserved;
            captured.bypass = slot.bypass;
            captured.health = persisted_health_for(slot);
        }
        preset.slots.push_back(std::move(captured));
    }
    return true;
}

std::string display_name_for_preset_slot(const RackSessionSlotSnapshot& slot)
{
    try {
        std::string result = std::filesystem::path(slot.plugin_path).stem().string();
        if (!result.empty())
            return result;
    } catch (...) {
    }
    return "VST3 Effect";
}

bool assign_next_dynamic_slot_id(DynamicRackState& state) noexcept
{
    RackSlotId candidate = kDynamicSlotIdBase;
    while (candidate != 0 && find_dynamic_slot(state, candidate) < state.slot_count)
        ++candidate;
    if (candidate == 0)
        return false;
    state.next_slot_id = candidate;
    return true;
}

void close_candidate_state(DynamicRackState& state) noexcept
{
    for (std::uint32_t index = 0; index < state.slot_count; ++index)
        close_plugin(state.slots[index].plugin);
    state.slot_count = 0;
}

bool materialize_preset_candidate(const RackPreset& preset,
                                  RackSharedAudioRegion& region,
                                  DynamicRackState& candidate,
                                  MissingPresetStateStore& missing_states,
                                  std::string& error)
{
    candidate = DynamicRackState{};
    missing_states = MissingPresetStateStore{};
    if (preset.slots.size() > safevst3::rack::kRackMaxSlots) {
        error = "Rack Preset exceeds the Rack slot limit";
        return false;
    }

    for (const RackSessionSlotSnapshot& source : preset.slots) {
        if (source.slot_id == 0 || find_dynamic_slot(candidate, source.slot_id) < candidate.slot_count) {
            error = "Rack Preset contains invalid or duplicate stable slot IDs";
            close_candidate_state(candidate);
            return false;
        }

        DynamicSlot slot{};
        slot.id = source.slot_id;
        slot.name = display_name_for_preset_slot(source);
        slot.path = source.plugin_path;
        slot.class_id = source.class_id;
        slot.bypass = source.bypass;

        const bool force_placeholder = source.health == RackPersistedSlotHealth::Quarantined;
        if (!force_placeholder) {
            RackPluginCatalogRecord record{};
            record.name = slot.name;
            record.path = source.plugin_path;
            record.class_id = source.class_id;
            std::unique_ptr<safevst3::HostedPlugin> plugin;
            std::string open_error;
            if (open_catalog_plugin(record, region, plugin) &&
                safevst3::rack::restore_rack_session_slot_state(*plugin, source, open_error)) {
                slot.latency_samples = plugin->latency_samples();
                slot.health = RackUiSlotHealth::Ready;
                slot.plugin = std::move(plugin);
            } else {
                if (plugin)
                    close_plugin(plugin);
                slot.health = RackUiSlotHealth::Missing;
            }
        } else {
            slot.health = RackUiSlotHealth::Quarantined;
        }

        if (!slot.plugin && !missing_states.put(source)) {
            error = "Rack Preset Missing placeholder store is full";
            close_candidate_state(candidate);
            return false;
        }
        candidate.slots[candidate.slot_count++] = std::move(slot);
    }

    if (!assign_next_dynamic_slot_id(candidate)) {
        error = "Rack Preset leaves no safe dynamic slot identity";
        close_candidate_state(candidate);
        return false;
    }
    return true;
}

void retire_current_plugins(DynamicRackState& state,
                            std::uint32_t generation_index,
                            std::vector<RetiredDynamicPlugin>& retired)
{
    for (std::uint32_t index = 0; index < state.slot_count; ++index) {
        if (!state.slots[index].plugin)
            continue;
        RetiredDynamicPlugin old{};
        old.plugin = std::move(state.slots[index].plugin);
        old.generation_index = generation_index;
        retired.push_back(std::move(old));
    }
}

bool load_preset_into_rack(const RackPreset& preset,
                           DynamicRackState& state,
                           MissingPresetStateStore& missing_states,
                           GenerationStore& store,
                           RackSharedAudioRegion& region,
                           RackVendorEditorManager& vendor_editors,
                           std::vector<RetiredDynamicPlugin>& retired,
                           std::string& error,
                           std::uint64_t& committed_generation)
{
    DynamicRackState candidate{};
    MissingPresetStateStore candidate_missing{};
    if (!materialize_preset_candidate(preset, region, candidate, candidate_missing, error))
        return false;

    CommitReservation reservation{};
    if (!reserve_commit(store, region, reservation)) {
        close_candidate_state(candidate);
        error = "Rack generation could not be reserved for preset load";
        return false;
    }

    vendor_editors.close_all();
    retire_current_plugins(state, reservation.current_index, retired);
    state = std::move(candidate);
    missing_states = std::move(candidate_missing);
    publish_dynamic_generation(store, region, state, reservation);
    reap_retired_plugins(store, retired);
    committed_generation = reservation.generation;
    return true;
}

RackPresetUiAck execute_preset_command(
    const RackPresetUiCommand& command,
    PresetRuntime& runtime,
    DynamicRackState& state,
    MissingPresetStateStore& missing_states,
    GenerationStore& store,
    RackSharedAudioRegion& region,
    RackVendorEditorManager& vendor_editors,
    std::vector<RetiredDynamicPlugin>& retired,
    bool& rack_changed,
    std::uint64_t& rack_generation) noexcept
{
    rack_changed = false;
    rack_generation = 0;
    if (command.command_id == 0)
        return preset_rejected_ack(command, runtime);

    try {
        std::string error;
        RackPresetId affected_id = command.preset_id;

        switch (command.type) {
        case RackPresetUiCommandType::SaveAs: {
            const std::string name = preset_command_name(command);
            if (!safevst3::rack::ui::rack_preset_ui_name_valid(name) ||
                runtime.entries.size() >= safevst3::rack::kRackPresetLibraryMaxEntries)
                return preset_rejected_ack(command, runtime);

            RackPreset preset{};
            if (!safevst3::rack::generate_rack_preset_id(preset.preset_id, error))
                return preset_failed_ack(command, runtime);
            preset.name = name;
            if (!capture_current_rack(state, missing_states, preset, error) ||
                !safevst3::rack::write_rack_preset_atomic(runtime.library, preset, error)) {
                if (!error.empty())
                    std::cerr << "R3-4 Save As Preset: " << error << '\n';
                return preset_failed_ack(command, runtime);
            }
            affected_id = preset.preset_id;
            runtime.entries.push_back({preset.preset_id, preset.name});
            sort_preset_entries(runtime.entries);
            runtime.active_id = preset.preset_id;
            runtime.active_name = preset.name;
            break;
        }

        case RackPresetUiCommandType::Load: {
            const RackPresetSummary* summary = find_preset_summary(runtime, command.preset_id);
            if (!summary)
                return preset_rejected_ack(command, runtime);
            RackPreset preset{};
            RackPresetLoadSource source = RackPresetLoadSource::None;
            if (!safevst3::rack::load_rack_preset_lkg(
                    runtime.library, command.preset_id, preset, source, error)) {
                if (!error.empty())
                    std::cerr << "R3-4 Load Preset: " << error << '\n';
                return preset_failed_ack(command, runtime);
            }
            if (!load_preset_into_rack(preset, state, missing_states, store, region,
                                       vendor_editors, retired, error, rack_generation)) {
                if (!error.empty())
                    std::cerr << "R3-4 Load Preset: " << error << '\n';
                return preset_failed_ack(command, runtime);
            }
            runtime.active_id = preset.preset_id;
            runtime.active_name = preset.name;
            rack_changed = true;
            break;
        }

        case RackPresetUiCommandType::Rename: {
            RackPresetSummary* summary = find_preset_summary(runtime, command.preset_id);
            const std::string name = preset_command_name(command);
            if (!summary || !safevst3::rack::ui::rack_preset_ui_name_valid(name))
                return preset_rejected_ack(command, runtime);
            if (!safevst3::rack::rename_rack_preset_atomic(
                    runtime.library, command.preset_id, name, error)) {
                if (!error.empty())
                    std::cerr << "R3-4 Rename Preset: " << error << '\n';
                return preset_failed_ack(command, runtime);
            }
            summary->name = name;
            if (preset_id_equal(runtime.active_id, command.preset_id))
                runtime.active_name = name;
            sort_preset_entries(runtime.entries);
            break;
        }

        case RackPresetUiCommandType::Delete: {
            if (!find_preset_summary(runtime, command.preset_id))
                return preset_rejected_ack(command, runtime);
            if (!safevst3::rack::delete_rack_preset(runtime.library, command.preset_id, error)) {
                if (!error.empty())
                    std::cerr << "R3-4 Delete Preset: " << error << '\n';
                return preset_failed_ack(command, runtime);
            }
            runtime.entries.erase(
                std::remove_if(runtime.entries.begin(), runtime.entries.end(),
                               [&](const RackPresetSummary& entry) {
                                   return entry.preset_id == command.preset_id;
                               }),
                runtime.entries.end());
            if (preset_id_equal(runtime.active_id, command.preset_id)) {
                runtime.active_id = {};
                runtime.active_name.clear();
            }
            break;
        }

        case RackPresetUiCommandType::Update: {
            const RackPresetSummary* summary = find_preset_summary(runtime, command.preset_id);
            if (!summary)
                return preset_rejected_ack(command, runtime);
            RackPreset preset{};
            preset.preset_id = command.preset_id;
            preset.name = summary->name;
            if (!capture_current_rack(state, missing_states, preset, error) ||
                !safevst3::rack::write_rack_preset_atomic(runtime.library, preset, error)) {
                if (!error.empty())
                    std::cerr << "R3-4 Update Preset: " << error << '\n';
                return preset_failed_ack(command, runtime);
            }
            runtime.active_id = preset.preset_id;
            runtime.active_name = preset.name;
            break;
        }

        case RackPresetUiCommandType::None:
            return preset_rejected_ack(command, runtime);
        }

        if (!advance_preset_generation(runtime))
            return preset_failed_ack(command, runtime);
        return preset_accepted_ack(command, runtime, affected_id);
    } catch (const std::exception& exception) {
        std::cerr << "R3-4 preset command exception: " << exception.what() << '\n';
        return preset_failed_ack(command, runtime);
    } catch (...) {
        std::cerr << "R3-4 preset command exception\n";
        return preset_failed_ack(command, runtime);
    }
}

int run_r3_4_product(const Options& options)
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

    editor.publish_snapshot(build_dynamic_ui_snapshot(state, store.generations[0].number));
    {
        std::lock_guard lock(catalog_runtime.mutex);
        editor.publish_catalog(catalog_runtime.catalog.snapshot());
    }
    editor.publish_presets(build_preset_ui_snapshot(preset_runtime));

    std::thread command_worker([&] {
        while (!command_queue.stopping() &&
               InterlockedCompareExchange(&endpoint.region->shutdown_requested, 0, 0) == 0) {
            vendor_editors.pump_messages();

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
                    if (rack_changed)
                        editor.publish_snapshot(build_dynamic_ui_snapshot(state, rack_generation));
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
                const std::uint32_t current = store.published_index.load(std::memory_order_acquire);
                editor.publish_snapshot(build_dynamic_ui_snapshot(
                    state, store.generations[current].number));
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
    return run_r3_4_product(options);
}

#else
int main() { return 0; }
#endif
