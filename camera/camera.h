// The 3D view: its rect during in-game cutscenes, and the camera's field of view on wide screens.
#pragma once

// The camera's get / set view rect functions are the expected version.
bool HasExpectedCameraRectCode();
void InstallCameraRectHooks();
// False when the game's FOV calls are not the expected code (FOV left alone).
bool InstallFovHooks();

// [FOV] scale: set, clamped to the slider's range, saved and applied at once.
void SetFovScale(float scale);
void StepFovScale(int notches);
// The camera sets its FOV again (after a change of [FOV] scale or mode).
void ReapplyCameraFov();
