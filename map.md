# Project map

`cshell_hud` is a wrapper for Might & Magic IX's `cshell.dll`. It loads the original as `cshell_orig.dll`, forwards its three exports, and patches the original in memory, so the game's 800x600 interface is scaled and anchored on any screen size.

Build: `build.bat` in an x86 Developer Command Prompt. Includes are written from the project root (`#include "core/log.h"`).

## Folders

| Folder | Job |
|---|---|
| `core/` | Loading the original, the install order, addresses, memory patching, the log, shared geometry |
| `settings/` | `cshell_hud.ini`: reading it (live), saving the values the game changes, key bindings |
| `layout/` | Where the 800x600 design space lands on the screen, and the way back for mouse points |
| `rendering/` | Hooks on the engine's 2D render object: every draw onto the screen is mapped |
| `ui/` | Hooks on the game's windows: the window tree, the HUD, aligned windows, the cursor, the party creation fix |
| `input/` | Key bindings (message hook) and mouse points converted in the window proc |
| `camera/` | The 3D view rect in cutscenes, and the field of view on wide screens |
| `media/` | Bink videos (aspect kept) and the master volume at start |
| `options/` | Extra controls on Options -> Game Options and Options -> Video Options |
| `diagnostics/` | Troubleshooting logs: HUD dump, draw timings, mouse traces, crashes |

## Files

### core/
| File | Contents |
|---|---|
| `cshell_hud.cpp/.h` | DLL entry. It finds and loads `cshell_orig.dll`, forwards the exports, and provides `LoadedAddress()` / `LinkedAddress()` and the folder for the ini and log. The project description is in its header comment. |
| `install.cpp/.h` | Checks that the original is the expected version, then installs every module in order. |
| `addresses.h` | Every address in the original (as linked, base `0x10000000`) and in `lithtech.exe`, plus vtable slot offsets and the `RenderMode` record. |
| `patching.cpp/.h` | Vtable and import slots, jumps and calls, byte checks, `RETURN_ADDRESS()`. |
| `log.cpp/.h` | `Log()` writes to `cshell_hud.log`. |
| `geometry.h` | `Rect`, `Mapping`, `Round`, `Clamp`, `Union`. |

### settings/
| File | Contents |
|---|---|
| `config.cpp/.h` | The `Config` struct (all ini settings), `LoadConfig()`, `SaveHudSize/FovScale/FovMode()`, alignment rule lookup, `g_enabled`. |
| `key_bindings.cpp/.h` | Parses `[KEYS]` text such as `LCtrl+Wheel`, and matches a binding against input messages. |

### layout/
| File | Contents |
|---|---|
| `layout.cpp/.h` | Screen size and scales, and the mapping in use for each draw (the 4:3 block, a forced mapping, the current alignment). Also `MapDesignRect()`, the cursor clamp rect, HUD size control, and the mapping used for each mouse point. Holds the scoped state helpers `ScopedDrawMapping`, `ScopedAlignment` and `ScopedHudWindowDraw`. |
| `hud_zones.cpp/.h` | The HUD's 3x3 zones: which zone a piece belongs to, zone -> screen, screen point -> HUD point, and the mapping for windows that follow the HUD size. |

### rendering/
| File | Contents |
|---|---|
| `renderer.cpp/.h` | The render object's unhooked functions (`g_unhookedRenderer`), plus the fix's own calls to them (`Renderer::Blit`, …), which the hooks let through. |
| `render_hooks.cpp/.h` | Hooks for Blit, Scale, Fill, Create, Delete, GetScreenRect and GetSize, plus solid-color surfaces, the loading-screen redraw and `UpdateBeforeDraw()`. |
| `driver_state.cpp/.h` | The engine's driver table and the original d3d.ren's state (`GetDrawState()`), and `ScopedSceneDraw`, which draws a picture made outside d3d.ren's Direct3D 2D mode inside it (`[UI] scene_draws`). |
| `screen_copies.cpp/.h` | Real-resolution copies of what the game copies from the screen (save-under, captures). |
| `black_bars.cpp/.h` | Detects full-screen menus from what a window tree draws, and paints the sides black. |

### ui/
| File | Contents |
|---|---|
| `window_tree.cpp/.h` | A re-implemented "draw all children" (places HUD pieces in their zones, writes the dump) and the control hit tests. |
| `window_rules.cpp/.h` | Draw hooks for the HUD (and its mouse handlers), for windows with an `[UI]` alignment rule, for windows following the HUD size, and for the mouse cursor. |
| `party_creation.cpp/.h` | A fix for a bug in the original game: on the party creation screen, clicking a portrait now selects that party member. The four invisible portrait buttons get the rects of the portraits shown. |

### input/
| File | Contents |
|---|---|
| `input.cpp/.h` | The message hook for the bindings, the patched window proc code (mouse point -> 800x600), the `ScreenToClient` import, and `g_mouse`. |

