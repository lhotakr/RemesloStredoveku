# Historické herní rozhraní

## Co je implementováno

`medieval::UI` je společná vrstva nad stávajícím Dear ImGui **1.92.6 WIP**
a backendem **SDLRenderer2**. Nepřidává další GUI knihovnu. Obsahuje správu
textur, fontové role, paletu, devítidílné panely, zalamovaná tlačítka ovladatelná
myší i klávesnicí, portréty, rozvržení rozhovoru a čtečku dokumentů.

Rozhovor je ukotvený u dolního okraje pracovního viewportu. Vlevo je portrét,
jméno a slovní vyjádření nálady; vpravo replika a samostatné odpovědi. Dlouhé
repliky a odpovědi mají vlastní posouvání, ukončení rozhovoru zůstává dostupné.
Při přechodu na jiný uzel se posun repliky i odpovědí vrátí na začátek.
Velikosti vycházejí z viewportu; měřítko je omezené na 0,75–1,5. Náhled editoru
volá tutéž funkci `dialogueContents()` jako hra.

`CampaignDialogUI.cpp` sestavuje pouze prezentační data. Nadále volá původní
`isDialogNodeAvailable()`, `isDialogChoiceAvailable()`, `formatChoiceLabel()`
a `applyDialogChoiceEffects()`. Vykreslovač vrací původní index odpovědi;
efekty se provedou jednou po dokončení vykreslování. Původní přechody, zavření,
nálada, příběhové příznaky a odměny zůstávají v `Campaign`. `CampaignDialog.cpp`,
`DialogManager.*` a `data/dialogs/dialogs.json` nebyly měněny.

Inventář a deník používají stejnou vrstvu. Identifikátory inventárních polí,
přesuny, vybavování, dělení zásob, kapacity a pravidla přijetí předmětu zůstaly
zachovány. Mění se kreslení podkladu polí, rozměry a počet sloupců zobrazené
mřížky. Stavy úkolů nadále vyhodnocují původní funkce. Diagnostické informace
deníku jsou zachované v rozbalovací části „Podrobnosti vývoje“.

## Ovládání a dokumenty

- Rozhovor: obvyklé zahájení interakce s NPC, kliknutí na odpověď nebo Tab a Enter.
- Deník: původní klávesa `Q`; tlačítko **Listiny a dopisy** otevře čtečku.
- Inventář: původní klávesa `I`, levé/pravé tlačítko myši a ostatní původní operace.
- Editor: původní dialogový náhled; **Restart náhledu** a **Náhled listin**.
- Čtečka: výběr dokumentu, **Zobrazit čitelný přepis** / **Zobrazit dobové písmo**,
  zavření tlačítkem nebo Escape. Při čtení se pozastaví aktualizace a ovládání světa.

`data/ui/documents.json` obsahuje tři explicitně ukázkové dokumenty: dopis,
farní zápis a správní listinu. `original` a `transcript` mohou mít odlišný text.
Běžný text používá Junicode, církevní Missaali a správní Aboensis; přepis vždy
Junicode. Dokumenty mají odlišné pergamenové podklady a okraje. Data a
`documentContents()` lze použít i pro skutečné herní listiny, zatím bez vazby
na jazykové dovednosti či nové úkoly. Ukázky nejsou edicí historických pramenů;
Missaali navíc vychází z pozdější tiskové předlohy z roku 1488.

Editorový náhled zobrazuje všechny volby a prochází jejich uzly, včetně
`closeDialog`. Neprovádí herní efekty ani nesimuluje příběhové podmínky, což je
označené přímo v editoru. Ostatní vývojová okna si zachovávají původní vzhled.

## Hlavní menu, určování přírodnin a mapa řemesel

Hlavní menu používá společný dřevěný panel, pergamenová tlačítka a fontové role.
Původní obrázek `assets/Images/MainMenuImg.png` zůstává pozadím. Nadpis a panel
se přizpůsobují viewportu; při nedostatku výšky se obsah panelu posouvá. Nová
kampaň, načtení, editor postavy, nastavení i konec hry zachovávají původní akce.
Sedm vstupů do vývojových nástrojů je v rozbalovací části **Vývojové nástroje**
(v angličtině **Development tools**). Existující rámování a tlačítka dalších
stránek menu, ukládání a herní nabídky nyní také používají společné prostředky;
jejich formuláře a herní akce nebyly přepisovány.

