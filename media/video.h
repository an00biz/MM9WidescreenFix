// Pre-rendered (Bink) videos: shown full screen, scaled with their aspect ratio kept ([VIDEO] mode).
#pragma once

// The movie start's fullscreen check in the original is the expected version.
bool HasExpectedMovieCode();
// Patches lithtech.exe's Bink blit (when it is the expected version) unless [VIDEO] mode=original.
void InstallVideoHook();
