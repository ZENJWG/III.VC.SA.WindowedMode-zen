module;
#include "stdafx.h"
#include "Bindings.h"

export module WindowedMode;

export class WindowedMode
{
public:
    static constexpr POINT Resolution_Default = { 640, 448 }; // default GTA's PS2 resolution
    static constexpr POINT Resolution_Min = { Resolution_Default.x / 4, Resolution_Default.y / 4 };

    using GameTitle = ::GameTitle;

    // game internals
    const GameBindings bindings;
    const GameTitle gameTitle;
    GameState& gameState;
    union{ RsGlobalType* rsGlobal; RsGlobalTypeSA* rsGlobalSA; }; // different in SA
    WNDPROC oriWindowProc = nullptr;
    inline static decltype(&CreateWindowExA) createWindow = &CreateWindowExA;
    IDirect3DDevice8* d3dDevice = nullptr;
    union{ D3DPRESENT_PARAMETERS* d3dPresentParams8; D3DPRESENT_PARAMETERS_D3D9* d3dPresentParams9; }; // DirectX 8 or 9
    DisplayMode** rwVideoModes; // array
    DWORD (*RwEngineGetNumVideoModes)();
    DWORD (*RwEngineGetCurrentVideoMode)();

    std::vector<DisplayMode> videoModesBackup;
    DisplayMode* videoModesSource = nullptr;
    int modifiedVideoMode = -1;

    explicit WindowedMode(const GameBindings& bindings);

    static HWND __stdcall InitWindow(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
    void InstallRenderHooks();
    void BeforeReset();
    void AfterReset(HRESULT result);
    void BeforePresent();
    std::array<uint8_t, sizeof(D3DPRESENT_PARAMETERS_D3D9)> resetSaved{}, resetEffective{};

    // config file
    CIniReader config;
    void InitConfig(); // create default ini file
    bool LoadConfig(); // returns true if maximized
    void SaveConfig();
    void EnableDpiAwareness(); // before any game window is created
    void WriteResolutionDiagnostics(const char* event);
    bool highDpiAware = true;
    bool resolutionDiagnostics = false;
    bool firstFrameDiagnosticsPending = false;
    const char* dpiSetupResult = "not attempted";

    // game window
    struct AspectRatioInfo { const char* name; float ratio; };
    static const AspectRatioInfo AspectRatios[];
    static int FindAspectRatio(POINT resolution, float threshold = 0.007f);

    enum WindowMode : BYTE
    {
        Windowed = 1, WindowedBorderless, Fullscreen,
        Min = Windowed,
        Max = Fullscreen
    };
    
    HWND window = 0;
    bool windowUpdating = false; // update in progress
    bool resetInProgress = false;
    bool sizing = false;
    bool postEffectUpdatePending = false;
    WindowMode windowMode = WindowMode::Windowed;
    WindowMode windowedStyle = WindowMode::Windowed;
    bool enterShortcutDown = false;

    // Keep expensive cursor synchronization off the render hot path.
    RECT cursorClientRect{};
    RECT cursorClipRect{};
    bool cursorClientRectValid = false;
    bool cursorSyncValid = false;
    bool cursorShown = true;
    bool cursorClipped = false;
    DWORD lastCursorSyncTime = 0;

    // Skip disk writes when window-state notifications repeat identical values.
    std::array<int, 9> savedConfigState{};
    bool savedConfigStateValid = false;
    bool autoDesktopResolution = false; // ResolutionX/Y = 0/0, preserved on save
    
    
    char windowTitle[64]{};
    // current window geometry
    POINT windowPos = {};
    POINT windowSize = {};
    POINT windowSizeClient = {};
    // last properties of windowed mode
    POINT windowPosWindowed = { -1, -1 };
    POINT windowSizeWindowed = Resolution_Default;
    POINT sizeBeforeUserResize = {};

    void WindowCalculateGeometry(bool center = false, bool resizeWindow = false);
    void WindowApplyExternalRect(const RECT& bounds); // DPI/display changes, one renderer resize
    void WindowResize(POINT resolution);
    void ChangeResolution(DWORD modeIndex);
    void BackupVideoModes();
    void SyncActiveVideoMode();
    void InitPresentationParameters();
    static void __cdecl ChangeResolutionSA(DWORD modeIndex);
    static int __cdecl ChangeVideoModeSA(DWORD modeIndex);
    void WindowToggleFullscreen();
    void WindowToggleStyle();
    DWORD WindowStyle() const;
    DWORD WindowStyleEx() const;
    void WindowUpdateTitle();
    static LRESULT APIENTRY WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    POINT SizeFromClient(POINT clientSize) const;
    POINT ClientFromSize(POINT windowSize) const;
    RECT GetFrameSize(bool paddOnly = false) const; // size of window frame and extra padding/shadow introduced in later versions of Windows
    
    // DirectX 3D related stuff
    bool IsD3D9() const;

    // other
    FpsCounter fpsCounter;
    bool autoPause = true;
    bool autoResume = true;
    bool autoPauseExecuted = false;

    bool IsMainMenuVisible() const;
    void SwitchMainMenu(bool show);
    
    void MouseUpdate(bool force = false);
    void UpdatePostEffect();
    void UpdateWidescreenFix();
};

export inline WindowedMode* inst = nullptr;

