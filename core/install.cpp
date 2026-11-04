// Installs the fix in order: every byte and pointer the core patches touch is checked first,
// and an unknown cshell_orig.dll leaves the game untouched. The option screens, the FOV and the
// videos check their own code and are skipped alone when it differs.
#include "core/install.h"
#include "camera/camera.h"
#include "settings/config.h"
#include "core/cshell_hud.h"
#include "diagnostics/diagnostics.h"
#include "input/input.h"
#include "core/log.h"
#include "options/options_game.h"
#include "options/options_video.h"
#include "rendering/render_hooks.h"
#include "media/video.h"
#include "ui/window_rules.h"
#include "ui/window_tree.h"

static bool IsExpectedOriginal()
{
    return HasExpectedRendererCode() && HasExpectedWindowTreeCode() && HasExpectedMouseCode()
        && HasExpectedHudAndCursorCode() && HasExpectedCameraRectCode() && HasExpectedMovieCode();
}

static void InstallOptionsScreens()
{
    // The Video Options screen uses the controls the Game Options check verified.
    if (InstallGameOptions())
        InstallVideoOptions();
    else
        Log("video options: unexpected code - not changed");
}

void InstallHooks()
{
    if (!IsExpectedOriginal()) {
        Log("ERROR: cshell_orig.dll is not the expected version - interface fix disabled");
        g_enabled = false;
        return;
    }
    if (!g_enabled) {
        Log("disabled in cshell_hud.ini (enable=0)");
        return;
    }

    InstallRenderHooks();
    InstallHudHooks();
    InstallWindowTreeHooks();
    InstallCursorHooks();
    InstallCameraRectHooks();
    InstallVideoHook();
    InstallOptionsScreens();
    if (InstallFovHooks())        // always installed: [FOV] mode is read live
        Log("fov: hooks installed");
    else
        Log("fov: unexpected code - FOV left alone");
    InstallWindowRules();
    InstallMouseHooks();
    InstallCursorApiLogging();
    Log("hooks installed (cshell_orig.dll at %08lX)", g_originalBase);
}
