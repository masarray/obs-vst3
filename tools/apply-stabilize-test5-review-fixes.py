from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        # Idempotent success when the replacement is already present.
        if new in text:
            return text
        raise SystemExit(f"patch anchor not found: {label}")
    return text.replace(old, new, 1)


plugin_path = Path("src/obs-plugin/plugin.cpp")
text = plugin_path.read_text(encoding="utf-8")

text = replace_once(
    text,
    """std::atomic<bool> scanner_in_progress{false};
std::jthread startup_scanner_thread;
std::jthread manual_scanner_thread;
std::mutex manual_scanner_thread_mutex;
""",
    """std::atomic<bool> scanner_in_progress{false};
std::atomic<bool> scanner_shutting_down{false};
std::jthread startup_scanner_thread;
std::jthread manual_scanner_thread;
std::mutex manual_scanner_thread_mutex;
std::mutex scan_refresh_mutex;
std::vector<obs_weak_source_t*> scan_refresh_waiters;
""",
    "scanner globals",
)

text = replace_once(
    text,
    """void start_startup_scanner()
{
""",
    """void request_scan_completion_refresh(Filter* filter)
{
    if (!filter || !filter->context ||
        filter->shutting_down.load(std::memory_order_acquire) ||
        scanner_shutting_down.load(std::memory_order_acquire))
        return;

    obs_weak_source_t* weak = obs_source_get_weak_source(filter->context);
    if (!weak)
        return;

    std::lock_guard lock(scan_refresh_mutex);
    if (scanner_shutting_down.load(std::memory_order_acquire)) {
        obs_weak_source_release(weak);
        return;
    }
    scan_refresh_waiters.push_back(weak);
}

void refresh_scan_waiters()
{
    std::vector<obs_weak_source_t*> waiters;
    {
        std::lock_guard lock(scan_refresh_mutex);
        waiters.swap(scan_refresh_waiters);
    }

    const bool unloading = scanner_shutting_down.load(std::memory_order_acquire);
    for (obs_weak_source_t* weak : waiters) {
        if (!unloading) {
            if (obs_source_t* source = obs_weak_source_get_source(weak)) {
                // No module callback is queued here. The scanner worker emits
                // the core properties-update signal while it is still joined
                // by stop_scanners(); the weak source avoids extending a
                // filter lifetime across module unload.
                obs_source_update_properties(source);
                obs_source_release(source);
            }
        }
        obs_weak_source_release(weak);
    }
}

void cancel_scan_refresh_waiters()
{
    std::vector<obs_weak_source_t*> waiters;
    {
        std::lock_guard lock(scan_refresh_mutex);
        waiters.swap(scan_refresh_waiters);
    }
    for (obs_weak_source_t* weak : waiters)
        obs_weak_source_release(weak);
}

void start_startup_scanner()
{
""",
    "scan refresh helpers",
)

text = replace_once(
    text,
    """        else if (result == ScannerRunResult::Failed)
            blog(LOG_WARNING, "[obs-safe-vst3] background installed VST3 discovery failed; existing cache kept");
    });
}
""",
    """        else if (result == ScannerRunResult::Failed)
            blog(LOG_WARNING, "[obs-safe-vst3] background installed VST3 discovery failed; existing cache kept");
        refresh_scan_waiters();
    });
}
""",
    "startup completion refresh",
)

text = replace_once(
    text,
    """void stop_scanners()
{
    startup_scanner_thread.request_stop();
""",
    """void stop_scanners()
{
    // Block new refresh registrations before stopping workers. Joining both
    // workers guarantees no module-owned completion code is still executing;
    // remaining weak waiters are then released before the DLL can unload.
    scanner_shutting_down.store(true, std::memory_order_release);
    startup_scanner_thread.request_stop();
""",
    "scanner shutdown gate",
)

text = replace_once(
    text,
    """    {
        std::lock_guard lock(manual_scanner_thread_mutex);
        manual_scanner_thread = std::jthread{};
    }
}

std::vector<ScanEntry> load_scan_cache()
""",
    """    {
        std::lock_guard lock(manual_scanner_thread_mutex);
        manual_scanner_thread = std::jthread{};
    }
    cancel_scan_refresh_waiters();
}

std::vector<ScanEntry> load_scan_cache()
""",
    "scanner waiter drain",
)

