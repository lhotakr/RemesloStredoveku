# Obrázkové scény v sekvencích

V editoru sekvencí vyberte nad náhledem scénu `houska_intro_2d`, případně
vytvořte další pomocí **+ 2D scéna**. Tlačítko **Pozadí / plátno** otevře
výběr obrázku a rozměry plátna. Výchozí rozměry odpovídají obrázku pozadí.

Pomocí **+ Objekt**, **+ Text** nebo **+ Postava** přidejte vrstvu. Vyberte ji
v seznamu nebo přímo na plátně a posuňte myší. V inspektoru nastavíte obrázek,
character a animaci, text a barvu, pozici, rozměry, měřítko, natočení obrázku,
pořadí vrstvy, průhlednost a počáteční viditelnost. Vrstvy lze duplikovat
nebo smazat. Ctrl+Z / Ctrl+Y vrací úpravy; **Save** ukládá i rozpracované scény.

Pozice X/Y označuje levý horní roh v pixelech plátna. Rozměr 0 se dopočítá
z obrázku; při zadání pouze šířky nebo výšky zůstává zachován poměr stran.
Šířka textu určuje zalamování, 0 znamená text bez automatického zalamování.
Vyšší číslo vrstvy se kreslí navrch; při stejném čísle rozhoduje ID.
Náhled i hra zachovávají poměr stran plátna a případně přidají černé okraje.

V **Pozadí / plátno** lze vložit `load_scene` na začátek vybraného uzlu.
Délku zobrazení určují další akce sekvence, například `wait`; další
`load_scene` přepne na jinou obrázkovou scénu nebo na 2.5D interiér.
`show_prop` / `hide_prop` ovládají i textové vrstvy, `move_actor` může
posouvat libovolnou vrstvu. **+ Bod** vytvoří cíl pohybu; jeho souřadnice
jsou rovněž v pixelech. Vrstva může mít bod umístění nastavený v inspektoru.

Minimální dokument scény:

```json
{
  "id": "houska_intro_2d",
  "type": "image_2d",
  "background_image": "assets/modern/Houska_intro.png",
  "markers": [],
  "actors": [],
  "props": [
    {
      "id": "title",
      "type": "text",
      "text": "Pod hradem Houska",
      "x": 80,
      "y": 60,
      "width": 900,
      "font_size": 40,
      "color": [255, 240, 210, 255],
      "layer": 1,
      "visible": true
    }
  ]
}
```

Cesty obrázků jsou relativní ke kořeni projektu. Volitelné `canvas` má
položky `width` a `height`. Obrázkové vrstvy používají `texture`, postavy
`character` a `animation`; společné jsou `x`, `y`, `width`, `height`,
`scale`, `layer`, `opacity` (0–1) a `visible`. Natočení obrázků je
`yaw_degrees`. Textové vrstvy jsou v poli `props`.

## Kamera a postava v 2.5D sekvenci

Na časové ose nastavte čas a otevřete **Kamera / postava**. Vyberte hrdinu,
délku a konečný odstup, potom stiskněte **Nájezd na postavu**. Výchozí
**Po dojezdu sledovat postavu** plynule předá dosažený záběr sledování:
odstup, výška a náklon zůstanou zachované. Nájezd vychází z aktuální kamery,
například z kamerového bodu nastaveného scéně nebo předchozí akcí `set_camera`.

**Přesunout pouze postavu** vloží chůzi k vybranému bodu. Překrývající se
pohyby běží současně na stopách Actor a Camera; vložení nájezdu nezpozdí
začátek již připravené chůze. Skupina počká na dokončení obou pohybů.
**Uvolnit kameru** vypne sledování. Ruční `camera_move` a `set_camera`
sledování také přeruší. Ctrl+Z / Ctrl+Y vrací vložení i úpravy.

Po výběru kamerové akce lze v inspektoru změnit cíl na kamerový bod,
nastavit výšku, směr, náklon, průběh pohybu a postavu pro zaměření nebo
sledování. Zaškrtnuté číselné hodnoty přepisují kamerový bod; ostatní
se přebírají z bodu nebo aktuálního záběru. Vyhlazení sledování `0` drží
pevný odstup, kladná hodnota dovolí měkké dohánění (vyšší = rychlejší).
Kamerové nájezdy jsou určeny pro 2.5D interiéry; vrstvy v obrázkových
scénách lze dál samostatně posouvat pomocí `move_actor`.

