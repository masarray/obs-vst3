#ifdef _WIN32

// Keep the proven R1/R2/R3-0/R3-1 fixture runtime byte-for-byte available.
// R3-2 routes fixture A/B/C launches through that exact implementation and
// adds the dynamic production empty-Rack path below.
#define run safevst3_r3_1_legacy_run
#define wmain safevst3_r3_1_legacy_wmain
#include "rack/main.cpp"
#undef wmain
#undef run

#include "rack/rack_plugin_catalog.hpp"
#include "rack/rack_slot_workflow.hpp"
#include "rack/rack_vendor_editor_manager.hpp"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using safevst3::rack::ui::PluginCatalogSnapshot;
using safevst3::rack::ui::RackPluginCatalog;
using safevst3::rack::ui::RackPluginCatalogRecord;
using safevst3::rack::ui::RackUiCommandReplayGuard;
using safevst3::rack::ui::RackUiCommandType;
using safevst3::rack::ui::RackVendorEditorManager;

constexpr RackSlotId kDynamicSlotIdBase = 0x1000000000000000ull;
constexpr std::size_t kDynamicCommandQueueCapacity = 16;
constexpr auto kVendorEditorPumpInterval = std::chrono::milliseconds(16);

struct DynamicSlot {
    RackSlotId id = 0;
    std::unique_ptr<safevst3::HostedPlugin> plugin;
    std::string name;
    std::string vendor;
    std::string path;
    std::string class_id;
    bool bypass = false;
    RackUiSlotHealth health = RackUiSlotHealth::Ready;
    std::uint32_t latency_samples = 0;
};

struct DynamicRackState {
    std::array<DynamicSlot, safevst3::rack::kRackMaxSlots> slots{};
    std::uint32_t slot_count = 0;
    RackSlotId next_slot_id = kDynamicSlotIdBase;
};

struct RetiredDynamicPlugin {
    std::unique_ptr<safevst3::HostedPlugin> plugin;
    std::uint32_t generation_index = 0;
};

struct CommitReservation {
    std::uint32_t current_index = 0;
    std::uint32_t next_index = 0;
    std::uint64_t generation = 0;
};

class DynamicCommandQueue {
public:
    bool push(const RackUiCommand& command) noexcept
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || count_ >= commands_.size())
            return false;
        commands_[(head_ + count_) % commands_.size()] = command;
        ++count_;
        cv_.notify_one();
        return true;
    }

    bool pop(RackUiCommand& command) noexcept
    {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, kVendorEditorPumpInterval, [&] {
            return stopping_ || count_ != 0;
        });
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
        cv_.notify_all();
    }

    bool stopping() const noexcept
    {
        std::lock_guard lock(mutex_);
        return stopping_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::array<RackUiCommand, kDynamicCommandQueueCapacity> commands_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    bool stopping_ = false;
};

struct CatalogRuntime {
    std::mutex mutex;
    RackPluginCatalog catalog;
    bool scanning = false;
    std::jthread scanner_thread;
};

std::uint32_t find_dynamic_slot(const DynamicRackState& state, RackSlotId id) noexcept
{
    for (std::uint32_t i = 0; i < state.slot_count; ++i) {
        if (state.slots[i].id == id)
            return i;
    }
    return state.slot_count;
}

void close_plugin(std::unique_ptr<safevst3::HostedPlugin>& plugin) noexcept
{
    if (!plugin)
        return;
    plugin->close();
    plugin.reset();
}

void reap_retired_plugins(GenerationStore& store,
                          std::vector<RetiredDynamicPlugin>& retired) noexcept
{
    auto it = retired.begin();
    while (it != retired.end()) {
        if (store.readers[it->generation_index].load(std::memory_order_acquire) == 0) {
            close_plugin(it->plugin);
            it = retired.erase(it);
        } else {
            ++it;
        }
    }
}