text = replace_once(
    text,
    """    populate_plugin_list(obs_properties_get(props, kPluginPath));
    if (!try_claim_scanner()) {
        blog(LOG_INFO, "[obs-safe-vst3] Rescan requested while discovery is already active; reusing active scan");
        return true;
    }

    obs_source_t* context = nullptr;
    if (filter && filter->context && !filter->shutting_down.load(std::memory_order_acquire))
        context = obs_source_get_ref(filter->context);

    try {
        std::lock_guard lock(manual_scanner_thread_mutex);
        if (manual_scanner_thread.joinable())
            manual_scanner_thread = std::jthread{};
        manual_scanner_thread = std::jthread([context](std::stop_token stop) {
            ScannerClaimGuard claim(true);
            const auto result = run_scanner_claimed(stop);
            if (result == ScannerRunResult::Success)
                blog(LOG_INFO, "[obs-safe-vst3] installed VST3 cache refreshed");
            else if (result == ScannerRunResult::Failed)
                blog(LOG_WARNING, "[obs-safe-vst3] manual installed VST3 discovery failed; existing cache kept");

            if (context) {
                if (!stop.stop_requested())
                    obs_source_update_properties(context);
                obs_source_release(context);
            }
        });
    } catch (const std::exception& e) {
        scanner_in_progress.store(false, std::memory_order_release);
        if (context)
            obs_source_release(context);
        blog(LOG_ERROR, "[obs-safe-vst3] could not start background Rescan worker: %s", e.what());
    }
""",
    """    populate_plugin_list(obs_properties_get(props, kPluginPath));
    request_scan_completion_refresh(filter);
    if (!try_claim_scanner()) {
        blog(LOG_INFO, "[obs-safe-vst3] Rescan requested while discovery is already active; reusing active scan");
        return true;
    }

    try {
        std::lock_guard lock(manual_scanner_thread_mutex);
        if (manual_scanner_thread.joinable())
            manual_scanner_thread = std::jthread{};
        manual_scanner_thread = std::jthread([](std::stop_token stop) {
            ScannerClaimGuard claim(true);
            const auto result = run_scanner_claimed(stop);
            if (result == ScannerRunResult::Success)
                blog(LOG_INFO, "[obs-safe-vst3] installed VST3 cache refreshed");
            else if (result == ScannerRunResult::Failed)
                blog(LOG_WARNING, "[obs-safe-vst3] manual installed VST3 discovery failed; existing cache kept");

            if (!stop.stop_requested())
                refresh_scan_waiters();
        });
    } catch (const std::exception& e) {
        scanner_in_progress.store(false, std::memory_order_release);
        refresh_scan_waiters();
        blog(LOG_ERROR, "[obs-safe-vst3] could not start background Rescan worker: %s", e.what());
    }
""",
    "rescan completion lifecycle",
)

text = replace_once(
    text,
    """bool obs_module_load(void)
{
    try {
""",
    """bool obs_module_load(void)
{
    scanner_shutting_down.store(false, std::memory_order_release);
    try {
""",
    "module load scanner reset",
)

plugin_path.write_text(text, encoding="utf-8")

installer_path = Path("installer/windows/obs-safe-vst3.iss")
text = installer_path.read_text(encoding="utf-8")

text = replace_once(
    text,
    """  ObsRootParam: String;
  LegacyPortableObsDirParam: String;
  CloseObsParam: String;
""",
    """  ObsRootParam: String;
  LegacyPortableObsDirParam: String;
  CloseObsParam: String;
  PreviousObsRoot: String;
""",
    "installer previous-root variable",
)

text = replace_once(
    text,
    """function GetPluginLocaleDir(Param: String): String;
begin
  Result := GetPluginDataDir('') + '\\locale';
end;

function IsObsRunning: Boolean;
""",
    """function GetPluginLocaleDir(Param: String): String;
begin
  Result := GetPluginDataDir('') + '\\locale';
end;

function ShouldCleanPreviousObsRoot: Boolean;
begin
  Result := (PreviousObsRoot <> '') and IsObsRootValid(PreviousObsRoot) and
    (CompareText(RemoveBackslashUnlessRoot(PreviousObsRoot),
      RemoveBackslashUnlessRoot(SelectedObsRoot)) <> 0);
end;

function GetPreviousPluginBinDir(Param: String): String;
begin
  Result := AddSlash(PreviousObsRoot) + 'obs-plugins\\64bit';
end;

function GetPreviousPluginDataDir(Param: String): String;
begin
  Result := AddSlash(PreviousObsRoot) + 'data\\obs-plugins\\obs-safe-vst3';
end;

function IsObsRunning: Boolean;
""",
    "installer previous-root helpers",
)

text = replace_once(
    text,
    """Type: files; Name: "{code:GetPluginBinDir}\\obs-safe-vst3-scanner.exe"
Type: filesandordirs; Name: "{code:GetPluginDataDir}"

[Code]
""",
    """Type: files; Name: "{code:GetPluginBinDir}\\obs-safe-vst3-scanner.exe"
Type: filesandordirs; Name: "{code:GetPluginDataDir}"
; If an earlier installer remembered a different valid OBS root, remove only
; this product from that old root before LastObsRoot is updated to the new one.
Type: files; Name: "{code:GetPreviousPluginBinDir}\\obs-safe-vst3.dll"; Check: ShouldCleanPreviousObsRoot
Type: files; Name: "{code:GetPreviousPluginBinDir}\\obs-safe-vst3-host.exe"; Check: ShouldCleanPreviousObsRoot
Type: files; Name: "{code:GetPreviousPluginBinDir}\\obs-safe-vst3-scanner.exe"; Check: ShouldCleanPreviousObsRoot
Type: filesandordirs; Name: "{code:GetPreviousPluginDataDir}"; Check: ShouldCleanPreviousObsRoot

[Code]
""",
    "installer old-root deletion",
)

text = replace_once(
    text,
    """  InitialRoot := LoadRememberedObsRoot;
  if InitialRoot = '' then
""",
    """  PreviousObsRoot := LoadRememberedObsRoot;
  InitialRoot := PreviousObsRoot;
  if InitialRoot = '' then
""",
    "installer remember previous root",
)

installer_path.write_text(text, encoding="utf-8")
print("stabilize-test5 review fixes applied")