`CampaignForageUI.cpp` odděluje vykreslování minihry od velkého
`CampaignRender.cpp`. Obsahuje posuvný pergamenový list s původním obrázkem
nálezu, pozorovanými znaky, volným popisem a odhadem názvu. Tlačítka
**Vyhodnotit určení** a **Sebrat** zůstávají pod listem dostupná i při dlouhém
obsahu, zavření je v záhlaví. Původní `ForageSystem::identify()`, znalosti,
zpětná vazba, přidání do inventáře a vyčerpání/obnova naleziště zůstávají
zachované. Také neurčený jedovatý druh si uchovává skutečné druhové ID.
Známý druh a obecný vzorek mají zachované vlastní větve. Vstupní pole používají
světlý podklad a tmavý text; přesnost určení tlumenou hnědou a zelenou.

Mapa řemesel používá původní data a ikony, společné rámy i pergamen pro mapu,
karty uzlů a detail dovednosti. Posuvný seznam ikon vybírá obor, kliknutí uzel,
tažení levým tlačítkem posouvá a kolečko přibližuje kolem kurzoru. Kolečko nad
plátnem patří mapě, ne posunu okolního okna. **Přehled stromu** obnoví výchozí
přiblížení a umístí vybraný uzel do záběru. Při změně rozměrů se záběr znovu
upraví tak, aby uzel nezůstal mimo plátno. Na úzkém viewportu se detail přesune
pod mapu a samostatně posouvá. Celé okno respektuje pracovní oblast i v
640×480. Výpočty postupu, podmínky, skryté uzly a obsahy oborů se nemění.

## Grafika a licence

Byl zkontrolován skutečný `LICENSE.txt` v archivu
`Plans/fantasy-ui-wood-parchment-and-stone-v1.2.zip`:
**Fantasy UI – Wood, Parchment & Stone**, HugoMcJohnson, sada 1.2,
licence 1.0 z 21. 10. 2026. Dovoluje použití a úpravy v osobních i komerčních
hotových hrách. Zakazuje zveřejnění a další distribuci samotných souborů jako
prostředků pro opětovné použití. Samostatné obrázky ani ZIP proto nepatří do
veřejného zdrojového repozitáře. Sdílení se spolupracovníky je povolené pouze
pro práci na licencovaném projektu. Poděkování autorovi je dobrovolné.

Grafika je lokálně rozbalená do **`assets/ui/medieval/private/`**, vyloučeného
z Gitu. Kopie původní licence a autorovy tabulky devítidílných okrajů jsou tamtéž.
Použité PNG podle `data/ui/medieval_ui.json`:

| Úloha | Původní soubor ze sady |
|---|---|
| Dřevěný podklad | `panel_wood_dark_1.png` |
| Běžný pergamen | `panel_parchment_ultrathin_fine_fibre.png` |
| Kámen, portrét a pole | `panel_stone_thin_1.png` |
| Církevní list | `panel_parchment_ultrathin_warm_vellum.png` |
| Správní listina | `panel_parchment_verythin_softly_aged.png` |
| Rám portrétu | `frame_wood.png` |
| Stavy tlačítek | `button_parchment_normal.png`, `button_parchment_hover.png`, `button_parchment_pressed.png`, `button_parchment_disabled.png`, `button_parchment_focus.png` |

Okraje zdroje odpovídají autorovu `nine-slice-guide.json`; jejich zobrazené
měřítko nastavuje `border_scale`. Rohy se škálují rovnoměrně a střed s hranami
vyplní zbývající prostor. Při velmi malém cíli se okraje společně zmenší,
aby se nepřekřížily. Textury se filtrují lineárně, nezávisle na pixelovém
filtrování herního světa. Při chybějících či nečitelných obrázcích jsou použity
prosté barevné panely s rámečkem; nejsou vydávány za grafiku zakoupené sady.

