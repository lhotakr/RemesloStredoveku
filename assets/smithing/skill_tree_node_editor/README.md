# Kovářství — strom dovedností pro ImGui Node Editor

Hotová grafická a datová sada pro herní strom kovářství v projektu
**Řemeslo středověku**. Plátno využívá `thedmd/imgui-node-editor` pro posun,
přiblížení, výběr uzlu a Bézierovy spoje. Samotný vzhled uzlů, pinů a stavů je
vlastní a navazuje na stávající kovářské ImGui UI.

## Navržená progrese

`Pozorovatel → Učeň → Tovaryš → specializace → Mistr → finální větev`

Specialista se větví na **Nástrojařství**, **Podkovářství** a
**Stavební kování**. Z Mistra se později odhalí volba **Inovátor** nebo
**Mistr dílny**. Před odhalením nemají finální uzly jméno ani zřetelný obrys.

Postup se nekupuje za abstraktní body. JSON počítá s důkazy z pozorování,
skutečné práce, chyb a výsledku výrobku nebo opravy.

## Obsah balíčku

- 57 samostatných průhledných PNG;
- atlas `2048 × 2048` se čtyřpixelovým odstupem;
- sedm stavů uzlu: hotový, aktivní, dostupný, blízký zámek, vzdálený zámek,
  tajný a vybraný;
- pět stavů pinů, odznaky, ukazatele postupu a dvě intenzity mlhy;
- 36 piktogramů hodností, specializací a konkrétních dovedností;
- přesná data stromu, ID uzlů/pinů/spojů, české prototypové texty i
  lokalizační klíče;
- vzorový renderer pro `imgui-node-editor` a vygenerované C++ souřadnice atlasu;
- dva kontrolní náhledy a validační skript.

## Doporučené napojení

1. Načti `assets/atlases/smithing_skill_tree_atlas_2048.png` jako jednu RGBA
   texturu s lineárním filtrováním a režimem `clamp`.
2. Přidej do projektu zdroje `imgui-node-editor` a soubory z `integration/`.
3. Jednou zavolej `SmithingSkillTree::Initialize`, každý snímek `DrawTree` a
   vedle něj běžný ImGui panel `DrawInspector`.
4. V produkční verzi načti uzly a texty z `smithing_skill_tree.json`; pole
   `title_cs` je prototyp, `title_key` je určeno pro lokalizaci.
5. Při změně stavu hráče přepni `state`, `progress` a viditelnost finálních
   uzlů. Spoje používej jako runtime kresbu, nikoli jako obrázky.

Rozložení se během každého snímku znovu nastavuje. Hráč proto může plátno
posouvat a přibližovat, ale nemůže měnit význam stromu přetahováním uzlů.
Inspektor je úmyslně mimo node canvas; uvnitř uzlů nejsou child windows ani
víceřádkové editory.

## Struktura

```text
assets/
  atlases/   provozní atlas
  nodes/     stavové rámy uzlů
  icons/     hodnosti, specializace a milníky
  pins/      přípojné body
  badges/    hotovo, zámek, tajemství, volba
  progress/  průběh
  overlays/  mlha
integration/
  smithing_skill_tree.json
  smithing_skill_tree_assets.json
  SmithingSkillTreeAtlas.generated.h
  SmithingSkillTreeNodeEditor.h/.cpp
  validation.json
preview/
  smithing_skill_tree_node_editor_mockup.png
  smithing_skill_tree_assets_overview.png
```

Runtime PNG neobsahují text. Veškerou češtinu, velikost písma a lokalizaci
kreslí ImGui. Celou sadu lze deterministicky obnovit příkazem:

```bash
python3 build_skill_tree_assets.py
python3 validate_assets.py
```
