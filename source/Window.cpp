module;
#include "stdafx.h"
#include "Bindings.h"
#include "ResolutionPolicy.h"
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

module WindowedMode;

WindowedMode::WindowedMode(const GameBindings& resolved) :
    bindings(resolved), gameTitle(resolved.title), gameState(*resolved.state),
    rsGlobal(resolved.globals), d3dPresentParams8(resolved.presentParameters), rwVideoModes(resolved.modes),
    RwEngineGetNumVideoModes(resolved.getNumVideoModes),
    RwEngineGetCurrentVideoMode(resolved.getCurrentVideoMode)
{}

const WindowedMode::AspectRatioInfo WindowedMode::AspectRatios[] = {
    { "1:1", 1.0f / 1.0f },
    { "2:1", 2.0f / 1.0f },
    { "4:3", 4.0f / 3.0f },
    { "5:4", 5.0f / 4.0f },
    { "10:7", 10.0f / 7.0f }, // GTA default aspect
    { "16:9", 16.0f / 9.0f },
    { "16:10", 16.0f / 10.0f }
};

HWND __stdcall WindowedMode::InitWindow(DWORD, LPCSTR className, LPCSTR title, DWORD,
    int, int, int, int, HWND parent, HMENU menu, HINSTANCE module, LPVOID param)
{
    try { inst->InstallRenderHooks(); }
    catch (const std::exception& error)
    {
        ShowError("%s", error.what());
        return nullptr;
    }
    inst->InitConfig();
    // Must be set before monitor geometry is queried or the HWND is created.
    inst->EnableDpiAwareness();
    const bool maximize = inst->LoadConfig();
    const bool center = inst->windowPos.x == -1 || inst->windowPos.y == -1;
    inst->WindowCalculateGeometry(center);
    inst->window = createWindow(inst->WindowStyleEx(), className, title, inst->WindowStyle(),
        inst->windowPos.x, inst->windowPos.y, inst->windowSize.x, inst->windowSize.y,
        parent, menu, module, param);
    if (!inst->window) return nullptr;

    // Keep the game's ANSI window procedure chain. Switching it to Unicode
    // gives ANSI subclasses a conversion token that some mods call directly.
    inst->oriWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(inst->window,
        GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
    if (maximize && inst->windowMode != Fullscreen)
        ShowWindow(inst->window, SW_MAXIMIZE);
    else
        inst->WindowCalculateGeometry(center, true);
    inst->WindowUpdateTitle();
    UpdateWindow(inst->window);
    return inst->window;
}

int WindowedMode::FindAspectRatio(POINT resolution, float threshold)
{
    auto ratio = float(resolution.x) / resolution.y;

    // find best match in
    int idx = -1;
    float dist = 9999.0f;
    for (size_t i = 0; i < _countof(AspectRatios); i++)
    {
        auto diff = fabs(AspectRatios[i].ratio - ratio);
        if (diff < dist)
        {
            idx = i;
            dist = diff;
        }
    }

    return dist <= threshold ? idx : -1;
}

DWORD WindowedMode::WindowStyle() const
{
    return WS_VISIBLE | WS_CLIPSIBLINGS | ((windowMode == WindowMode::Windowed) ?
        WS_OVERLAPPEDWINDOW :
        WS_POPUP);
}

DWORD WindowedMode::WindowStyleEx() const
{
    return 0;
}

void WindowedMode::WindowCalculateGeometry(bool center, bool resizeWindow)
{
    if (windowUpdating) return;
    windowUpdating = true;
    cursorClientRectValid = false;

    POINT windowCenter = { windowPos.x + windowSize.x / 2, windowPos.y + windowSize.y / 2};
    auto monitorRect = GetMonitorRect(windowCenter);
    auto monitorWidth = monitorRect.right - monitorRect.left;
    auto monitorHeight = monitorRect.bottom - monitorRect.top;
    bool monitorSingle = GetSystemMetrics(SM_CMONITORS) <= 1;

    // size
    if (windowMode == WindowMode::Fullscreen)
    {
        windowPos.x = monitorRect.left;
        windowPos.y = monitorRect.top;
        windowSize.x = windowSizeClient.x = monitorWidth;
        windowSize.y = windowSizeClient.y = monitorHeight;
    }
    else if (!IsZoomed(window)) // not maximized windowed modes
    {
        windowSize = SizeFromClient(windowSizeWindowed);

        if (monitorSingle) // limit window size to desktop
        {
            windowSize.x = min(windowSize.x, monitorWidth);
            windowSize.y = min(windowSize.y, monitorHeight);
        }

        // The actual bordered client may be smaller than a 4K request because
        // its frame must fit on the monitor. Do not overwrite the requested
        // resolution merely by switching window styles.
        windowSizeClient = ClientFromSize(windowSize);

        // window position
        if (center)
        {
            windowPosWindowed.x = monitorRect.left + (monitorWidth - windowSize.x) / 2;
            windowPosWindowed.y = monitorRect.top + (monitorHeight - windowSize.y) / 2;
        }
        
        if (monitorSingle) // keep entire window on the screen
        {
            windowPosWindowed.x = max(windowPosWindowed.x, monitorRect.left);
            if (windowPosWindowed.x + windowSize.x > monitorRect.right)
            {
                windowPosWindowed.x = monitorRect.right - windowSize.x;
            }

            windowPosWindowed.y = max(windowPosWindowed.y, monitorRect.top);
            if (windowPosWindowed.y + windowSize.y > monitorRect.bottom)
            {
                windowPosWindowed.y = monitorRect.bottom - windowSize.y;
            }
        }

        windowPos = windowPosWindowed;
    }

    // apply to the window
    if (resizeWindow && !IsZoomed(window))
    {
        SetWindowLong(window, GWL_STYLE, WindowStyle());
        SetWindowLong(window, GWL_EXSTYLE, WindowStyleEx());
        SetWindowPos(window, 0, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW); // update the frame
        auto padding = GetFrameSize(true);

        SetWindowPos(window, 0,
            windowPos.x - padding.left,
            windowPos.y - padding.top,
            windowSize.x,
            windowSize.y,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);

        WindowUpdateTitle();
    }

    // Windows is authoritative (DPI, maximization and frame metrics can change).
    RECT client;
    if (window && !IsIconic(window) && GetClientRect(window, &client) &&
        client.right > 0 && client.bottom > 0)
    {
        windowSizeClient = { client.right, client.bottom };
        // windowSizeWindowed is the requested (persisted) size. Real user
        // resizing is recorded in WM_WINDOWPOSCHANGED instead.
    }

    // apply resolution to game internals
    if (gameTitle == GameTitle::GTASA)
    {
        if (rsGlobalSA->ps) { rsGlobalSA->ps->fullScreen = false; rsGlobalSA->ps->window = window; }
        rsGlobalSA->MaximumWidth = windowSizeClient.x;
        rsGlobalSA->MaximumHeight = windowSizeClient.y;
    }
    else
    {
        if (rsGlobal->ps) { rsGlobal->ps->fullScreen = false; rsGlobal->ps->window = window; }
        rsGlobal->screenWidth = rsGlobal->MaximumWidth = windowSizeClient.x;
        rsGlobal->screenHeight = rsGlobal->MaximumHeight = windowSizeClient.y;
    }

    // Do not edit Present here: it belongs to RenderWare's reset/recovery path.
    // In particular, WM_SIZE can be dispatched synchronously from inside Reset.
    windowUpdating = false;
    if (resizeWindow && window && d3dDevice && !resetInProgress)
        CallWindowProcA(oriWindowProc, window, WM_SIZE, SIZE_RESTORED,
            MAKELPARAM(windowSizeClient.x, windowSizeClient.y));
}

void WindowedMode::WindowApplyExternalRect(const RECT& bounds)
{
    if (!window || windowUpdating || resetInProgress || IsIconic(window) ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        return;

    RECT previousBounds{};
    const bool havePrevious = GetWindowRect(window, &previousBounds) != FALSE;
    const POINT previousClient = windowSizeClient;

    // Moving the HWND may synchronously generate WM_SIZE and WM_WINDOWPOSCHANGED.
    // Block those nested game callbacks and notify RenderWare only once below.
    windowUpdating = true;
    bool moved = true;
    if (!havePrevious || !EqualRect(&previousBounds, &bounds))
        moved = SetWindowPos(window, nullptr, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top,
            SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE) != FALSE;
    windowUpdating = false;
    if (!moved) return;

    windowPos = {bounds.left, bounds.top};
    windowSize = {bounds.right - bounds.left, bounds.bottom - bounds.top};
    if (windowMode != Fullscreen)
        windowPosWindowed = windowPos;
    else if (autoDesktopResolution)
        // Track the current monitor for the next switch back to windowed
        // mode, while keeping ResolutionX/Y = 0/0 on disk.
        windowSizeWindowed = windowSize;

    // Refresh the actual physical-pixel client size. The requested INI width
    // and height are intentionally not changed by a DPI or display event.
    WindowCalculateGeometry(false, false);
    RECT actualBounds{};
    if (GetWindowRect(window, &actualBounds))
        windowSize = {actualBounds.right - actualBounds.left,
                      actualBounds.bottom - actualBounds.top};
    WindowUpdateTitle();
    cursorClientRectValid = false;

    if (d3dDevice && ResolutionPolicy::ShouldNotifyRenderer(
            {previousClient.x, previousClient.y},
            {windowSizeClient.x, windowSizeClient.y}))
        CallWindowProcA(oriWindowProc, window, WM_SIZE, SIZE_RESTORED,
            MAKELPARAM(windowSizeClient.x, windowSizeClient.y));
    // Persist the new monitor position, not the DPI-generated window size.
    if (windowMode != Fullscreen) SaveConfig();
    MouseUpdate(true);
}

void WindowedMode::WindowResize(POINT resolution)
{
    if (resolution.x < Resolution_Min.x || resolution.y < Resolution_Min.y) return;
    // Restore before assigning the requested size: restoring dispatches WM_SIZE.
    if (IsZoomed(window))
    {
        windowUpdating = true;
        ShowWindow(window, SW_RESTORE);
        windowUpdating = false;
    }
    if (windowMode == WindowMode::Fullscreen)
        windowMode = windowedStyle;

    windowSizeWindowed = resolution;
    autoDesktopResolution = false; // an explicit game menu resolution takes precedence
    WindowCalculateGeometry(true, true); // and resize the window
    SaveConfig();
}

void WindowedMode::WindowToggleFullscreen()
{
    if (IsIconic(window) || !HasFocus(window)) return;
    const bool leaving = windowMode == Fullscreen;
    if (!leaving) windowedStyle = windowMode;
    if (IsZoomed(window))
    {
        windowUpdating = true;
        ShowWindow(window, SW_RESTORE);
        windowUpdating = false;
    }
    // Preserve the configured resolution. A 4K borderless window may look
    // identical to desktop fullscreen after Alt+Enter; that is intentional.
    windowMode = leaving ? windowedStyle : Fullscreen;
    WindowCalculateGeometry(false, true);
    SaveConfig();
}

void WindowedMode::WindowToggleStyle()
{
    if (IsIconic(window) || !HasFocus(window)) return;
    if (IsZoomed(window))
    {
        windowUpdating = true;
        ShowWindow(window, SW_RESTORE);
        windowUpdating = false;
    }
    windowedStyle = windowedStyle == Windowed ? WindowedBorderless : Windowed;
    windowMode = windowedStyle;
    WindowCalculateGeometry(false, true);
    SaveConfig();
}

void WindowedMode::WindowUpdateTitle()
{
    // Update the Win32 caption only when its text actually changes. This
    // avoids WM_SETTEXT traffic at every FPS sampling interval.
    char nextTitle[sizeof(windowTitle)]{};
    const char* appName = rsGlobal->AppName ? rsGlobal->AppName : "Grand Theft Auto";
    if (windowMode == WindowMode::Windowed && HasFocus(window))
    {
        const int idx = FindAspectRatio(windowSizeClient);
        if (idx >= 0)
            _snprintf_s(nextTitle, _countof(nextTitle), _TRUNCATE, "%s | %ldx%ld (%s) @ %u fps", appName,
                windowSizeClient.x, windowSizeClient.y, AspectRatios[idx].name, fpsCounter.get());
        else
            _snprintf_s(nextTitle, _countof(nextTitle), _TRUNCATE, "%s | %ldx%ld @ %u fps", appName,
                windowSizeClient.x, windowSizeClient.y, fpsCounter.get());
    }
    else
        _snprintf_s(nextTitle, _countof(nextTitle), _TRUNCATE, "%s", appName);

    if (strcmp(windowTitle, nextTitle) != 0)
    {
        strcpy_s(windowTitle, nextTitle);
        if (window) SetWindowTextA(window, windowTitle);
    }
}

LRESULT APIENTRY WindowedMode::WindowProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) inst->window = wnd;
    if (msg == WM_WINDOWPOSCHANGED) inst->cursorClientRectValid = false;
    // Handle caption buffers in the same ANSI encoding as CreateWindowExA
    // and SetWindowTextA.
    if (msg == WM_SETTEXT || msg == WM_GETTEXT || msg == WM_GETTEXTLENGTH)
        return DefWindowProcA(wnd, msg, wParam, lParam);
    // The original games send this malformed notification after a camera resize
    // fails while maximized. Neither our handler nor DefWindowProc can consume it.
    if (msg == WM_WINDOWPOSCHANGED && !lParam) return 0;
    // Reset can synchronously dispatch activation and position messages as well
    // as WM_SIZE. Never re-enter game/rendering code through those callbacks.
    if (inst->resetInProgress && msg != WM_STYLECHANGING)
        return DefWindowProcA(wnd, msg, wParam, lParam);
    switch (msg)
    {
        // Per-monitor DPI transitions can change the non-client frame metrics.
        // Use the OS-suggested rectangle for ordinary windows, and the monitor's
        // physical bounds for desktop fullscreen; never persist a DPI-generated
        // client size as the user's requested INI resolution.
        case WM_DPICHANGED:
        {
            if (inst->windowUpdating || inst->resetInProgress || !lParam)
                return 0;
            RECT target = *reinterpret_cast<const RECT*>(lParam);
            if (inst->windowMode == Fullscreen)
            {
                const POINT center = {
                    target.left + (target.right - target.left) / 2,
                    target.top + (target.bottom - target.top) / 2
                };
                target = GetMonitorRect(center);
            }
            inst->WindowApplyExternalRect(target);
            return 0;
        }

        case WM_DISPLAYCHANGE:
        {
            // A fullscreen window follows desktop resolution changes. Ignore
            // duplicates to avoid unnecessary GPU resets on display events.
            if (!inst->windowUpdating && !inst->resetInProgress &&
                inst->windowMode == Fullscreen && !IsIconic(wnd))
            {
                RECT current{};
                if (GetWindowRect(wnd, &current))
                {
                    const POINT center = {
                        current.left + (current.right - current.left) / 2,
                        current.top + (current.bottom - current.top) / 2
                    };
                    const RECT target = GetMonitorRect(center);
                    if (!EqualRect(&current, &target))
                        inst->WindowApplyExternalRect(target);
                }
            }
            break; // preserve the game's own WM_DISPLAYCHANGE handler
        }

        // window focus/defocus
        case WM_ACTIVATE:
        {
            auto result = (LOWORD(wParam) == WA_INACTIVE) ?
                DefWindowProcA(wnd, msg, wParam, lParam) : // don't pause game on defocus
                CallWindowProcA(inst->oriWindowProc, wnd, msg, wParam, lParam);

            // handle automatic pause/resume
            if (inst->gameState == Playing_Game)
            {
                switch(LOWORD(wParam))
                {
                    case WA_INACTIVE:
                        if (inst->autoPause && !inst->IsMainMenuVisible())
                        {
                            inst->SwitchMainMenu(true);
                            inst->autoPauseExecuted = true;
                        }
                        break;

                    case WA_CLICKACTIVE: // mouse click
                        if (!IsCursorInClientRect(wnd))
                        {
                            inst->autoPauseExecuted = false;
                            break; // user clicked on the window caption or edge
                        }
                        [[fallthrough]];

                    case WA_ACTIVE:
                        // Never close a menu the player opened manually.
                        if (inst->autoResume && inst->autoPauseExecuted &&
                            inst->IsMainMenuVisible()) // TODO: check if not in some submenu
                        {
                            inst->autoPauseExecuted = false;
                            inst->SwitchMainMenu(false);
                        }
                        break;
                }
            }

            inst->WindowUpdateTitle();
            inst->MouseUpdate(true);
            return result;
        }

        // don't pause game on defocus
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            inst->enterShortcutDown = false;
            return DefWindowProcA(wnd, msg, wParam, lParam);
        
        // restore proper handling of ShowCursor
        case WM_SETCURSOR:
            return DefWindowProcA(wnd, msg, wParam, lParam);

        // do not send keyboard events to the inactive window
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            if (!HasFocus(wnd))
                return DefWindowProcA(wnd, msg, wParam, lParam); // bypass the game
            
            // handle Alt+Enter and Ctrl+Enter key combinations
            const bool alt = (lParam & (1L << 29)) != 0;
            const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (wParam == VK_RETURN && (alt || control || inst->enterShortcutDown))
            {
                if (!(lParam & (1L << 30)) && !inst->enterShortcutDown)
                {
                    if (alt) inst->WindowToggleFullscreen();
                    else if (control) inst->WindowToggleStyle();
                }
                inst->enterShortcutDown = true;
                return 0;
            }

            break;
        }
        case WM_SYSKEYUP:
        case WM_KEYUP:
            if (wParam == VK_RETURN && inst->enterShortcutDown)
            {
                inst->enterShortcutDown = false;
                return 0;
            }
            break;

        // handle the window menu Alt+Key hotkey messages
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU)
                return S_OK; // handled
            break;

        // do not send mouse events to the inactive window
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
        case WM_MOUSEACTIVATE:
        case WM_MOUSEHOVER:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        {
            const bool hasFocus = HasFocus(wnd);
            // Ordinary client mouse messages already contain client coordinates.
            // Avoid GetCursorPos + GetClientRect + two ClientToScreen calls for
            // each mouse-move packet, especially on high-polling-rate mice.
            bool inClient;
            if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEACTIVATE)
                inClient = IsCursorInClientRect(wnd);
            else
            {
                const int x = static_cast<short>(LOWORD(lParam));
                const int y = static_cast<short>(HIWORD(lParam));
                inClient = x >= 0 && y >= 0 &&
                    x < inst->windowSizeClient.x && y < inst->windowSizeClient.y;
            }

            if (!hasFocus && inClient)
            {
                static const HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
                SetCursor(arrow);
            }

            if (!hasFocus || !inClient)
            {
                return DefWindowProcA(wnd, msg, wParam, lParam); // bypass the game
            }
            break;
        }

        case WM_STYLECHANGING:
        {
            auto styles = (STYLESTRUCT*)lParam;
            
            if (wParam == GWL_STYLE)
            {
                auto mask = WS_MINIMIZE | WS_MAXIMIZE;
                styles->styleNew &= mask;
                styles->styleNew |= inst->WindowStyle();
            }
            else if (wParam == GWL_EXSTYLE)
                styles->styleNew = inst->WindowStyleEx();
                
            break;
        }

        // user dragging the window edge
        case WM_ENTERSIZEMOVE:
            inst->sizing = true;
            inst->sizeBeforeUserResize = inst->windowSizeClient;
            ClipCursor(NULL);
            return 0;

        case WM_GETMINMAXINFO:
        {
            auto limits = (MINMAXINFO*)lParam;
            limits->ptMinTrackSize = inst->SizeFromClient(Resolution_Min);
            return 0;
        }

        case WM_SIZING:
        {
            auto wndRect = (RECT*)lParam;
            auto size = inst->ClientFromSize({
                wndRect->right - wndRect->left,
                wndRect->bottom - wndRect->top
            });

            // minimal game resolution
            size.x = max(size.x, Resolution_Min.x);
            size.y = max(size.y, Resolution_Min.y);

            // snap to known aspect ratios
            auto idx = FindAspectRatio(size, 0.02f);
            if (idx != -1)
            {
                auto currAspect = float(size.x) / size.y;

                switch(wParam)
                {
                    case WMSZ_LEFT:
                    case WMSZ_RIGHT:
                        size.x = LONG(size.y * AspectRatios[idx].ratio);
                        break;

                    case WMSZ_TOP:
                    case WMSZ_BOTTOM:
                        size.y = LONG(size.x / AspectRatios[idx].ratio);
                        break;

                    default: // sizing both X and Y
                    {
                        if (currAspect < AspectRatios[idx].ratio)
                            size.x = LONG(size.y * AspectRatios[idx].ratio);
                        else
                            size.y = LONG(size.x / AspectRatios[idx].ratio);
                    }
                }
            }

            // update window title immediately
            inst->windowSizeClient.x = size.x;
            inst->windowSizeClient.y = size.y;
            inst->WindowUpdateTitle();

            // apply modified window size
            size = inst->SizeFromClient(size);
            if (wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT) wndRect->left = wndRect->right - size.x;
            if (wParam == WMSZ_RIGHT || wParam == WMSZ_TOPRIGHT || wParam == WMSZ_BOTTOMRIGHT) wndRect->right = wndRect->left + size.x;
            if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT) wndRect->top = wndRect->bottom - size.y;
            if (wParam == WMSZ_BOTTOM || wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_BOTTOMRIGHT) wndRect->bottom = wndRect->top + size.y;

            return TRUE;
        }

        case WM_EXITSIZEMOVE:
            inst->sizing = false;
            if (inst->windowSizeClient.x != inst->sizeBeforeUserResize.x ||
                inst->windowSizeClient.y != inst->sizeBeforeUserResize.y)
                inst->autoDesktopResolution = false;
            inst->WindowCalculateGeometry(false, true);
            inst->SaveConfig();
            inst->MouseUpdate(true);
            return 0;

        // minimize, maximize, restore
        case WM_SIZE:
            if (!inst->windowUpdating && !inst->resetInProgress && !inst->sizing &&
                wParam != SIZE_MINIMIZED && wParam != SIZE_MAXHIDE && LOWORD(lParam) && HIWORD(lParam))
                CallWindowProcA(inst->oriWindowProc, wnd, msg, wParam, lParam); // inform the game
            return DefWindowProcA(wnd, msg, wParam, lParam); // call default as otherwise maximization will not work correctly on later Windows versions

        // position or size changed
        case WM_WINDOWPOSCHANGED:
        {
            if (inst->windowUpdating || inst->resetInProgress || IsIconic(wnd))
                return DefWindowProcA(wnd, msg, wParam, lParam);

            bool updated = false;
            bool sizeChanged = false;
            auto info = (WINDOWPOS*)lParam;

            // correct modern Windows styles invisible border
            RECT padding = inst->GetFrameSize(true);
            POINT pos = { info->x + padding.left, info->y + padding.top };
            if ((info->flags & SWP_NOMOVE) == 0)
            {
                if (pos.x != inst->windowPos.x || pos.y != inst->windowPos.y)
                {
                    inst->windowPos = pos;
                    updated = true;
                }
            }

            if ((info->flags & SWP_NOSIZE) == 0)
            {
                if (info->cx != inst->windowSize.x || info->cy != inst->windowSize.y)
                {
                    inst->windowSize = { info->cx, info->cy };
                    sizeChanged = true;
                    updated = true;
                }
            }

            if (updated)
            {
                RECT client;
                if (!GetClientRect(wnd, &client) || client.right <= 0 || client.bottom <= 0)
                    return DefWindowProcA(wnd, msg, wParam, lParam);
                inst->windowSizeClient = { client.right, client.bottom };
                if (inst->windowMode != WindowMode::Fullscreen && !IsZoomed(wnd))
                {
                    inst->windowPosWindowed = inst->windowPos;
                    if (sizeChanged)
                        inst->windowSizeWindowed = inst->windowSizeClient;
                }
                inst->WindowCalculateGeometry();
                inst->WindowUpdateTitle();
                if (!inst->sizing) inst->SaveConfig();
            }

            // DefWindowProc generates WM_SIZE/WM_MOVE. The game's handler must not
            // run a second time or resize the camera while a Reset is in progress.
            return DefWindowProcA(wnd, msg, wParam, lParam);
        }
    }

    return CallWindowProcA(inst->oriWindowProc, wnd, msg, wParam, lParam);
}