**Stav Gitu při zahájení práce:** všech pět dodaných ZIPů už bylo připravených
ke commitu (`A` v indexu), včetně zakoupené grafiky. Tento index byl zachován.
Nové pravidlo `.gitignore` již připravený ZIP z indexu nevyřadí. Před případným
veřejným commitem je tedy nutné zakoupený archiv z připravovaného commitu
vynechat. V této práci se nic necommitovalo ani neodesílalo na GitHub.

## Písma a české znaky

| Role | Skutečný použitý soubor | Původní archiv / licence |
|---|---|---|
| Body | `assets/Fonts/Medieval/Junicode-Regular.otf` | `Plans/junicode.zip`, `junicode/doc/OFL.txt` |
| Heading | `assets/Fonts/Medieval/MedievalSharp-Regular.ttf` | `Plans/MedievalSharp.zip`, `OFL.txt` |
| ChurchDocument | `assets/Fonts/Medieval/Missaali-Regular.otf` | `Plans/missaali.zip`, `missaali/OFL.txt` |
| NobleDocument | `assets/Fonts/Medieval/Aboensis-Regular.otf` | `Plans/aboensis.zip`, `aboensis/OFL.txt` |
| Fallback | existující `assets/Fonts/NotoSans-Regular.ttf` | již přítomné písmo projektu |

Všechny čtyři přidané historické fonty mají **SIL Open Font License 1.1**.
Byly ponechány beze změny a u každého je přiložen jeho původní úplný text licence:
`Junicode-OFL.txt`, `MedievalSharp-OFL.txt`, `Missaali-OFL.txt`, `Aboensis-OFL.txt`.
OFL dovoluje přibalování k aplikaci a redistribuci při zachování licence
a copyrightu; nepovoluje samostatný prodej písma. Původní upozornění na
vyhrazené názvy se zachovávají přesně podle dodaných souborů.

Historické fonty se načítají jednou při vytvoření herního ImGui kontextu.
Za každý historický zdroj se slučuje Noto Sans; první zdroj poskytuje vlastní
znaky a náhradní doplňuje chybějící znaky. Rozsahy zahrnují základní a rozšířenou
latinku, Latin Extended Additional a běžnou interpunkci. ImGui 1.92 podle potřeby
rasterizuje glyfy a backend aktualizuje atlas; fontové soubory se během snímků
znovu nenačítají. Používá se aktuální `PushFont(font, size)`.

Chybí-li historický font, použije se Noto Sans. Chybí-li i Noto, zkusí se
Junicode, nakonec vestavěný font ImGui. Česká sazba byla ověřena se skutečnými
historickými fonty a Noto; bez všech externích písem je prioritou funkčnost
rozhraní a úplnost latinky vestavěného fontu není slibována.

## Instalace a doplnění prostředků

V aktuálním pracovním stromu jsou všechny dodané podklady již připravené.
Přípravu lze opakovat z kořenového adresáře projektu:

```bash
python3 tools/prepare_medieval_ui.py
cmake --build cmake-build-debug --parallel 2
```

Skript používá pouze standardní knihovnu Pythonu. Nic nestahuje, nepřidává do
indexu a nemění původní archivy. Chybějící archiv oznámí a ostatní zpracuje dál.
Pro jinou pracovní kopii zkopíruj do `Plans/` pět ZIPů se stejnými názvy,
nebo ručně připrav:

1. Obsah `Assets/` z grafické sady do `assets/ui/medieval/private/Assets/`.
   `LICENSE.txt`, `inventory.json` a `nine-slice-guide.json` patří do
   `assets/ui/medieval/private/`.
2. Čtyři konkrétní fontové soubory uvedené v tabulce do `assets/Fonts/Medieval/`,
   spolu s příslušnými původními licencemi.
3. Vlastní portrét Matěje například do `assets/ui/medieval/portraits/matej.png`
   a do `data/ui/medieval_ui.json` nastav:

```json
"portraits": {"matej_sedlak": "assets/ui/medieval/portraits/matej.png"}
```

