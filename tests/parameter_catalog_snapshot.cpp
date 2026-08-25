#include "common/parameter_catalog_snapshot.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "parameter catalog snapshot failure: " << message << '\n';
    std::exit(1);
}

safevst3::ParameterCatalogEntry entry(std::uint32_t id,
                                      double value,
                                      const char* title)
{
    safevst3::ParameterCatalogEntry result{};
    result.id = id;
    result.step_count = id == 20 ? 4 : 0;
    result.flags = id == 20 ? safevst3::ParameterList
                            : safevst3::ParameterCanAutomate;
    result.default_normalized = id == 20 ? 0.25 : 0.5;
    result.current_normalized = value;
    const std::size_t count = std::min(std::strlen(title), result.title.size() - 1);
    std::memcpy(result.title.data(), title, count);
    return result;
}

class FakeSource final : public safevst3::ParameterCatalogSource {
public:
    bool begin_read(std::uint32_t& observed_generation) const noexcept override
    {
        ++begin_calls;
        observed_generation = generation();
        if ((observed_generation & 1u) != 0)
            return false;
        ++active_readers;
        return true;
    }

    void end_read() const noexcept override
    {
        ++end_calls;
        --active_readers;
    }

    std::uint32_t generation() const noexcept override
    {
        if (generation_index >= generations.size())
            return generations.empty() ? 0 : generations.back();
        return generations[generation_index++];
    }

    std::uint32_t exposed_count() const noexcept override
    {
        return static_cast<std::uint32_t>(active_entries().size());
    }

    std::uint32_t total_count() const noexcept override { return total; }

    bool read_entry(std::uint32_t index,
                    safevst3::ParameterCatalogEntry& destination) const noexcept override
    {
        const auto& source = active_entries();
        if (index >= source.size())
            return false;
        destination = source[index];
        return true;
    }

    const std::vector<safevst3::ParameterCatalogEntry>& active_entries() const noexcept
    {
        // The first attempt sees an intentionally mixed/obsolete generation;
        // subsequent attempts see the stable replacement catalog.
        return generation_index <= 1 || replacement.empty() ? initial : replacement;
    }

    mutable std::size_t generation_index = 0;
    mutable std::size_t begin_calls = 0;
    mutable std::size_t end_calls = 0;
    mutable std::size_t active_readers = 0;
    std::vector<std::uint32_t> generations{2, 2};
    std::vector<safevst3::ParameterCatalogEntry> initial;
    std::vector<safevst3::ParameterCatalogEntry> replacement;
    std::uint32_t total = 0;
};

} // namespace

int main()
{
    safevst3::ParameterCatalogSnapshot snapshot{};
    FakeSource stable;
    stable.initial = {entry(10, 0.2, "Gain"), entry(20, 0.75, "Mode")};
    stable.total = 3;
    require(safevst3::read_parameter_catalog_snapshot(stable, snapshot),
            "a stable even generation must be readable");
    require(snapshot.count == 2 && snapshot.total_count == 3,
            "stable catalog counts changed");
    require(snapshot.entries[0].id == 10 && snapshot.entries[0].current_normalized == 0.2,
            "stable first parameter changed");
    require(std::string(snapshot.entries[1].title.data()) == "Mode",
            "stable parameter metadata changed");
    require(stable.begin_calls == 1 && stable.end_calls == 1 && stable.active_readers == 0,
            "stable read must hold and release one reader guard");

    FakeSource changing;
    changing.generations = {2, 3, 4, 4};
    changing.initial = {entry(10, 0.2, "Old")};
    changing.replacement = {entry(30, 0.8, "New A"), entry(40, 0.6, "New B")};
    changing.total = 2;
    snapshot = {};
    require(safevst3::read_parameter_catalog_snapshot(changing, snapshot),
            "reader must retry and accept the next stable generation");
    require(snapshot.count == 2 && snapshot.entries[0].id == 30 &&
                std::string(snapshot.entries[1].title.data()) == "New B",
            "reader exposed a mixed old/new catalog generation");
    require(changing.begin_calls == 2 && changing.end_calls == 2 &&
                changing.active_readers == 0,
            "every admitted retry must release its reader guard");

    FakeSource unstable;
    unstable.generations = {2, 3, 4, 5, 6, 7};
    unstable.initial = {entry(10, 0.2, "Unstable")};
    snapshot.count = 99;
    require(!safevst3::read_parameter_catalog_snapshot(unstable, snapshot),
            "reader must reject a catalog that never stabilizes");
    require(snapshot.count == 0 && snapshot.total_count == 0,
            "failed read must return an empty snapshot");
    require(unstable.generation_index == safevst3::kParameterCatalogReadAttempts * 2,
            "catalog retries must remain strictly bounded");
    require(unstable.active_readers == 0,
            "failed stable-generation attempts must release all reader guards");

    FakeSource writer_active;
    writer_active.generations = {1, 3, 5};
    writer_active.initial = {entry(10, 0.2, "Writing")};
    require(!safevst3::read_parameter_catalog_snapshot(writer_active, snapshot),
            "odd writer generations must never be exposed");
    require(writer_active.generation_index == safevst3::kParameterCatalogReadAttempts,
            "active-writer retries must remain strictly bounded");
    require(writer_active.end_calls == 0 && writer_active.active_readers == 0,
            "odd generations must not admit a catalog reader");

    std::cout << "parameter catalog snapshot ok\n";
    return 0;
}