### camera/
| File | Contents |
|---|---|
| `camera.cpp/.h` | Cutscene view rects, the camera nudge, FOV conversion (hor+ / correct), and FOV scale control. |

### media/
| File | Contents |
|---|---|
| `video.cpp/.h` | Bink fullscreen blit in `lithtech.exe`, scaled with the aspect ratio kept and black bars around it. |
| `sound.cpp/.h` | Applies the saved master volume at start. |

### options/
| File | Contents |
|---|---|
| `options_controls.cpp/.h` | The game's own controls (label, slider, list box) and `BackgroundPainter`, which paints art into a screen's background picture. |
| `options_game.cpp/.h` | HUD Scale and FOV sliders and the FOV Type list; double-click on a slider resets it. |
| `options_video.cpp/.h` | Resolution list with a scroll bar; Apply switches the mode. |

### diagnostics/
| File | Contents |
|---|---|
| `diagnostics.cpp/.h` | `[HUD] dump` state, `PerfTimer` (`[HUD] perf`), the hit-trace window, cursor API logging, the crash logger, `DescribeAddress()`. |
| `screen_access.cpp/.h` | `[HUD] perf` on the driver side: times d3d.ren's LockScreen and BlitFromScreen (hooked in lithtech.exe's driver table) and logs the call chain of each. |

## Layers

The higher layers call into the ones below them, never the other way:

```
core, settings              addresses, patching, log, config: used by everything
layout                      pure mapping math over config and the screen state
rendering                   draw hooks: ask layout where things go
ui, input, camera, media    hooks on game objects: set layout state, draw through rendering
options                     extra screens built from the game's controls
diagnostics                 read-only view of everything, for the log
```

`core/install.cpp` is the only file that knows every module. It calls each module's `HasExpected…()` check and then its `Install…()`.

## How a frame flows

1. The game draws its window tree. `ui/window_tree` (`HookDrawChildren`) walks the children and tells `rendering/black_bars` a tree began.
2. **The HUD:** `ui/window_rules` (`HookHudDraw`) raises `g_hudDrawDepth`. Each leaf child gets its zone from `layout/hud_zones`.
3. **Windows with a rule:** an aligned window sets a `ScopedAlignment`. A window following the HUD size sets a `ScopedDrawMapping` and collects the screen area it draws.
4. **Each draw call:** a Blit, Scale or Fill on the screen reaches `rendering/render_hooks`, which calls `UpdateBeforeDraw()` (config reload, screen size) and `layout::MapDesignRect()`, then draws scaled. A draw the game makes outside d3d.ren's Direct3D 2D mode (the item popup does, between frames) is wrapped in that mode by `rendering/driver_state` (`ScopedSceneDraw`).
5. **Copies from the screen** go to `rendering/screen_copies` and are drawn back from there.
6. **End of the tree:** `rendering/black_bars` decides whether this was a full-screen menu and paints the bars.
7. **The cursor** is drawn by `ui/window_rules` (`DrawCursor`) around the real mouse point.

## How a mouse point flows

1. The window proc's patched code calls `input/ConvertWindowMouse`. It picks the mapping under the point with `layout::GetMouseMappingAt()`: a recently drawn HUD-following window, else the 4:3 block of the open aligned window. It writes the 800x600 point into the game's globals.
2. **HUD handlers** (`ui/window_rules`, `ConvertHudMouse`) convert the real point again with `layout/hud_zones::ScreenToHudPoint()`.
3. **Key bindings** go through `input/HookMessages` and are swallowed before the game sees them.

## Where to look

| Change | File |
|---|---|
| A new ini setting | `settings/config.h` (field), `settings/config.cpp` (read), `cshell_hud.ini` (document) |
| An address after a game update | `core/addresses.h`; the byte checks in each module's `HasExpected…()` |
| HUD zone borders, pinned pieces | `layout/hud_zones.cpp` (defaults in `settings/config.cpp`) |
| Menu block size or alignment | `layout/layout.cpp` (`GetBlockMapping`, `UpdateScreenSize`) |
| A slow or wrong draw | `rendering/render_hooks.cpp` (`DrawScaled`, `HookFill`), `rendering/driver_state.cpp` (draws outside the Direct3D 2D mode); `[HUD] perf=1` names what locks the screen or draws outside that mode (`diagnostics/screen_access.cpp`) |
| Trails or garbage behind dialogs | `rendering/screen_copies.cpp` |
| Grey bars beside menus | `rendering/black_bars.cpp` |
| FOV | `camera/camera.cpp` |
| Layout of the Options screens | the constants at the top of `options/options_game.cpp` and `options/options_video.cpp` |
| A screen's own bug (a control that ignores clicks) | the screen's module in `ui/`, like `ui/party_creation.cpp`; find the class through the source-file strings in the original |
| A crash | `cshell_hud.log` (crash logger in `diagnostics/`); `[HUD] dump=1` / `mouse_log=1` / `perf=1` |
