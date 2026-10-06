# Animace výroby ručně kovaného hřebíku – v1

První herně použitelný balík krátkých animací pro kovářskou výrobní smyčku. Grafika je rozdělena na samostatný kov, ruce s nástroji a částicové efekty, takže lze každý úder svázat se skutečnou změnou stavu výrobku.

## Obsah

- `hammer_strike_atlas_2048x1024_v1.png` – osm póz těžkého úderu
- `tongs_rotate_atlas_2048x1024_v1.png` – sevření a otočení polotovaru o čtvrt otáčky
- `nail_workpiece_deformation_atlas_2048x2048_v1.png` – šestnáct stavů od žhavé tyče po vychladlý hřebík
- `smithing_effects_animation_atlas_2048x2048_v1.png` – malé jiskry, silné jiskry, pára a okuje
- `frames` – všechny buňky samostatně jako PNG RGBA
- `previews` – statické přehledy a animované GIF náhledy
- `nail_forging_animations.json` – pořadí snímků, časování a události pro engine

Všechny atlasy používají buňky `512 × 512 px`, řazení po řádcích a pivot `256,256`.

## Důležitá událost dopadu

U animace `hammer_strike_heavy` je snímek `6` okamžik dopadu. V jednom herním kroku má:

1. postoupit stav deformace polotovaru,
2. vytvořit jiskry a okuje,
3. přehrát zvuk úderu,
4. spustit krátký otřes kovadliny.

Polotovar se nemá automaticky přehrávat jako běžná animace. Jeho buňka se mění pouze po platném úderu, otočení, oddělení přířezu nebo změně teploty.

## Doporučené vrstvy

1. prostředí a kovadlina,
2. polotovar hřebíku,
3. mezifázová vada,
4. kleště,
5. kladivo a ruka,
6. jiskry, okuje a pára,
7. uživatelské rozhraní.

Otřes kovadliny je definovaný v JSONu jako krátký posun o dva obrazové body a nevyžaduje další texturu.
