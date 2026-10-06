# Vady ručně kovaného hřebíku

Balík obsahuje osm viditelných výrobních vad pro hodnocení hřebíku v kovářské minihře.

- Rozměr atlasu: `2048 × 1024 px`
- Rozložení: `4 × 2`
- Rozměr buňky: `512 × 512 px`
- Formát: PNG RGBA
- Pivot každé fáze: střed buňky `256 × 256`
- Pořadí: zleva doprava, nejprve horní a poté spodní řada

## Vady

1. `crooked_shank` – křivý dřík po nerovnoměrných úderech
2. `blunt_point` – nedostatečně vykovaná tupá špička
3. `point_off_axis` – špička vychýlená mimo osu
4. `undersized_head` – příliš malá a nedokovaná hlava
5. `head_off_center` – hlava posunutá mimo osu dříku
6. `neck_crack` – trhlina u krčku po práci za příliš nízké teploty
7. `burned_iron` – přepálené, okujené a materiálově oslabené železo
8. `overdrawn_shank` – příliš vytažený a zeslabený dřík

Soubor `nail_defects.json` obsahuje výřezy, pivoty, závažnost, pravděpodobnou příčinu a očekávaný způsob selhání. Hodnoty závažnosti jsou návrhové a lze je při vyvažování změnit bez úpravy grafiky.

## Debug příkaz ve hře

- `smithing defects` vypíše všechny vady včetně technického ID, českého názvu, popisu a cesty k obrázku.
- `smithing defect crooked_shank` vygeneruje konkrétní vadu na aktivním hřebíku a otevře kontrolní stanici.
- `smithing defect random` vygeneruje náhodnou vadu na aktivním hřebíku.
