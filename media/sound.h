// Master volume at game start. The game hands the saved MasterVolume (autoexec.cfg) to the sound
// system only when Apply is pressed in Sound Options, never at start: after a restart the slider
// shows the saved value but the sound plays at full volume.
#pragma once

enum VolumeStage { VOLUME_IN_MENUS = 1, VOLUME_IN_GAME_WORLD = 2 };

// Hands the saved volume over once per stage: in the menus (once the console variable exists),
// and again when the game world is first shown. [SOUND] apply_volume=0 turns it off.
void ApplyMasterVolume(int stage);
