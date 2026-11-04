// Checks that cshell_orig.dll is the expected version and installs every part of the fix.
#pragma once

// Leaves g_enabled false when the original is unknown (nothing is patched then).
void InstallHooks();
