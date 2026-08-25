#include "common/reload_component_transaction.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "reload component transaction failure: " << message << '\n';
    std::exit(1);
}

enum class Event {
    Pause,
    Reconcile,
    Capture,
    CloseEditor,
    Recreate,
    Restore,
    ReconcileRestored,
    RegenerateRuntime,
    Republish,
    Resume,
    Commit,
    Recovery,
};

class FakeTarget final : public safevst3::ReloadComponentTarget {
public:
    safevst3::ReloadComponentStep fail = safevst3::ReloadComponentStep::None;
    std::vector<Event> events;
    unsigned recovery_count = 0;
    bool destructive_reload_started = false;
    bool committed = false;

    bool reload_pause_dsp() noexcept override
    {
        events.push_back(Event::Pause);
        return fail != safevst3::ReloadComponentStep::PauseDsp;
    }

    bool reload_reconcile_pending() noexcept override
    {
        events.push_back(Event::Reconcile);
        return fail != safevst3::ReloadComponentStep::ReconcilePending;
    }

    bool reload_capture_state() noexcept override
    {
        events.push_back(Event::Capture);
        return fail != safevst3::ReloadComponentStep::CaptureState;
    }

    bool reload_close_editor() noexcept override
    {
        events.push_back(Event::CloseEditor);
        return fail != safevst3::ReloadComponentStep::CloseEditor;
    }

    bool reload_recreate_plugin() noexcept override
    {
        events.push_back(Event::Recreate);
        destructive_reload_started = true;
        return fail != safevst3::ReloadComponentStep::RecreatePlugin;
    }

    bool reload_restore_state() noexcept override
    {
        events.push_back(Event::Restore);
        return fail != safevst3::ReloadComponentStep::RestoreState;
    }

    bool reload_reconcile_restored_state() noexcept override
    {
        events.push_back(Event::ReconcileRestored);
        return fail != safevst3::ReloadComponentStep::ReconcileRestoredState;
    }

    bool reload_regenerate_runtime() noexcept override
    {
        events.push_back(Event::RegenerateRuntime);
        return fail != safevst3::ReloadComponentStep::RegenerateRuntime;
    }

    bool reload_republish_runtime() noexcept override
    {
        events.push_back(Event::Republish);
        return fail != safevst3::ReloadComponentStep::RepublishRuntime;
    }

    bool reload_resume_dsp() noexcept override
    {
        events.push_back(Event::Resume);
        return fail != safevst3::ReloadComponentStep::ResumeDsp;
    }

    void reload_commit() noexcept override
    {
        events.push_back(Event::Commit);
        committed = true;
    }

    void reload_request_recovery() noexcept override
    {
        events.push_back(Event::Recovery);
        ++recovery_count;
    }
};

} // namespace

int main()
{
    using namespace safevst3;

    FakeTarget success;
    const auto success_result = coordinate_reload_component(success);
    const std::array expected{
        Event::Pause,
        Event::Reconcile,
        Event::Capture,
        Event::CloseEditor,
        Event::Recreate,
        Event::Restore,
        Event::ReconcileRestored,
        Event::RegenerateRuntime,
        Event::Republish,
        Event::Resume,
        Event::Commit,
    };
    require(success_result.completed, "successful full reload must complete");
    require(success.recovery_count == 0, "successful full reload must not request recovery");
    require(success.events.size() == expected.size(), "successful reload ordering length changed");
    for (std::size_t i = 0; i < expected.size(); ++i)
        require(success.events[i] == expected[i], "successful reload ordering changed");
    require(success.committed, "successful full reload must commit only after DSP resumes");

    const std::array failures{
        ReloadComponentStep::PauseDsp,
        ReloadComponentStep::ReconcilePending,
        ReloadComponentStep::CaptureState,
        ReloadComponentStep::CloseEditor,
        ReloadComponentStep::RecreatePlugin,
        ReloadComponentStep::RestoreState,
        ReloadComponentStep::ReconcileRestoredState,
        ReloadComponentStep::RegenerateRuntime,
        ReloadComponentStep::RepublishRuntime,
        ReloadComponentStep::ResumeDsp,
    };

    for (const auto step : failures) {
        FakeTarget target;
        target.fail = step;
        const auto result = coordinate_reload_component(target);
        require(!result.completed && result.failed_step == step,
                "failed reload frontier must remain diagnosable");
        require(target.recovery_count == 1,
                "each failed reload frontier must request recovery exactly once");
        require(!target.committed, "failed reload transaction must never commit");
        require(!target.events.empty() && target.events.back() == Event::Recovery,
                "recovery must be the final action after reload failure");
        if (step == ReloadComponentStep::PauseDsp ||
            step == ReloadComponentStep::ReconcilePending ||
            step == ReloadComponentStep::CaptureState ||
            step == ReloadComponentStep::CloseEditor) {
            require(!target.destructive_reload_started,
                    "preflight failure must not destroy the current plug-in instance");
        }
    }

    std::cout << "full reload component transaction ok\n";
    return 0;
}
