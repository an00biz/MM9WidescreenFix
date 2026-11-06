# Might & Magic IX UI & HUD Fix

<b>Дисклеймер: данный фикс был создан с помощью генеративного ИИ.</b>

Инструкция по установке данного фикса находится в конце данного `README`, а также в файле `INSTALLATION.txt` (также есть в архиве).

## Введение

Оригинальная игра залочена на разрешении `800x600`, т.к. координаты интерфейсов привязаны не к разрешению экрана, а жёстко закодированы, и если поменять разрешение вручную через `autoexec.cfg`, что находится в корневом каталоге игры, весь UI "едет", а HUD остаётся по центру экрана в его оригинальном размере:
<img width="1920" height="1080" alt="Screenshot0" src="https://github.com/user-attachments/assets/b276d504-1f7c-4a4d-a220-24c1a763ef2d" />
<img width="1920" height="1080" alt="Screenshot1" src="https://github.com/user-attachments/assets/cda086e8-6218-44c5-a38a-b62516c3b095" />

Также побочным эффектом данного изменения является искажение вертикального FOV (далее `fovY`), ибо по умолчанию в игре горизонтальный FOV (далее `fovX`) = `90°`, а `fovY = fovX * ScreenHeight / ScreenWidth`, что при разрешении `800x600` даёт угол в `67.5°` (что даже так уже немного растягивает картинку), а при разрешении `1920x1080` даёт угол `50.625°`, из-за чего по горизонтали видимость абсолютно такая же, а по вертикали меньше ещё на `16.875°`:
<img width="1920" height="1080" alt="ezgif com-optimize (1)" src="https://github.com/user-attachments/assets/be111139-c1ac-4afb-bc13-51c93a94da26" />

## Что делает этот фикс

### Разрешение экрана и привязка UI и HUD к относительным координатам вместо абсолютных

- Теперь вы можете менять разрешение экрана прямо в настройках игры. В разделе `Options` -> `Video Options` добавлено отдельное окно `Resolution`, которое подтягивает все возможные поддерживаемые разрешения экрана из рендерера игры `d3d.ren` (а он в свою очередь от DirectX), а HUD и UI теперь будут менять свой размер и местоположение в соответствии с разрешением:
<img width="1920" height="1080" alt="Screenshot3" src="https://github.com/user-attachments/assets/0553d7b7-eba6-4830-8fcc-74c1c240e2c8" />
<img width="1920" height="1080" alt="ezgif com-optimize" src="https://github.com/user-attachments/assets/ae424153-858a-4d0b-8115-2b20c2f5f202" />

- Вы можете менять размер HUD комбинацией `LCtrl + MouseWheel (Up/Down)` во время игры. Комбинация `LCtrl + MouseWheel (клик)` возвращает размер HUD на значение по умолчанию (максимальный размер). Минимальный размер — оригинальный размер HUD, максимальный — в соответствии с разрешением экрана:
<img width="800" height="450" alt="LithTech2026 11 04-14 19 59 01-ezgif com-optimize" src="https://github.com/user-attachments/assets/87287768-a9a3-45e4-952f-00f1f48c4b57" />

### Исправленный FOV

- В настройках `Options` -> `Game Options` теперь можно выбрать тип FOV. На выбор даётся три типа: `Original`, `Original Fixed` и `Correct` (в файле ini зовутся `original`, `hor+` и `correct` соответственно).
  - `Original`: рассчитывает FOV по оригинальным формулам игры;
  - `Original Fixed` (`hor+`): расширяет `fovX` в соответствии с разрешением экрана, но сохраняет `fovY` на значении `67.5°`;
  - `Correct`: рассчитывает FOV по правильным формулам, из-за чего растянутость изображения по вертикали полностью устраняется.
<img width="1920" height="1080" alt="Screenshot4" src="https://github.com/user-attachments/assets/cdeb0053-3011-4670-9480-1a83a76db252" />
<img width="1920" height="1080" alt="ezgif com-optimize" src="https://github.com/user-attachments/assets/98c37157-574b-48ce-81bc-0fd3046f7181" />

- На скриншоте выше также видно новые ползунки, отвечающие за размер HUD и FOV:
  - Первый делает то же, что и комбинация `LCtrl + MouseWheel (Up/Down)` во время игры. Двойное нажатие на ползунок возвращает его на значение по умолчанию (максимальный размер);
  - Второй позволяет менять FOV. Двойное нажатие на ползунок возвращает его на значение по умолчанию. Вы также можете менять FOV во время игры комбинацией `LAlt + MouseWheel (Up/Down)`. Комбинация `LAlt + MouseWheel (клик)` возвращает FOV на значение по умолчанию:
<img width="800" height="450" alt="ezgif com-optimize" src="https://github.com/user-attachments/assets/c4cd1faf-4dde-4d65-a38b-d42d5fc44366" />

### Прочее

Портреты игровых персонажей в меню создания отряда теперь кликабельны.
В разделе `Option -> Sound Options` ползунок `Master Volume` теперь запоминает установленное значение, а не сбрасывает его при каждом новом заходе в игру.

### Дополнительные настройки

Помимо настроек, представленных выше, к файлу `cshell.dll` также прилагается файл `cshell_hud.ini` с дополнительными настройками. К каждой такой настройке имеется описание.
Пример: `mode` в разделе `[UI]`. `mode=fit` сохраняет соотношение 4:3 у UI и подгоняет его под высоту экрана, `mode=stretched` растягивает UI по всему экрану.
<img width="1920" height="1080" alt="ezgif com-optimize" src="https://github.com/user-attachments/assets/cb8ba4fd-d8a9-4c5c-a272-e72cddc4d1c0" />

## Совместимость с врапперами

Этот фикс можно использовать вместе с любым враппером, будь то GOG'овский `ddraw.dll`, <a href="https://github.com/elishacloud/dxwrapper/">dxwrapper от elishacloud</a> или <a href="https://github.com/dege-diosg/dgVoodoo2">dgVoodoo2</a>. Проверено и работает на каждом.

## Установка

1. В корневом каталоге игры откройте папку `/data`, найдите и переименуйте файл `cshell.dll` в `cshell_orig.dll`;
2. Извлеките из архива `cshell.dll` и `cshell_hud.ini` и положите их в эту же папку;
3. Играйте :)