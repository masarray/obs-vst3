#ifdef _WIN32

#define wmain legacy_scanner_wmain
#include "main_legacy.cpp"
#undef wmain

#include <string_view>

namespace {

bool subcategory_has_token(const std::vector<std::string>& subcategories,
                           std::string_view token) noexcept
{
    for (const auto& value : subcategories) {
        std::size_t start = 0;
        while (start <= value.size()) {
            const auto end = value.find('|', start);
            const auto length = end == std::string::npos ? value.size() - start : end - start;
            if (std::string_view(value).substr(start, length) == token)
                return true;
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    return false;
}

bool should_publish_obs_insert_effect(const std::vector<std::string>& subcategories) noexcept
{
    // Keep every explicit effect, including dual-role Fx|Instrument classes.
    // Exclude only classes that explicitly declare Instrument without any Fx token.
    // Empty, legacy or unknown metadata stays visible so the scanner never loses
    // a valid effect merely because a vendor omitted or invented subcategories.
    if (subcategory_has_token(subcategories, "Fx"))
        return true;
    if (subcategory_has_token(subcategories, "Instrument"))
        return false;
    return true;
}

int probe_insert_effects_only(const fs::path& plugin, const fs::path& output)
{
    std::string error;
    const std::string plugin_utf8 = utf8(plugin.wstring());
    auto module = VST3::Hosting::Module::create(plugin_utf8, error);
    if (!module) {
        std::cerr << error << '\n';
        return 3;
    }

    auto factory = module->getFactory();
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out)
        return 4;

    bool found = false;
    for (const auto& info : factory.classInfos()) {
        if (info.category() != kVstAudioEffectClass)
            continue;
        if (!should_publish_obs_insert_effect(info.subCategories()))
            continue;

        found = true;
        out << sanitize(info.name()) << '\t'
            << sanitize(plugin_utf8) << '\t'
            << sanitize(info.ID().toString()) << '\n';
    }

    // A successful probe containing only instrument-only classes is deliberately
    // treated as "no audio effects". The parent will then remove stale cached
    // instrument entries instead of retaining them as fallback effects.
    return found ? 0 : 5;
}

bool classification_self_test() noexcept
{
    const std::vector<std::string> empty;
    return should_publish_obs_insert_effect(empty) &&
           should_publish_obs_insert_effect({"Fx"}) &&
           should_publish_obs_insert_effect({"Fx", "EQ"}) &&
           should_publish_obs_insert_effect({"Fx", "Instrument"}) &&
           should_publish_obs_insert_effect({"Fx|Instrument"}) &&
           should_publish_obs_insert_effect({"Spatial", "Fx"}) &&
           should_publish_obs_insert_effect({"Analyzer"}) &&
           should_publish_obs_insert_effect({"VendorSpecificCategory"}) &&
           should_publish_obs_insert_effect({"instrument", "synth"}) &&
           !should_publish_obs_insert_effect({"Instrument"}) &&
           !should_publish_obs_insert_effect({"Instrument", "Synth"}) &&
           !should_publish_obs_insert_effect({"Instrument|Synth"}) &&
           !should_publish_obs_insert_effect({"Instrument|Sampler"});
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    fs::path probe_path;
    fs::path out_path;
    DWORD parent_pid = 0;
    bool self_test = false;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--probe" && i + 1 < argc)
            probe_path = argv[++i];
        else if (arg == L"--out" && i + 1 < argc)
            out_path = argv[++i];
        else if (arg == L"--parent-pid" && i + 1 < argc)
            parent_pid = static_cast<DWORD>(std::wcstoul(argv[++i], nullptr, 10));
        else if (arg == L"--self-test")
            self_test = true;
    }

    if (self_test) {
        const int legacy_result = legacy_scanner_wmain(argc, argv);
        if (legacy_result != 0)
            return legacy_result;
        if (!classification_self_test()) {
            std::cerr << "scanner effect classification self-test failed\n";
            return 12;
        }
        std::cout << "scanner effect classification self-test passed\n";
        return 0;
    }

    if (!probe_path.empty() && !out_path.empty()) {
        std::jthread parent_watchdog;
        if (!start_parent_watchdog(parent_pid, parent_watchdog)) {
            std::cerr << "probe parent watchdog unavailable; refusing to load vendor code\n";
            return 11;
        }
        return probe_insert_effects_only(probe_path, out_path);
    }

    // Discovery, full scan orchestration and all other scanner behavior remain
    // byte-for-byte on the proven implementation. Its child probes re-enter
    // this wrapper, so only class publication semantics change.
    return legacy_scanner_wmain(argc, argv);
}

#else
#include "main_legacy.cpp"
#endif