Soubor portrétu dosud dodán nebyl; používá se neutrální iniciála. Portrét si
zachovává poměr stran. `portrait_aliases` mapuje `matej_intro` na `matej_sedlak`,
takže editor i hra použijí tentýž obrázek. Pro další dialogy lze přidat další
aliasy. Cesty v konfiguraci jsou relativní ke kořenu předanému do `initialize()`;
hra používá stávající `REMESLO_PROJECT_ROOT`. Současný krok CMake po sestavení
kopíruje `assets/` a `data/` včetně licencí také vedle binárního souboru.

## Životní cyklus

`Game::init()` po inicializaci ImGui a SDLRenderer2 zavolá
`UI::Get().initialize(renderer, root)`. UI vlastní SDL textury přes `unique_ptr`
s `SDL_DestroyTexture`; fontová data vlastní atlas ImGui. `Game::shutdown()`
uvolní UI před backendem, kontextem a SDL rendererem. Opakované `shutdown()`
je bezpečné. Ztracený/obnovený renderer vyžaduje znovuinicializaci UI ve stejném
bodu jako ostatní grafické prostředky hry.

`Font`, `Style`, `Window` a `Child` vyvažují ImGui zásobníky a páry Begin/End
destruktory, také při předčasném návratu nebo oříznutém okně. Pozadí potomka se
kreslí do seznamu rodiče a respektuje jeho ořez. Styl historických oken se
nepřenáší do zbytku editoru.

## Ověření

```bash
cmake --build cmake-build-debug --parallel 2
ctest --test-dir cmake-build-debug --output-on-failure
SDL_VIDEODRIVER=dummy cmake-build-debug/RemesloMedievalUITests --capture out/medieval-ui
```

`RemesloMedievalUITests` skutečně používá SDL software renderer a stejný
SDLRenderer2 backend jako hra. Kontroluje devítidílné rohy i velmi malé cíle,
rozvržení s posunutým viewportem, české a latinské glyfy ve všech pěti rolích,
měřítko fontu při původním `FontGlobalScale`, myš i klávesnici, původní index
vybrané odpovědi, posouvání dlouhých voleb, přepnutí přepisu, všechny tři druhy
listin, ořez potomka, vyvážení stylů a fontů, chybějící/chybné PNG, chybějící
fonty, načtení obrázkového portrétu, samotné náhradní Noto bez historických
fontů, neplatný konfigurační JSON a opakované uvolnění prostředků.

Rozhovor používá skutečný text úvodního dialogu Matěje. Vykreslení se zkouší
v 640×480, 1280×720 a 1920×1080; výpočet rozvržení také v 3440×1440.
Volba `--capture` ukládá lokální BMP pro vizuální kontrolu do ignorovaného `out/`.
Zachycené obrázky obsahují licencovanou grafiku a nejsou přidávány do repozitáře.

**Výsledek 17. 11. 2026:** projekt byl úspěšně sestaven, závěrečný CTest prošel
**5/5** bez chyb (kovářství, sekvence, zvuk sekvencí, interiérový editor a nové
historické UI). `git diff --check` nehlásí chyby bílých znaků.

Doplňková lokální integrační kontrola byla sestavena proti 78 skutečným
objektovým souborům hry, bez přepisování herní logiky či produkčních hlaviček.
Prošla přijetím a odevzdáním Matějova úkolu přes skutečné odpovědi, ověřila
podmínky uzlů a voleb, původní indexy, efekty, náladu a příběhové příznaky.
U inventáře ověřila dělení 10 → 5 + 5, položení jednoho kusu, sloučení,
vybavení, odmítnutí nesprávného předmětu a kontrolu kapacity prázdného pole.
Vykreslila skutečný inventář i deník a prošla přechody i zavření editorového
náhledu. Zdroj a reprodukční skript jsou lokálně v ignorovaném výstupu:

```bash
python3 out/medieval-ui/build_campaign_checks.py
SDL_VIDEODRIVER=dummy out/medieval-ui/CampaignUIIntegration
```

Zdroj: `out/medieval-ui/CampaignUIIntegration.cpp`. Tato doplňková kontrola
není součástí CTest ani veřejných zdrojů; vyžaduje současný debug build a jeho
`compile_commands.json`. Lokální PNG/BMP zachycují také skutečný rozhovor,
inventář, deník a náhled editoru (`campaign-matej`, `campaign-inventory`,
`campaign-journal`, `editor-dialog-preview`). Vizuálně byl zkontrolován
rozhovor v 640×480 a 1280×720, inventář, deník a církevní dokument.

