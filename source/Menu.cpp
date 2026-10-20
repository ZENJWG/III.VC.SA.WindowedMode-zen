module;
#include "stdafx.h"
#include "Bindings.h"

module WindowedMode;

void WindowedMode::BackupVideoModes()
{
    const auto count = RwEngineGetNumVideoModes();
    if (!*rwVideoModes || !count || count == DWORD(-1)) return;
    if (videoModesSource != *rwVideoModes || videoModesBackup.size() != count)
    {
        videoModesSource = *rwVideoModes;
        videoModesBackup.assign(videoModesSource, videoModesSource + count);
        modifiedVideoMode = -1;
    }
}

void WindowedMode::SyncActiveVideoMode()
{
    BackupVideoModes();
    if (!*rwVideoModes) return;
    const auto current = RwEngineGetCurrentVideoMode();
    if (current >= videoModesBackup.size()) return;

    if (modifiedVideoMode >= 0 && modifiedVideoMode != static_cast<int>(current) &&
        modifiedVideoMode < static_cast<int>(videoModesBackup.size()))
        (*rwVideoModes)[modifiedVideoMode] = videoModesBackup[modifiedVideoMode];

    modifiedVideoMode = static_cast<int>(current);
    auto& mode = (*rwVideoModes)[current];
    mode.width = static_cast<UINT>(windowSizeClient.x);
    mode.height = static_cast<UINT>(windowSizeClient.y);
    mode.flags &= ~1u;
    mode.refreshRate = 0;
    const auto format = IsD3D9() ? d3dPresentParams9->BackBufferFormat :
        d3dPresentParams8->BackBufferFormat;
    if (format != D3DFMT_UNKNOWN) mode.format = format;
}

void WindowedMode::ChangeResolution(DWORD modeIndex)
{
    // Settings-loading hooks can run before the first device is created.
    // Those are not user window-resize requests; keep the INI geometry.
    if (!d3dDevice) return;
    BackupVideoModes();
    if (!*rwVideoModes || modeIndex >= videoModesBackup.size()) return;
    const auto& mode = videoModesBackup[modeIndex];
    if (mode.width < UINT(Resolution_Min.x) || mode.height < UINT(Resolution_Min.y) ||
        mode.width > LONG_MAX || mode.height > LONG_MAX) return;
    WindowResize({ (LONG)mode.width, (LONG)mode.height });
}

void __cdecl WindowedMode::ChangeResolutionSA(DWORD modeIndex)
{
    inst->ChangeResolution(modeIndex);
    inst->bindings.selectVideoMode(modeIndex);
}

int __cdecl WindowedMode::ChangeVideoModeSA(DWORD modeIndex)
{
    inst->BackupVideoModes();
    if (!*inst->rwVideoModes || modeIndex >= inst->videoModesBackup.size()) return 0;
    // SA also uses this call for AA changes and loading settings. Those are not
    // window resize requests. Let RW rebuild resources, using the actual client.
    auto& mode = (*inst->rwVideoModes)[modeIndex];
    const auto saved = mode;
    mode.width = inst->windowSizeClient.x;
    mode.height = inst->windowSizeClient.y;
    mode.flags &= ~1u;
    const auto result = inst->bindings.changeVideoMode(modeIndex);
    mode = saved;
    if (inst->RwEngineGetCurrentVideoMode() == modeIndex)
        inst->SyncActiveVideoMode();
    return result;
}

bool WindowedMode::IsMainMenuVisible() const
{
    return *bindings.menuActive;
}

void WindowedMode::SwitchMainMenu(bool show)
{
    if (show == IsMainMenuVisible()) return;
    if (bindings.requestMenuOpen)
    {
        if (show) bindings.requestMenuOpen();
        else bindings.requestMenuClose();
    }
    else
    {
        *bindings.menuOpen = show;
        *bindings.menuClose = !show;
    }
}
