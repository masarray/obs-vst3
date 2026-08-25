#include "common/channel_adapter.hpp"
#include "common/io_restart_transaction.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void require(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "io restart transaction failure: " << message << '\n';
    std::exit(1);
}

enum class Event { Stop, Deactivate, InspectRequested, Confirm, InspectConfirmed, Rebuild, Activate, Latency, Start, Commit, Recovery };

class FakeTarget final : public safevst3::IoRestartTarget {
public:
    safevst3::IoRestartStep failure = safevst3::IoRestartStep::None;
    safevst3::IoLayout requested{2,1};
    safevst3::IoLayout confirmed{2,1};
    bool confirmation = true;
    std::vector<Event> events;
    unsigned recovery = 0;
    bool set_processing(bool enabled) noexcept override { events.push_back(enabled?Event::Start:Event::Stop); return failure != (enabled?safevst3::IoRestartStep::StartProcessing:safevst3::IoRestartStep::StopProcessing); }
    bool set_active(bool enabled) noexcept override { events.push_back(enabled?Event::Activate:Event::Deactivate); return failure != (enabled?safevst3::IoRestartStep::Activate:safevst3::IoRestartStep::Deactivate); }
    bool inspect_requested_io(safevst3::IoLayout& out) noexcept override { events.push_back(Event::InspectRequested); out=requested; return failure!=safevst3::IoRestartStep::InspectRequested; }
    bool confirm_requested_io(const safevst3::IoLayout&) noexcept override { events.push_back(Event::Confirm); return confirmation && failure!=safevst3::IoRestartStep::ConfirmArrangements; }
    bool inspect_confirmed_io(safevst3::IoLayout& out) noexcept override { events.push_back(Event::InspectConfirmed); out=confirmed; return failure!=safevst3::IoRestartStep::InspectConfirmed; }
    bool rebuild_process_data(const safevst3::IoLayout&) noexcept override { events.push_back(Event::Rebuild); return failure!=safevst3::IoRestartStep::RebuildProcessData; }
    std::uint32_t query_latency() noexcept override { events.push_back(Event::Latency); return 123; }
    bool commit_io(const safevst3::IoLayout&, std::uint32_t) noexcept override { events.push_back(Event::Commit); return failure!=safevst3::IoRestartStep::Commit; }
    void request_recovery() noexcept override { events.push_back(Event::Recovery); ++recovery; }
};
}

int main()
{
    FakeTarget ok;
    auto result = safevst3::run_io_restart_transaction(ok);
    const std::vector<Event> expected{Event::Stop,Event::Deactivate,Event::InspectRequested,Event::Confirm,Event::InspectConfirmed,Event::Rebuild,Event::Activate,Event::Latency,Event::Start,Event::Commit};
    require(result.committed && result.layout.input_channels==2 && result.layout.output_channels==1 && result.latency_samples==123, "success result changed");
    require(ok.events==expected, "lifecycle ordering changed");

    FakeTarget advisory;
    advisory.confirmation=false;
    result=safevst3::run_io_restart_transaction(advisory);
    require(result.committed, "setBusArrangements false must be advisory when confirmed topology matches");

    FakeTarget mismatch;
    mismatch.confirmation=false;
    mismatch.confirmed={1,2};
    result=safevst3::run_io_restart_transaction(mismatch);
    require(!result.committed && result.failed_step==safevst3::IoRestartStep::ConfirmArrangements && mismatch.recovery==1, "advisory mismatch must recover");

    for (auto failure : {safevst3::IoRestartStep::StopProcessing,safevst3::IoRestartStep::Deactivate,safevst3::IoRestartStep::InspectRequested,safevst3::IoRestartStep::InspectConfirmed,safevst3::IoRestartStep::RebuildProcessData,safevst3::IoRestartStep::Activate,safevst3::IoRestartStep::StartProcessing,safevst3::IoRestartStep::Commit}) {
        FakeTarget target; target.failure=failure;
        auto failed=safevst3::run_io_restart_transaction(target);
        require(!failed.committed && target.recovery==1, "failed frontier must recover exactly once");
    }
    FakeTarget unsupported; unsupported.requested={3,2};
    result=safevst3::run_io_restart_transaction(unsupported);
    require(!result.committed && unsupported.recovery==1, "unsupported layout must recover");

    constexpr std::size_t n=4;
    float l[n]{1,2,3,4}, r[n]{3,4,5,6}, s0[n]{}, s1[n]{}, o0[n]{}, o1[n]{};
    float* pin[2]{};
    require(safevst3::prepare_input_channels(l,r,2,1,n,s0,s1,pin), "stereo->mono input prep");
    require(pin[0]==s0 && s0[0]==2 && s0[3]==5, "stereo->mono average");
    require(safevst3::prepare_input_channels(l,nullptr,1,2,n,s0,s1,pin), "mono->stereo input prep");
    require(s0[2]==3 && s1[2]==3, "mono duplication");
    float* pout[2]{};
    require(safevst3::prepare_output_channels(o0,o1,2,1,s0,s1,pout), "mono plugin output prep");
    for (std::size_t i=0;i<n;++i) pout[0][i]=static_cast<float>(i+1);
    require(safevst3::finalize_output_channels(o0,o1,2,1,n,pout[0],pout[1]), "mono->stereo finalize");
    require(o0[3]==4 && o1[3]==4, "mono output duplication");
    require(safevst3::prepare_output_channels(o0,nullptr,1,2,s0,s1,pout), "stereo plugin output prep");
    for (std::size_t i=0;i<n;++i){pout[0][i]=2; pout[1][i]=4;}
    require(safevst3::finalize_output_channels(o0,nullptr,1,2,n,pout[0],pout[1]), "stereo->mono finalize");
    require(o0[0]==3 && o0[3]==3, "stereo output average");
    std::cout << "io restart transaction ok\n";
}