POINT WindowedMode::SizeFromClient(POINT clientSize) const
{
    auto frame = GetFrameSize();
    clientSize.x += frame.left + frame.right;
    clientSize.y += frame.top + frame.bottom;
    return clientSize;
}

POINT WindowedMode::ClientFromSize(POINT windowSize) const
{
    auto frame = GetFrameSize();
    windowSize.x -= frame.left + frame.right;
    windowSize.y -= frame.top + frame.bottom;
    return windowSize;
}

RECT WindowedMode::GetFrameSize(bool padOnly) const
{
    RECT frame = { 0 };

    if (padOnly)
    {
        RECT base, extended;
        if (GetWindowRect(window, &base) &&
            SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &extended, sizeof(RECT))))
        {
            frame.left = extended.left - base.left;
            frame.top = extended.top - base.top;
            frame.right = base.right - extended.right;
            frame.bottom = base.bottom - extended.bottom;
        }
    }
    else
    {
        using GetDpi = UINT(WINAPI*)(HWND);
        using AdjustForDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        static const auto user32 = GetModuleHandleA("user32.dll");
        static const auto getDpi = reinterpret_cast<GetDpi>(GetProcAddress(user32, "GetDpiForWindow"));
        static const auto adjustForDpi = reinterpret_cast<AdjustForDpi>(GetProcAddress(user32, "AdjustWindowRectExForDpi"));
        if (!window || !getDpi || !adjustForDpi ||
            !adjustForDpi(&frame, WindowStyle(), false, WindowStyleEx(), getDpi(window)))
            AdjustWindowRectEx(&frame, WindowStyle(), false, WindowStyleEx());
        frame.left *= -1; // offset to thickness
        frame.top *= -1; // offset to thickness
    }

    return frame;
}

