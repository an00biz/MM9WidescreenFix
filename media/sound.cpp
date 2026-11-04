// Applies the saved master volume at game start (see sound.h).
#include "media/sound.h"
#include "core/addresses.h"
#include "settings/config.h"
#include "core/cshell_hud.h"
#include "core/log.h"

typedef float (__cdecl *GetVarFloatFn)(void* consoleVariable);
typedef void  (__cdecl *SetMasterVolumeFn)(int volume);

static int g_volumeAppliedStage = 0;

void ApplyMasterVolume(int stage)
{
    if (!g_enabled || !g_config.applyVolume || g_volumeAppliedStage >= stage) return;
    void* volumeVariable = *(void**)LoadedAddress(VA_MASTER_VOLUME_VAR);
    if (GetLtClient() == NULL || volumeVariable == NULL) return;

    float volume = Clamp(GetLtClientFunction<GetVarFloatFn>(LTCLIENT_GET_VAR_FLOAT)(volumeVariable), 0.0f, 100.0f);
    GetLtClientFunction<SetMasterVolumeFn>(LTCLIENT_SET_MASTER_VOLUME)((int)volume);
    g_volumeAppliedStage = stage;
    Log("sound: master volume %d from autoexec.cfg applied%s", (int)volume,
        stage == VOLUME_IN_GAME_WORLD ? " (game world)" : "");
}