bool reserve_commit(GenerationStore& store, RackSharedAudioRegion& region,
                    CommitReservation& reservation) noexcept
{
    reservation.current_index = store.published_index.load(std::memory_order_acquire);
    reservation.next_index = reservation.current_index ^ 1u;
    const std::uint64_t current_generation = store.generations[reservation.current_index].number;
    if (current_generation == 0 || current_generation == std::numeric_limits<std::uint64_t>::max())
        return false;
    reservation.generation = current_generation + 1;
    return wait_until_generation_unreachable(store, reservation.next_index, region);
}

void build_active_generation(RackChainGeneration& generation,
                             const DynamicRackState& state,
                             std::uint64_t generation_number) noexcept
{
    generation = RackChainGeneration{};
    generation.number = generation_number;
    for (std::uint32_t i = 0; i < state.slot_count; ++i) {
        const DynamicSlot& slot = state.slots[i];
        if (slot.bypass || !slot.plugin ||
            slot.health == RackUiSlotHealth::Missing ||
            slot.health == RackUiSlotHealth::Quarantined)
            continue;
        generation.slots[generation.slot_count++] = {
            slot.id, slot.plugin.get(), slot.latency_samples};
    }
}

void publish_dynamic_projection(RackSharedAudioRegion& region,
                                const DynamicRackState& state,
                                std::uint64_t generation) noexcept
{
    region.committed_slot_count = state.slot_count;
    for (std::uint32_t i = 0; i < safevst3::rack::kRackMaxSlots; ++i)
        region.committed_slot_ids[i] = i < state.slot_count ? state.slots[i].id : 0;
    MemoryBarrier();
    write_shared_generation(region.committed_chain_generation, generation);
    MemoryBarrier();
}

void publish_dynamic_generation(GenerationStore& store, RackSharedAudioRegion& region,
                                const DynamicRackState& state,
                                const CommitReservation& reservation) noexcept
{
    RackChainGeneration& next = store.generations[reservation.next_index];
    build_active_generation(next, state, reservation.generation);
    store.published_index.store(reservation.next_index, std::memory_order_release);
    publish_dynamic_projection(region, state, reservation.generation);
}

RackUiSnapshot build_dynamic_ui_snapshot(const DynamicRackState& state,
                                         std::uint64_t generation) noexcept
{
    RackUiSnapshot snapshot{};
    snapshot.generation = generation;
    snapshot.slot_count = state.slot_count;
    copy_ui_text(snapshot.rack_name, "VST3 Rack");
    for (std::uint32_t i = 0; i < state.slot_count; ++i) {
        const DynamicSlot& source = state.slots[i];
        auto& destination = snapshot.slots[i];
        destination.slot_id = source.id;
        destination.latency_samples = source.latency_samples;
        destination.bypass = source.bypass;
        destination.editor_available = source.plugin && source.plugin->edit_controller();
        destination.health = source.bypass ? RackUiSlotHealth::Bypassed : source.health;
        copy_ui_text(destination.plugin_name, source.name);
        copy_ui_text(destination.vendor, source.vendor);
        if (!source.bypass && source.plugin && source.health == RackUiSlotHealth::Ready)
            snapshot.total_latency_samples += source.latency_samples;
    }
    return snapshot;
}

std::optional<RackPluginCatalogRecord> resolve_catalog_record(
    CatalogRuntime& runtime, std::uint64_t generation,
    std::uint64_t entry_id) noexcept
{
    std::lock_guard lock(runtime.mutex);
    const RackPluginCatalogRecord* record = runtime.catalog.resolve(generation, entry_id);
    if (!record)
        return std::nullopt;
    return *record;
}

