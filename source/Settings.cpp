module;
#include "stdafx.h"
#include <fstream>
#include "Bindings.h"
#include "ResolutionPolicy.h"

module WindowedMode;

void WindowedMode::EnableDpiAwareness()
{
    // Borderless/windowed Direct3D sizes must use physical pixels. A legacy
    // DPI-unaware process can otherwise treat a 3840x2160 desktop as 1920x1080
    // at 200% Windows scaling, with Windows upscaling the result.
    //
    // Call before CreateWindowEx. Respect any DPI awareness already selected by
    // the game manifest or by a different plugin.
    highDpiAware = config.ReadInteger("MAIN", "HighDPIAware", 1) != 0;
    resolutionDiagnostics = config.ReadInteger("MAIN", "ResolutionDiagnostics", 0) != 0;
    if (!highDpiAware)
    {
        dpiSetupResult = "disabled in INI";
        return;
    }

    const HMODULE user32 = GetModuleHandleA("user32.dll");
    using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto setContext = reinterpret_cast<SetContextFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setContext)
    {
        if (setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        {
            dpiSetupResult = "per-monitor-v2 enabled";
            return;
        }
        DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED)
        {
            dpiSetupResult = "already set (manifest/other plugin)";
            return;
        }
        if (error == ERROR_INVALID_PARAMETER)
        {
            if (setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))
            {
                dpiSetupResult = "per-monitor-v1 enabled";
                return;
            }
            if (GetLastError() == ERROR_ACCESS_DENIED)
            {
                dpiSetupResult = "already set (manifest/other plugin)";
                return;
            }
        }
        dpiSetupResult = "could not set per-monitor DPI awareness";
        return;
    }

    // Vista/7/8 fallback: sufficient to prevent scaling on the primary display.
    using SetLegacyFn = BOOL(WINAPI*)();
    const auto setLegacy = reinterpret_cast<SetLegacyFn>(
        GetProcAddress(user32, "SetProcessDPIAware"));
    if (setLegacy && setLegacy())
        dpiSetupResult = "system DPI awareness enabled";
    else
        dpiSetupResult = "DPI awareness unavailable or already set";
}

void WindowedMode::WriteResolutionDiagnostics(const char* event)
{
    if (!resolutionDiagnostics || !window) return;

    // One write at first Present and at successful device resets, not each
    // frame. Enable diagnostics only when troubleshooting.
    auto diagnosticPath = std::filesystem::path(config.GetIniPath());
    diagnosticPath += ".resolution.log";
    std::ofstream file(diagnosticPath, std::ios::app);
    if (!file) return;

    RECT client{};
    GetClientRect(window, &client);
    MONITORINFOEXA monitor{};
    monitor.cbSize = sizeof(monitor);
    const bool hasMonitor = GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST),
        &monitor) != FALSE;
    DEVMODEA display{};
    display.dmSize = sizeof(display);
    const bool hasDisplay = hasMonitor &&
        EnumDisplaySettingsA(monitor.szDevice, ENUM_CURRENT_SETTINGS, &display) != FALSE;

    UINT dpi = 0;
    using GetDpiFn = UINT(WINAPI*)(HWND);
    const auto getDpi = reinterpret_cast<GetDpiFn>(
        GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow"));
    if (getDpi) dpi = getDpi(window);

    auto modeWidth = 0u, modeHeight = 0u;
    if (*rwVideoModes)
    {
        const auto idx = RwEngineGetCurrentVideoMode();
        const auto count = RwEngineGetNumVideoModes();
        if (idx < count && count != DWORD(-1))
        {
            modeWidth = (*rwVideoModes)[idx].width;
            modeHeight = (*rwVideoModes)[idx].height;
        }
    }

    const RwCamera* camera = bindings.camera ? *bindings.camera : nullptr;
    const RwRaster* raster = camera ? camera->frameBuffer : nullptr;
    const auto backbufferWidth = IsD3D9() ? d3dPresentParams9->BackBufferWidth :
        d3dPresentParams8->BackBufferWidth;
    const auto backbufferHeight = IsD3D9() ? d3dPresentParams9->BackBufferHeight :
        d3dPresentParams8->BackBufferHeight;
    SYSTEMTIME time{};
    GetLocalTime(&time);
    file << "---- " << event << " " << time.wYear << '-' << time.wMonth << '-'
         << time.wDay << " " << time.wHour << ':' << time.wMinute << ':'
         << time.wSecond << " ----\n"
         << "DPI setup: " << dpiSetupResult << "; window DPI: " << dpi << "\n"
         << "Window mode: " << int(windowMode)
         << "; saved requested client: " << windowSizeWindowed.x
         << 'x' << windowSizeWindowed.y
         << "; desktop auto: " << autoDesktopResolution << "\n"
         << "Client: " << client.right << 'x' << client.bottom
         << "; D3D backbuffer: " << backbufferWidth << 'x' << backbufferHeight
         << "; active RW mode: " << modeWidth << 'x' << modeHeight << "\n"
         << "RW camera raster: " << (raster ? raster->nWidth : 0)
         << 'x' << (raster ? raster->nHeight : 0) << "\n";
    if (hasDisplay)
        file << "Physical monitor mode: " << display.dmPelsWidth << 'x'
             << display.dmPelsHeight << "\n";
    else
        file << "Physical monitor mode: unavailable\n";
    file << '\n';
}

