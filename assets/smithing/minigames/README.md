# Řemeslo středověku – assety kovářských miniher v1

Balíček navazuje na návrh rozhraní `Remeslo_stredoveku_UI_kovarstvi_navrh_v1`.
Je určen pro pevnou kameru v pracovní ploše UI a lze jej použít ve vrstvené 2D
verzi i v pozdější hybridní variantě s 3D polotovarem.

## Obsah

- 6 pracovních pozadí 1536 × 768 px: výheň, kovadlina, káď, brus, ponk a zkouška;
- 8 průhledných vrstev rukou a nástrojů, jednotlivě 512 × 512 px;
- atlas rukou a nástrojů 2048 × 1024 px;
- 8 fází téhož polotovaru sekery, jednotlivě 512 × 512 px;
- atlas polotovaru 2048 × 1024 px;
- 8 průhledných efektů a jejich atlas 2048 × 1024 px;
- přesný JSON soupis souřadnic atlasů a doporučených kombinací.

## Doporučené vrstvení

1. pozadí pracoviště;
2. polotovar;
3. ruce a nástroj;
4. jiskry, pára, voda nebo třísky;
5. kódem kreslené obrysy, místa zásahu a další zpětná vazba;
6. společné UI.

Pozadí se má vejít do oblasti `[28, 156, 1464, 675]` návrhu 1920 × 1080.
Při jiném rozlišení se škáluje celá pracovní plocha se zachováním poměru stran.

## 2D prototyp

Pro první hratelnou verzi stačí mezi fázemi polotovaru krátce prolínat. Teplotu
lze měnit barevným násobením pouze na vrstvě polotovaru. Jiskry a kapky je lepší
rozmnožit jednoduchým částicovým systémem; dodané efekty slouží jako výrazný
zásah a výtvarná předloha.

## Pozdější 3D varianta

Není nutné převádět do 3D kovárnu ani rozhraní. Do pracovní plochy lze vykreslit
malou samostatnou 3D scénu a nahradit pouze vrstvu `workpiece`. Doporučení:

- pevná nebo ortografická kamera, aby se neměnila kompozice UI;
- světlo zleva v barvě výhně a slabé neutrální vyplnění;
- materiál polotovaru s maskou teploty po jednotlivých zónách;
- deformace nízkopolygonové mřížky pouze v okolí úderu;
- ruce, jiskry, pára i UI mohou zůstat 2D.

Tím vznikne hybridní minihra bez zásahu do 2.5D mapy, kolizí nebo přechodu mezi
venkovní mapou a interiérem kovárny.

## Technická poznámka

Generované atlasy měly šachovnici pouze namalovanou. Před zabalením byla
odstraněna, okraje byly znovu změkčeny a výsledná PNG mají skutečný alfa kanál.
Přesné pořadí a výřezy jsou v `integration/smithing_minigame_assets.json`.