bool start_catalog_refresh(CatalogRuntime& runtime, RackEditorWindow& editor) noexcept
{
    {
        std::lock_guard lock(runtime.mutex);
        if (runtime.scanning)
            return false;
        runtime.scanning = true;
        runtime.catalog.set_scanning(true);
        editor.publish_catalog(runtime.catalog.snapshot());
    }

    if (runtime.scanner_thread.joinable())
        runtime.scanner_thread = std::jthread{};

    runtime.scanner_thread = std::jthread([&runtime, &editor](std::stop_token stop) {
        const std::filesystem::path scanner = safevst3::rack::ui::rack_scanner_path();
        const std::filesystem::path cache = safevst3::rack::ui::rack_catalog_cache_path();
        const bool scanned = safevst3::rack::ui::run_rack_scanner(scanner, cache, stop);

        PluginCatalogSnapshot publish{};
        {
            std::lock_guard lock(runtime.mutex);
            if (scanned && !stop.stop_requested())
                (void)runtime.catalog.load_cache(cache);
            runtime.catalog.set_scanning(false);
            runtime.scanning = false;
            publish = runtime.catalog.snapshot();
        }
        if (!stop.stop_requested())
            editor.publish_catalog(publish);
    });
    return true;
}

RackUiCommandAck failed_ack(const RackUiCommand& command,
                            const GenerationStore& store) noexcept
{
    RackUiCommandAck ack{};
    ack.command_id = command.command_id;
    ack.result = RackUiCommandResult::Failed;
    const std::uint32_t index = store.published_index.load(std::memory_order_acquire);
    ack.committed_generation = store.generations[index].number;
    return ack;
}

RackUiCommandAck rejected_ack(const RackUiCommand& command,
                              const GenerationStore& store) noexcept
{
    RackUiCommandAck ack = failed_ack(command, store);
    ack.result = RackUiCommandResult::Rejected;
    return ack;
}

RackUiCommandAck accepted_ack(const RackUiCommand& command,
                              std::uint64_t generation) noexcept
{
    RackUiCommandAck ack{};
    ack.command_id = command.command_id;
    ack.result = RackUiCommandResult::Accepted;
    ack.committed_generation = generation;
    return ack;
}

bool open_catalog_plugin(const RackPluginCatalogRecord& record,
                         RackSharedAudioRegion& region,
                         std::unique_ptr<safevst3::HostedPlugin>& plugin) noexcept
{
    auto candidate = std::make_unique<safevst3::HostedPlugin>();
    std::string error;
    if (!candidate->open(record.path, record.class_id, region.sample_rate,
                         region.channels, nullptr, error)) {
        std::cerr << "R3-2 plug-in open failed: " << error << '\n';
        return false;
    }
    plugin = std::move(candidate);
    return true;
}

