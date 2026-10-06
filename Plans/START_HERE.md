# Začátek integrace

## 1. Datový model řemesel

Načti `../catalog/crafts_master.json`. V něm jsou všechna řemesla, hodnosti,
důkazy zvládnutí, pracovní dráhy, jakost a skrytá větev. Herní texty jsou
česky; identifikátory oborů zůstávají stabilní anglické klíče pro kód a JSON.

## 2. Kovářství — doporučené pořadí

1. `assets/smithing/items` — devět předmětů a jejich výrobní/jakostní stavy.
2. `assets/smithing/environment_2_5d` — objekty, textury a rozvržení kovárny.
3. `assets/smithing/minigames` — šest pracovišť, ruce, nástroje, polotovary a efekty.
4. `assets/smithing/imgui` — společné prvky rozhraní a jeden provozní atlas.
5. `assets/smithing/skill_tree_node_editor` — strom, JSON a příklad napojení na imgui-node-editor.
6. `assets/smithing/ui_prototype` — obrazovkové návrhy jednotlivých fází práce.

## 3. Fog of War

Výchozí soubor je `assets/shared/fog_of_war/assets/overlays/FogOfWar.png`.
Je bezešvý v obou osách a odpovídá cestě, kterou už načítá CampaignInit.cpp.

## 4. Textury a atlasy

V běhu používej atlasy, pokud jsou v dané sadě přiložené. Samostatné PNG jsou
určené pro kontrolu, výměnu prvku a nástroje. Tím se sníží počet načtených
textur, což je pro současný engine důležité.

## 5. Závislosti příkladu stromu

- C++14 nebo novější;
- Dear ImGui;
- thedmd/imgui-node-editor;
- vlastní SDL/OpenGL/DirectX způsob načtení atlasu do `ImTextureID`.

Detail vybraného uzlu kresli mimo node canvas. Herní rozložení uzlů je pevné;
hráč smí plátno posouvat a přibližovat, ne přestavovat strom.
