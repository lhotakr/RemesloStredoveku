# Bezešvý Fog of War — Řemeslo středověku

Nová tmavá kouřová textura pro `assets/overlays/FogOfWar.png`. Je periodická
v obou osách: levá hrana navazuje na pravou a horní na spodní. Kromě plynulého
přechodu mají protilehlé krajní pixely shodné hodnoty, takže při opakování
nevznikne světlá ani tmavá spára.

## Soubory

- `assets/overlays/FogOfWar.png` — doporučená provozní varianta, RGBA 1024 px;
- `assets/overlays/FogOfWar_512.png` — úspornější varianta se čtvrtinovou
  paměťovou náročností na grafické kartě;
- `FogOfWar_Translucent*.png` — alternativy s proměnným alfa kanálem, pokud
  neprůhlednost neřídí kód přes `SDL_SetTextureAlphaMod`;
- `preview/FogOfWar_tiling_test_3x3.png` — devět dlaždic bez zakreslených hran;
- `validation.json` — číselná kontrola shody protilehlých okrajů;
- `build_seamless_fog.py` — reprodukovatelná výroba z výtvarného zdroje.

## Použití v SDL

Stávající načítání očekává přesně cestu `assets/overlays/FogOfWar.png`, takže
stačí tímto souborem nahradit současnou texturu. Při skládání několika kopií
drž cílové obdélníky na celých pixelech. Pokud mlhu škáluješ, sousední
obdélníky musí sdílet stejnou hranu bez mezery a bez zaokrouhlování každého
rozměru jiným směrem.

Výchozí varianta má alfa kanál 255 a je vhodná, když průhlednost nastavuje hra.
Jestli se textura vykresluje bez `SDL_SetTextureAlphaMod`, použij variantu
`FogOfWar_Translucent.png`.