void WindowedMode::MouseUpdate(bool force)
{
    if (!window) return;

    const bool focused = HasFocus(window);
    const DWORD now = GetTickCount();
    // Reassert state occasionally because other mods / the game can change
    // ShowCursor or ClipCursor without informing us.
    constexpr DWORD cursorResyncIntervalMs = 250;
    const bool resync = force || !cursorSyncValid ||
        (now - lastCursorSyncTime >= cursorResyncIntervalMs);
    if (resync) lastCursorSyncTime = now;

    if (!focused)
    {
        if (resync || !cursorShown) SetCursorVisible(true);
        if (resync || cursorClipped) ClipCursor(nullptr);
        cursorShown = true;
        cursorClipped = false;
        cursorSyncValid = true;
        return; // No rectangle/mouse polling while the window is inactive.
    }

    if (!cursorClientRectValid || force)
    {
        RECT rect{};
        if (!GetClientRect(window, &rect)) return;
        // Convert both corners in one Win32 call without aliasing RECT as POINT.
        POINT corners[2] = {{rect.left, rect.top}, {rect.right, rect.bottom}};
        MapWindowPoints(window, nullptr, corners, 2);
        cursorClientRect = {corners[0].x, corners[0].y,
            corners[1].x, corners[1].y};
        cursorClientRectValid = true;
    }

    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;
    const bool inside = !sizing && PtInRect(&cursorClientRect, cursor);
    const bool showCursor = !inside;
    const bool shouldClip = !sizing && !IsMainMenuVisible();

    if (resync || !cursorSyncValid || showCursor != cursorShown)
        SetCursorVisible(showCursor);

    if (resync || !cursorSyncValid || shouldClip != cursorClipped ||
        (shouldClip && !EqualRect(&cursorClipRect, &cursorClientRect)))
    {
        ClipCursor(shouldClip ? &cursorClientRect : nullptr);
    }

    cursorShown = showCursor;
    cursorClipped = shouldClip;
    if (shouldClip) cursorClipRect = cursorClientRect;
    cursorSyncValid = true;
}