void WindowedMode::InitConfig()
{
    if (!std::filesystem::exists(config.GetIniPath()))
    {
        // New installations use the desktop resolution rather than the
        // classic game's 640x448 fallback. Existing INIs are never replaced.
        windowMode = Fullscreen;
        windowedStyle = Windowed;
        autoDesktopResolution = true;
        SaveConfig();
    }
}

bool WindowedMode::LoadConfig()
{
    // Read old v2 keys as fallbacks. CIniReader is case-sensitive.
    auto read = [this](const char* section, const char* key, const char* oldSection, const char* oldKey, int fallback)
    {
        return config.ReadInteger(section, key, config.ReadInteger(oldSection, oldKey, fallback));
    };
    windowMode = static_cast<WindowMode>(std::clamp(read("MAIN", "WindowMode", "window", "mode", Windowed), int(Min), int(Max)));
    windowedStyle = static_cast<WindowMode>(std::clamp(read("MAIN", "WindowedStyle", "window", "windowedStyle",
        windowMode == Fullscreen ? Windowed : windowMode), int(Windowed), int(WindowedBorderless)));
    if (windowMode != Fullscreen) windowedStyle = windowMode;

    windowPosWindowed.x = read("WINDOW", "PositionX", "window", "positionX", -1);
    windowPosWindowed.y = read("WINDOW", "PositionY", "window", "positionY", -1);
    const int configuredWidth = read("WINDOW", "ResolutionX", "window", "resolutionX", 0);
    const int configuredHeight = read("WINDOW", "ResolutionY", "window", "resolutionY", 0);
    const auto requested = ResolutionPolicy::NormalizeRequested(
        {configuredWidth, configuredHeight},
        {Resolution_Min.x, Resolution_Min.y});
    autoDesktopResolution = ResolutionPolicy::DesktopAuto(requested.width, requested.height);
    if (autoDesktopResolution)
    {
        // Use the monitor containing the saved window position, or the
        // primary monitor for a centered window.
        const RECT monitor = GetMonitorRect({
            windowPosWindowed.x == -1 ? 0 : windowPosWindowed.x,
            windowPosWindowed.y == -1 ? 0 : windowPosWindowed.y
        });
        windowSizeWindowed = {
            monitor.right - monitor.left, monitor.bottom - monitor.top
        };
    }
    else
    {
        windowSizeWindowed.x = requested.width;
        windowSizeWindowed.y = requested.height;
    }
    windowPos = windowPosWindowed;
    windowSize = windowSizeClient = windowSizeWindowed;
    autoPause = read("MAIN", "AutoPause", "game", "autoPause", 1) != 0;
    autoResume = read("MAIN", "AutoResume", "game", "autoResume", 1) != 0;
    return read("WINDOW", "Maximized", "window", "maximized", 0) != 0;
}

void WindowedMode::SaveConfig()
{
    // Most WM_WINDOWPOSCHANGED notifications don't change persisted values.
    // Avoid synchronous file reads/writes (and antivirus scans) in those cases.
    const std::array<int, 9> state = {
        int(windowMode), int(windowedStyle), int(autoPause), int(autoResume),
        int(window && IsZoomed(window)), windowPosWindowed.x, windowPosWindowed.y,
        autoDesktopResolution ? 0 : windowSizeWindowed.x,
        autoDesktopResolution ? 0 : windowSizeWindowed.y
    };
    if (savedConfigStateValid && state == savedConfigState) return;

    // Preserve unrelated settings and previous keys. Refuse to replace an
    // existing INI that couldn't be read (permissions/corruption).
    const auto iniPath = std::filesystem::path(config.GetIniPath());
    mINI::INIFile file(iniPath.string());
    mINI::INIStructure ini;
    if (!file.read(ini) && std::filesystem::exists(iniPath)) return;
    auto set = [&ini](const char* section, const char* key, int value)
    {
        ini[section][key] = std::to_string(value);
    };
    set("MAIN", "WindowMode", windowMode);
    set("MAIN", "WindowedStyle", windowedStyle);
    set("MAIN", "AutoPause", autoPause);
    set("MAIN", "AutoResume", autoResume);
    set("MAIN", "HighDPIAware", highDpiAware);
    set("MAIN", "ResolutionDiagnostics", resolutionDiagnostics);
    set("WINDOW", "Maximized", window && IsZoomed(window));
    set("WINDOW", "PositionX", windowPosWindowed.x);
    set("WINDOW", "PositionY", windowPosWindowed.y);
    set("WINDOW", "ResolutionX", autoDesktopResolution ? 0 : windowSizeWindowed.x);
    set("WINDOW", "ResolutionY", autoDesktopResolution ? 0 : windowSizeWindowed.y);
    // Write a sibling temporary file, then atomically replace the INI.
    // A crash during serialization can no longer leave a half-written INI.
    auto temporaryPath = iniPath;
    temporaryPath += ".tmp";
    mINI::INIFile temporaryFile(temporaryPath.string());
    if (temporaryFile.write(ini, true) &&
        MoveFileExW(temporaryPath.c_str(), iniPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        savedConfigState = state;
        savedConfigStateValid = true;
    }
    else
        DeleteFileW(temporaryPath.c_str());
}