Příklad nájezdu, který nepohybuje hrdinou a po dokončení ho začne sledovat:

```json
{
  "type": "camera_move",
  "target_actor": "patrik",
  "duration": 3,
  "distance": 2.2,
  "easing": "smooth",
  "follow_after": true
}
```

## Hudba přes více uzlů

Akci `play_music` stačí vložit jednou na začátek sekvence. Hudba pokračuje
při přechodu do dalších uzlů i při `load_scene`; `loops: -1` ji opakuje.
Zastaví ji `stop_music`, ukončení náhledu nebo ji nahradí další platná
akce `play_music`. Chybějící či nečitelný zvukový soubor původní skladbu
nevypne. V úvodu `intro_houska` takto hraje `introCinematics.ogg`.

V inspektoru zvukové akce nastavte **Hlasitost** v procentech. Akce
`set_music_volume` mění úroveň již hrající hudby; přechod může být plynulý.
Úroveň sekvence se násobí hlasitostí hudby v nastavení hry.

V **Audio knihovna** vloží tlačítko **Dočasně zeslabit hudbu** akci
`duck_music` na kurzor osy. Nastavte úroveň zeslabení, délku držení
(`Duration`) a časy plynulého zeslabení a návratu. Akce běží současně
s replikou a po skončení držení obnoví aktuální základní hlasitost.
Délku držení lze upravit i tažením pravého okraje klipu. Akce
`restore_music` ukončí zeslabení dříve. Pauza náhledu pozastaví i přechody.

## Sebrání předmětu v sekvenci

Akce `pickup_item` přidá `item` (ID z inventáře), v množství `count`,
a skryje objekt uvedený v `prop`. Lze použít i ID objektu přímo z interiéru.
`once_flag` zabrání opakovanému přidání při návratu do uzlu.
V úvodu Patrik po prohlédnutí motyky sebere `artifact_hoe` a skryje
podlahový objekt `interior_object_3`.

## Sprity na podlaze v 2.5D

Ve scéně stiskněte **+ Dekorace na podlahu** a klikněte na podlahu
v prostoru nebo půdorysu. V inspektoru rekvizity zvolte **Vybrat texturu
z projektu**, nastavte šířku, délku a natočení. PNG může mít průhledné pozadí.
**Upravit bod umístění** vybere bod dekorace pro přesouvání myší.
Pravé tlačítko nebo Esc zruší vkládání. **Smazat objekt** odstraní dekoraci;
Ctrl+Z / Ctrl+Y vrací vložení i úpravy.

V obecném editoru interiéru otevřete **Objects**, vyberte typ **Dekorace
na podlaze**, texturu a **Přidat obecný objekt**. Vznikne na pozici kamery,
pak ji přesuňte v půdorysu nebo číselně. U existujícího objektu stačí změnit
**Vykreslení** na **Na podlaze**.

Dekorace kopíruje sklon podlahy, neblokuje chůzi a nevytváří žádný sektor.
Rozměry jsou v jednotkách prostoru; hodnota 0 odvodí délku z měřítka
`scale` a šířku z poměru stran obrázku. Výška nad bodem dovolí malý odstup
od podlahy. Stěny a postavy dekoraci zakrývají.

Ve scénách i interiérech se používá `render_mode: "floor"`,
`floor_width`, `floor_depth`, `yaw_degrees` a `z_offset`. Ve scéně jde
o běžnou rekvizitu v `props`, takže funguje také `show_prop` / `hide_prop`.

## Sprity předmětů z inventáře

U rekvizity v 2.5D i obrázkové scéně použijte **Vybrat předmět z inventáře**.
Hledání funguje podle názvu i ID: napište `motyka` a vyberte **Podivná motyka |
artifact_hoe** pro hlavní artefakt, případně **Motyka | hoe_basic** pro běžný nástroj.
Vybere se stejný obrázek jako v inventáři; natočení, rozměry a režim
**Na podlaze** nastavíte v inspektoru dekorace.

V editoru interiéru jsou pojmenované položky **Inventář / …** ve výběru textury
objektu. Volba **Jen předměty inventáře** omezí seznam na inventářový katalog.
Scéna ukládá cestu obrázku, takže se stejný sprite načte i při přehrávání.
