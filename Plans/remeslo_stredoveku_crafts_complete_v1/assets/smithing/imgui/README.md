# Řemeslo středověku – jednotlivé prvky UI pro ImGui

Sada převádí návrh rozhraní kovářských miniher na skutečné samostatné prvky.
Veškeré herní texty kreslí ImGui; do obrázků nejsou zapečené české názvy ani
klávesové zkratky. Stejná sada tedy může později obsloužit další jazyky a jiná
rozlišení.

## Obsah

- 63 samostatných provozních PNG a 36 samostatných řezů rámů;
- jeden atlas `2048 × 1024` pro úsporné načítání jediné textury;
- čtyři devítidílné rámy: pergamen, dřevo, železo a tmavý překryv;
- samostatných devět řezů každého rámu pro kontrolu nebo jednodušší vykreslovač;
- tlačítka `normal / hover / pressed / disabled`;
- záložky `normal / hover / active / completed / locked`;
- smyslové štítky, pozorovací karty a klávesové zkratky v několika stavech;
- šest ikon pracovišť a dvacet ikon pozorování či akcí;
- uzly výrobní fáze, stavové kosočtverce, zaměřovač a světelný překryv;
- JSON se souřadnicemi, UV a doporučenými velikostmi;
- vygenerované C++ konstanty a obecné pomocné funkce pro ImGui.

## Doporučené napojení

1. Do hry zkopíruj `assets/atlases/smithing_ui_atlas_2048.png`.
2. Načti jej jednou přes `IMG_LoadTexture` a zapni `SDL_BLENDMODE_BLEND`.
3. Pro běžné prvky používej UV souřadnice z
   `integration/SmithingUiAtlas.generated.h`.
4. Velké panely kresli pomocí `SmithingImGui::DrawNineSlice`; rohy a nýty tak
   zůstanou stále stejně velké.
5. Text a interakční plochu kresli až nad obrázkem. Pro kliknutí se hodí
   `InvisibleButton`, jak ukazuje `SmithingImGui::AtlasButton`.

Jednotlivá PNG jsou určena hlavně k prohlížení, ladění a případné výměně prvku.
V běžném sestavení doporučuji načíst pouze atlas, protože projekt už dříve
narazil na příliš mnoho textur.

## Důležité technické vlastnosti

- PNG používají přímý alfa kanál a průhledné okolí;
- atlas má mezi výřezy čtyřpixelový odstup;
- očekávané filtrování je lineární a ovinutí `clamp`;
- základní měřítko rozhraní je `1920 × 1080`, ale panely nejsou vázané na tuto
  velikost;
- devítidílné rámy mají zdrojový okraj `32 px` a nejmenší doporučenou velikost
  `96 × 96 px`;
- velikost textu ani české znaky nejsou součástí atlasu.

## Struktura

```text
assets/
  atlases/       jeden provozní atlas
  chrome/        rámy, medailon, oddělovač a devítidílné řezy
  controls/      tlačítka, záložky, karty, štítky a klávesy
  icons/         pracoviště a pozorované vlastnosti
  indicators/    fáze, stavy, zaměřovač a světelný překryv
integration/
  smithing_imgui_assets.json
  SmithingUiAtlas.generated.h
  SmithingImGuiSkin.h
  SmithingImGuiExample.cpp
preview/
  smithing_imgui_assets_overview.png
  smithing_imgui_runtime_preview.jpg
```

Skript `build_imgui_assets.py` dokáže celou sadu znovu deterministicky sestavit.