RackUiCommandAck execute_dynamic_command(
    const RackUiCommand& command, DynamicRackState& state,
    GenerationStore& store, RackSharedAudioRegion& region,
    CatalogRuntime& catalog_runtime, RackEditorWindow& editor,
    RackVendorEditorManager& vendor_editors,
    std::vector<RetiredDynamicPlugin>& retired,
    bool& topology_changed) noexcept
{
    topology_changed = false;
    reap_retired_plugins(store, retired);

    const std::uint32_t published_index = store.published_index.load(std::memory_order_acquire);
    const std::uint64_t published_generation = store.generations[published_index].number;

    if (command.type == RackUiCommandType::RefreshCatalog) {
        return start_catalog_refresh(catalog_runtime, editor)
                   ? accepted_ack(command, published_generation)
                   : rejected_ack(command, store);
    }

    if (command.type == RackUiCommandType::OpenVendorEditor) {
        const std::uint32_t index = find_dynamic_slot(state, command.slot_id);
        if (index >= state.slot_count)
            return rejected_ack(command, store);
        DynamicSlot& slot = state.slots[index];
        if (!slot.plugin || !slot.plugin->edit_controller() ||
            (slot.health != RackUiSlotHealth::Ready &&
             slot.health != RackUiSlotHealth::Bypassed))
            return rejected_ack(command, store);
        std::string error;
        if (!vendor_editors.open(slot.id, *slot.plugin, slot.name, error)) {
            if (!error.empty())
                std::cerr << "R3-3 vendor editor: " << error << '\n';
            return failed_ack(command, store);
        }
        return accepted_ack(command, published_generation);
    }

    CommitReservation reservation{};
    switch (command.type) {
    case RackUiCommandType::AddSlot: {
        if (state.slot_count >= safevst3::rack::kRackMaxSlots ||
            command.target_index > state.slot_count)
            return rejected_ack(command, store);
        const auto record = resolve_catalog_record(
            catalog_runtime, command.catalog_generation, command.catalog_entry_id);
        if (!record)
            return rejected_ack(command, store);

        std::unique_ptr<safevst3::HostedPlugin> plugin;
        if (!open_catalog_plugin(*record, region, plugin))
            return failed_ack(command, store);
        if (!reserve_commit(store, region, reservation)) {
            close_plugin(plugin);
            return failed_ack(command, store);
        }
        if (state.next_slot_id == 0 ||
            state.next_slot_id == safevst3::rack::kRackSlotIdA ||
            state.next_slot_id == safevst3::rack::kRackSlotIdB ||
            state.next_slot_id == safevst3::rack::kRackSlotIdC) {
            close_plugin(plugin);
            return failed_ack(command, store);
        }

        for (std::uint32_t i = state.slot_count; i > command.target_index; --i)
            state.slots[i] = std::move(state.slots[i - 1]);
        DynamicSlot added{};
        added.id = state.next_slot_id++;
        added.name = record->name;
        added.vendor = record->vendor;
        added.path = record->path;
        added.class_id = record->class_id;
        added.latency_samples = plugin->latency_samples();
        added.plugin = std::move(plugin);
        state.slots[command.target_index] = std::move(added);
        ++state.slot_count;
        publish_dynamic_generation(store, region, state, reservation);
        topology_changed = true;
        return accepted_ack(command, reservation.generation);
    }

    case RackUiCommandType::MoveSlot: {
        const std::uint32_t source = find_dynamic_slot(state, command.slot_id);
        if (source >= state.slot_count || command.target_index >= state.slot_count ||
            source == command.target_index)
            return rejected_ack(command, store);
        if (!reserve_commit(store, region, reservation))
            return failed_ack(command, store);

        DynamicSlot moving = std::move(state.slots[source]);
        if (source < command.target_index) {
            for (std::uint32_t i = source; i < command.target_index; ++i)
                state.slots[i] = std::move(state.slots[i + 1]);
        } else {
            for (std::uint32_t i = source; i > command.target_index; --i)
                state.slots[i] = std::move(state.slots[i - 1]);
        }
        state.slots[command.target_index] = std::move(moving);
        publish_dynamic_generation(store, region, state, reservation);
        topology_changed = true;
        return accepted_ack(command, reservation.generation);
    }

    case RackUiCommandType::SetBypass: {
        const std::uint32_t index = find_dynamic_slot(state, command.slot_id);
        if (index >= state.slot_count || state.slots[index].bypass == command.bypass ||
            !rack_ui_can_bypass(state.slots[index].health))
            return rejected_ack(command, store);
        if (!reserve_commit(store, region, reservation))
            return failed_ack(command, store);
        state.slots[index].bypass = command.bypass;
        publish_dynamic_generation(store, region, state, reservation);
        topology_changed = true;
        return accepted_ack(command, reservation.generation);
    }

    case RackUiCommandType::RemoveSlot: {
        const std::uint32_t index = find_dynamic_slot(state, command.slot_id);
        if (index >= state.slot_count || !rack_ui_can_remove(state.slots[index].health))
            return rejected_ack(command, store);
        if (!reserve_commit(store, region, reservation))
            return failed_ack(command, store);

        vendor_editors.close(command.slot_id);
        RetiredDynamicPlugin old{};
        old.plugin = std::move(state.slots[index].plugin);
        old.generation_index = reservation.current_index;
        for (std::uint32_t i = index; i + 1 < state.slot_count; ++i)
            state.slots[i] = std::move(state.slots[i + 1]);
        state.slots[state.slot_count - 1] = DynamicSlot{};
        --state.slot_count;
        publish_dynamic_generation(store, region, state, reservation);
        if (old.plugin)
            retired.push_back(std::move(old));
        reap_retired_plugins(store, retired);
        topology_changed = true;
        return accepted_ack(command, reservation.generation);
    }

    case RackUiCommandType::ReplaceSlot: {
        const std::uint32_t index = find_dynamic_slot(state, command.slot_id);
        if (index >= state.slot_count || !rack_ui_can_replace(state.slots[index].health))
            return rejected_ack(command, store);
        const auto record = resolve_catalog_record(
            catalog_runtime, command.catalog_generation, command.catalog_entry_id);
        if (!record)
            return rejected_ack(command, store);

        std::unique_ptr<safevst3::HostedPlugin> replacement;
        if (!open_catalog_plugin(*record, region, replacement))
            return failed_ack(command, store);
        if (!reserve_commit(store, region, reservation)) {
            close_plugin(replacement);
            return failed_ack(command, store);
        }

        const RackSlotId stable_id = state.slots[index].id;
        vendor_editors.close(stable_id);
        RetiredDynamicPlugin old{};
        old.plugin = std::move(state.slots[index].plugin);
        old.generation_index = reservation.current_index;
        const bool bypass = state.slots[index].bypass;
        DynamicSlot replaced{};
        replaced.id = stable_id;
        replaced.name = record->name;
        replaced.vendor = record->vendor;
        replaced.path = record->path;
        replaced.class_id = record->class_id;
        replaced.bypass = bypass;
        replaced.latency_samples = replacement->latency_samples();
        replaced.plugin = std::move(replacement);
        state.slots[index] = std::move(replaced);
        publish_dynamic_generation(store, region, state, reservation);
        if (old.plugin)
            retired.push_back(std::move(old));
        reap_retired_plugins(store, retired);
        topology_changed = true;
        return accepted_ack(command, reservation.generation);
    }

    case RackUiCommandType::None:
    case RackUiCommandType::OpenRack:
    case RackUiCommandType::RefreshCatalog:
    case RackUiCommandType::OpenVendorEditor:
        break;
    }
    return rejected_ack(command, store);
}

