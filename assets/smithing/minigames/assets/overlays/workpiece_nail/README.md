# Pracovní fáze ručně kovaného hřebíku

Atlas obsahuje osm po sobě jdoucích fází výroby hřebíku pro kovářskou minihru.

- Rozměr atlasu: `2048 × 1024 px`
- Rozložení: `4 × 2`
- Rozměr buňky: `512 × 512 px`
- Formát: PNG RGBA
- Pořadí: zleva doprava, nejprve horní a poté spodní řada
- Orientace: hlava nebo zásobní část vlevo, pracovní konec a špička vpravo

## Fáze

1. `rod_cold` – studená hranatá železná tyč
2. `rod_heated` – zahřátý pracovní konec
3. `shank_drawn` – vytažený dřík
4. `point_forged` – vykovaná špička
5. `blank_cut` – oddělený polotovar
6. `head_partial` – rozpracovaná hlava
7. `nail_hot` – dokončený žhavý hřebík
8. `nail_finished` – vychladlý běžný hřebík

Soubor `nail_workpiece_stages.json` obsahuje zdrojové obdélníky, středové pivoty a příznak žhavého stavu.
