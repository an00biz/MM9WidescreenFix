# Might & Magic IX UI & HUD Fix

<b>Disclaimer: this fix was created using generative AI.</b>

You can find the installation guide of this fix in the end of this `README` and in the `INSTALLATION.txt` file (in the archive file too).

<a href="https://github.com/an00biz/MM9WidescreenFix/blob/main/README_ru.md">Описание на русском здесь.</a>

## Introduction

The original game is locked to `800x600` resolution because HUD and UI coordinates are hardcoded and if you change the resolution manually in `autoexec.cfg`, which you can find in the game root folder, the UI is falling apart and the HUD stays at the center of the screen in it's original size:
<img width="1920" height="1080" alt="Screenshot0" src="https://github.com/user-attachments/assets/b276d504-1f7c-4a4d-a220-24c1a763ef2d" />
<img width="1920" height="1080" alt="Screenshot1" src="https://github.com/user-attachments/assets/cda086e8-6218-44c5-a38a-b62516c3b095" />

A side effect of this is vertical FOV distortion (`fovY` further) because game's default horizontal FOV (`fovX` further) = `90°` and `fovY = fovX * ScreenHeight / ScreenWidth`, which with the `800x600` resoluton gives a `67.5°` angle (and it's already stretching the picture a little bit) and with the `1920x1080` resolution gives an angle equals to `50.625°`, which makes the horizontal visibility the same and reduces vertical by `16.875°`:
<img width="1920" height="1080" alt="ezgif com-optimize (1)" src="https://github.com/user-attachments/assets/be111139-c1ac-4afb-bc13-51c93a94da26" />

## What this fix does

### Screen resolution and UI and HUD are now tied to relative coordinates instead of hardcoded ones

- Now you can change the game's resolution in the settings. In `Options` -> `Video Options` there's a new `Resolution` window, which gets a list of every supported resolutions from game's renderer `d3d.ren` (which gets it from DirectX). HUD and UI should now get it's size and location according to the resolution:
<img width="1920" height="1080" alt="Screenshot3" src="https://github.com/user-attachments/assets/0553d7b7-eba6-4830-8fcc-74c1c240e2c8" />
<img width="1920" height="1080" alt="ezgif com-optimize" src="https://github.com/user-attachments/assets/ae424153-858a-4d0b-8115-2b20c2f5f202" />

- You can scale HUD with a `LCtrl + MouseWheel (Up/Down)` combination while playing the game. `LCtrl + MouseWheel (click)` combination resets HUD scale to it's default value (max size). Minimum size — original HUD size, maximum one — according to the game's resolution:
<img width="800" height="450" alt="LithTech2026 11 04-14 19 59 01-ezgif com-optimize" src="https://github.com/user-attachments/assets/87287768-a9a3-45e4-952f-00f1f48c4b57" />

### Corrected FOV

- In `Options` -> `Game Options` you can now choose FOV type. You can select one from three: `Original`, `Original Fixed` and `Correct` (in the ini file called `original`, `hor+` and `correct` respectively).
  - `Original`: calculates FOV using game's original math;
  - `Original Fixed` (`hor+`): expands `fovX` according to the screen resolution, but keeps the `fovY` on `67.5°`;
  - `Correct`: calculates FOV using proper math, which removes the vertical stretching completely.
<img width="1920" height="1080" alt="Screenshot4" src="https://github.com/user-attachments/assets/cdeb0053-3011-4670-9480-1a83a76db252" />
<img width="1920" height="1080" alt="ezgif com-optimize" src="https://github.com/user-attachments/assets/98c37157-574b-48ce-81bc-0fd3046f7181" />

- On the screenshot above you can see the new slider bars that can regulate HUD scale and FOV:
  - First one does the same thing `LCtrl + MouseWheel (Up/Down)` combination does while playing. Double click on the slider sets it back to the default value (max size);
  - Second one lets you change FOV. Double click on the slider sets it back to the default value. You can change FOV with the `LAlt + MouseWheel (Up/Down)` combination while playing the game. The `LAlt + MouseWheel (click)` combination sets it back to the default value:
<img width="800" height="450" alt="ezgif com-optimize" src="https://github.com/user-attachments/assets/c4cd1faf-4dde-4d65-a38b-d42d5fc44366" />

### Miscellaneous

In `Option -> Sound Options` the `Master Volume` slider now remembers the value it's set and not resetting it every time you enter the game.

### Additional settings

In addition to the settings listed above, the `cshell.dll` file also includes a `cshell_hud.ini` file with additional settings. Every setting have it's own description.
Example: the "mode" setting in the `[UI]` section. `mode=fit` preserves the `4:3` aspect ratio of the UI and adjusts it to fit the screen height, while `mode=stretched` stretches the UI to fill the entire screen.
<img width="1920" height="1080" alt="ezgif com-optimize" src="https://github.com/user-attachments/assets/cb8ba4fd-d8a9-4c5c-a272-e72cddc4d1c0" />

## Wrappers compatibility

You can use this fix with any wrapper. I've tested it on GOG's `ddraw.dll`, <a href="https://github.com/elishacloud/dxwrapper/">elishacloud's dxwrapper</a> and <a href="https://github.com/dege-diosg/dgVoodoo2">dgVoodoo2</a>. It works on every one of them.

## Installation

1. In your root game folder, go to `/data` folder, find and rename file `cshell.dll` to `cshell_orig.dll`;
2. Put `cshell.dll` and `cshell_hud.ini` from the archive in the same folder;
3. Play the game :)