void close_dynamic_state(DynamicRackState& state,
                         std::vector<RetiredDynamicPlugin>& retired) noexcept
{
    for (std::uint32_t i = 0; i < state.slot_count; ++i)
        close_plugin(state.slots[i].plugin);
    state.slot_count = 0;
    for (auto& old : retired)
        close_plugin(old.plugin);
    retired.clear();
}

int run_r3_2_product(const Options& options)
{
    Endpoint endpoint;
    if (!endpoint.open(options)) {
        std::cerr << "R3-3 Rack helper could not open transport\n";
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
    publish_dynamic_projection(*endpoint.region, state, store.generations[0].number);

    CatalogRuntime catalog_runtime;
    const std::filesystem::path cache = safevst3::rack::ui::rack_catalog_cache_path();
    if (!cache.empty()) {
        std::lock_guard lock(catalog_runtime.mutex);
        (void)catalog_runtime.catalog.load_cache(cache);
    }

    DynamicCommandQueue command_queue;
    std::vector<RetiredDynamicPlugin> retired;
    RackUiCommandReplayGuard replay;
    RackVendorEditorManager vendor_editors;

    RackEditorWindow editor([&](const RackUiCommand& command) {
        if (command_queue.push(command))
            return RackUiCommandAck{};
        return failed_ack(command, store);
    });
    editor.publish_snapshot(build_dynamic_ui_snapshot(state, store.generations[0].number));
    {
        std::lock_guard lock(catalog_runtime.mutex);
        editor.publish_catalog(catalog_runtime.catalog.snapshot());
    }

    std::thread command_worker([&] {
        while (!command_queue.stopping() &&
               InterlockedCompareExchange(&endpoint.region->shutdown_requested, 0, 0) == 0) {
            vendor_editors.pump_messages();
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

    // Every deterministic fixture launch stays on the previously qualified
    // R1/R2/R3 implementation. Only the production empty-Rack path owns the
    // dynamic slot/browser/vendor-editor control plane.
    if (options.fixture_plugins_enabled)
        return safevst3_r3_1_legacy_run(options);
    return run_r3_2_product(options);
}

#else
int main() { return 0; }
#endif
