# Navrh assetu pro vady rucne kovaneho hrebiku

## Průzkum aktuálního stavu

Současný `workpiece_nail` atlas je správně fáziovaný: osm buněk představuje
postup od studené tyče po vychladlý hotový hřebík. Současný `defects_nail`
atlas je ale spíše atlas finálních kontrolních vad. Když se jeho buňka
`burned_iron` použije už během zahřívání nebo vytahování, obraz vypadá jako
hotový zkažený hřebík položený přes rozpracovanou tyč.

Z toho plyne pravidlo:

- `workpiece_nail` je základní silueta a fáze;
- `defects_nail` je finální nebo kontrolní zobrazení vady;
- mezifázové poškození má být samostatná vrstva stavu materiálu, ne náhrada
  celého výrobku.

## Doporučené balíky

### 1. Nail condition overlays, minimum pro další build

Atlas `nail_condition_overlays_atlas_2048x1024_v1.png`, mřížka `4 x 2`,
buňky `512 x 512`, průhledné RGBA, pivot `256,256`.

Tyto vrstvy se kreslí nad aktuální pracovní fázi a nemění celou siluetu:

1. `scale_light_hot` - první okuje na žhavém konci;
2. `scale_heavy_hot` - silná vrstva okují při přehřívání;
3. `burned_end_hot` - přepálený pracovní konec, jasný okraj a drobivý povrch;
4. `burned_end_cold` - vychladlý přepálený konec, tmavý křehký povrch;
5. `neck_crack_hot` - vlasová trhlina u krčku v žáru;
6. `neck_crack_cold` - čitelná studená trhlina u krčku;
7. `overdrawn_thin_zone` - lokálně příliš tenký dřík;
8. `hammer_flatspots` - hrubé plošky po opakovaných ranách.

Tohle řeší hlavní problém bez násobení všech vad všemi fázemi.

### 2. Phase defect variants, druhá vlna

Atlas `nail_phase_defects_atlas_4096x2048_v1.png`, mřížka `8 x 4`,
buňky `512 x 512`, průhledné RGBA.

Navržené buňky:

1. `drawing_crooked_shank_hot`
2. `drawing_overdrawn_shank_hot`
3. `drawing_neck_crack_hot`
4. `drawing_burned_iron_hot`
5. `point_blunt_point_hot`
6. `point_off_axis_hot`
7. `point_crooked_shank_hot`
8. `point_burned_iron_hot`
9. `cutting_bad_cut_hot`
10. `cutting_neck_crack_hot`
11. `cutting_overdrawn_shank_hot`
12. `cutting_burned_iron_hot`
13. `head_undersized_head_hot`
14. `head_off_center_hot`
15. `head_neck_crack_hot`
16. `head_burned_iron_hot`
17. `inspect_crooked_shank_hot`
18. `inspect_blunt_point_hot`
19. `inspect_point_off_axis_hot`
20. `inspect_undersized_head_hot`
21. `inspect_head_off_center_hot`
22. `inspect_neck_crack_hot`
23. `inspect_burned_iron_hot`
24. `inspect_overdrawn_shank_hot`
25. `finished_crooked_shank_cold`
26. `finished_blunt_point_cold`
27. `finished_point_off_axis_cold`
28. `finished_undersized_head_cold`
29. `finished_head_off_center_cold`
30. `finished_neck_crack_cold`
31. `finished_burned_iron_cold`
32. `finished_overdrawn_shank_cold`

Současný `defects_nail` atlas lze dočasně brát jako zkrácenou verzi buněk
25-32.

## Prompt pro generování

Společná část:

> Střízlivá malovaná realističnost malé české vesnické kovárny kolem roku 1400,
> průhledné PNG RGBA, stejný boční pohled jako atlas ručně kovaného hřebíku,
> hlava nebo zásobní část vlevo, pracovní konec a špička vpravo, bez pozadí,
> bez rukou, bez nástrojů, bez textu, bez vodoznaku, měkký okraj alfa kanálu,
> pivot přesně uprostřed buňky 512 x 512.

Pro condition overlay:

> Vytvoř pouze průhlednou vrstvu povrchového poškození pro ručně kovaný hřebík,
> ne celý hřebík. Vrstva musí pasovat přes existující polotovar v bočním pohledu.
> Zobraz: `{asset_id}`.

Pro phase variant:

> Vytvoř celý rozpracovaný ručně kovaný hřebík ve fázi `{phase}` s vadou
> `{defect}`. Tvar musí odpovídat stejné ose, velikosti a kompozici jako
> základní atlas pracovních fází.

## Integrace do hry

Krátkodobě:

- finální `defects_nail` kreslit pouze ve fázi `Inspecting` a `Finished`;
- před kontrolou zobrazovat vadu textově a přes teplotní tint základního kusu.

Po dodání nových assetů:

- přidat `nail_condition_overlays` do `smithing_minigame_assets.json`;
- doplnit loader textury do `Campaign::SmithingMinigameAssets`;
- vykreslit condition overlay mezi polotovarem a rukama;
- phase variants použít jen tam, kde vada mění siluetu, jinak dát přednost
  levnější condition vrstvě.