Navazující rozšíření menu, přírodnin a řemesel bylo znovu úspěšně sestaveno
a prošlo CTest **5/5**. Test společného tlačítka navíc ověřuje minimální výšku
pro menu při zachování myši i klávesnicové aktivace. Další lokální kontrola
`out/medieval-ui/ExtendedUIIntegration.cpp`, spojená s 79 aktuálními objekty hry,
ověřila skutečný přechod menu do konfigurace kampaně, vyhodnocení nalezeného
druhu, změnu znalostí, známý druh, sebrání do batohu a vyčerpání naleziště.
Na skutečné mapě řemesel ověřila výběr oboru a jiného uzlu, zoom kolečkem,
tažení a reset záběru. Všechny tři obrazovky vykreslila v 640×480, 1280×720
a 1920×1080, včetně kontroly hranic okna a dostupnosti spodních tlačítek.
Kontroly běžely přes SDL software renderer; obrazovky v 640×480 a 1280×720
byly navíc zkontrolovány vizuálně. Reprodukce v této pracovní kopii:

```bash
python3 out/medieval-ui/build_extended_checks.py
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy out/medieval-ui/ExtendedUIIntegration
```

Nové lokální záznamy jsou `out/medieval-ui/main-menu-1280x720.png`,
`forage-1280x720.png` a `crafts-1280x720.png`, také s variantami menšího a většího
rozlišení a známým druhem. Integrační zdroje, skripty a záznamy zůstávají
v ignorovaném `out/`, nikoli mezi veřejnými prostředky.

Automatické kontroly nenahrazují kompletní ruční průchod světem: integrační
kontrola nastavuje připravenost vody příznakem, nechodí k herní studni.
Celý úkol na skutečné uložené pozici, cílový monitor a všechny kombinace
přesunů inventáře je vhodné ještě projít ručně. Původní inventární pravidla
nebyla v této práci rozšiřována ani opravována.

## Změněné a vytvořené soubory

- Upravené: `.gitignore`, `CMakeLists.txt`, `src/game/Game.cpp`,
  `src/game/CampaignRender.cpp`, `src/game/EditorDialogs.cpp`.
- Nová grafická vrstva: `src/ui/MedievalUI.h`, `src/ui/MedievalUI.cpp`,
  `src/ui/DialoguePanel.cpp`, `src/ui/DocumentReader.cpp`.
- Nové adaptéry hry: `src/game/CampaignDialogUI.cpp`, `src/game/CampaignMenusUI.cpp`.
- Nová obrazovka přírodnin: `src/game/CampaignForageUI.cpp`.
- Nová data: `data/ui/medieval_ui.json`, `data/ui/documents.json`.
- Nové nástroje a testy: `tools/prepare_medieval_ui.py`, `src/tests/MedievalUITests.cpp`.
- Dokumentace: tento soubor a `assets/ui/medieval/portraits/README.md`.
- Fonty: čtyři binární soubory a čtyři licence vyjmenované výše v
  `assets/Fonts/Medieval/`.
- Lokální ignorované prostředky: 62 původních PNG, `LICENSE.txt`, `inventory.json`
  a `nine-slice-guide.json` v `assets/ui/medieval/private/`; úplný seznam PNG
  poskytuje zkopírovaný `inventory.json`.

## Navazující NPC editor

Rozšířit existující NPC definice o cestu/klíč portrétu a v náhledu používat
stejný `DialogueView`. Animované portréty lze později doplnit jako atlasový
výřez v `portrait()` bez zásahu do dialogových efektů. Simulaci příběhových
podmínek v editoru je vhodné přidat přes oddělený testovací kontext s vlastními
příznaky a náladou, aby náhled neměnil rozehranou hru. Doklady následně napojit
na inventární předměty a gramotnost; současný přepis i fontové role jsou pro
toto rozšíření připravené.

Záměrně dosud není dodán portrét Matěje, animační editor ani systém výuky čtení
a jazykových dovedností. Vývojové panely mimo dialogový náhled nejsou přestylované.
