# Návrh UI řemesel a první sada předmětů

## Základ obrazovky řemesla

Každé z 13 světských řemesel má vlastní stránku. Stránka není běžný strom schopností za body, ale obrazový záznam skutečného učení.

- Levý sloupec: seznam objevených řemesel, hodnost, celkový postup a upozornění na nový poznatek.
- Střed: šest hlavních milníků od Pozorovatele po Inovátora / Mistra dílny. Větve specializací se oddělují až od úrovně Specialista.
- Pravý panel po výběru uzlu: co hráč viděl, co už zkusil, chybějící vybavení, mistr, praktický důkaz a naučené vady.
- Dolní lišta: poslední výrobky, jejich jakost, nalezené chyby a možnost porovnat špatný kus s povedeným.

Na jedné stránce je vhodné zobrazit nejvýše 6 hlavních uzlů a 2-3 specializační odbočky. Strom zůstane čitelný i při běžném rozlišení a hráč uvidí celý průběh oboru bez dalšího posouvání.

## Vizuální stavy milníku

Stav znalosti a stav výrobku jsou dvě různé věci.

| Znalost uzlu | Obraz v UI | Zobrazené informace |
| --- | --- | --- |
| Neznámý | silná mlha, bezejmenná silueta | pouze návaznost na předchozí uzel |
| Zaslechnutý | rozmazaný obrázek, tlumená barva | název nebo účel, pokud jej hráč skutečně slyšel |
| Pozorovaný | ostrý obraz v šedých tónech | pozorované kroky a známá rizika |
| Vyzkoušený | částečně barevný obraz | vlastní pokusy, chyby a nejlepší výsledek |
| Osvojený | plná barva | samostatná výroba a stabilní běžná jakost |
| Zvládnutý / prokázaný | plná barva, pečeť mistra | důkaz zvládnutí, vyšší jakosti a možnost učit druhé |

Rozmazání je lepší vyrábět až při vykreslení nebo v malé stránkové mezipaměti. Jeden čistý sprite lze potom použít v inventáři, stromu, porovnání vad i dílenské minihře. Přiložené soubory `heard_blurred` a `unknown_fogged` jsou jen ukázky výsledného vzhledu.

## Skryté poslední řemeslo

`Tajemství artefaktu` nemá být zpočátku vidět jako čtrnáctá běžná položka. Celá stránka zůstane v mlze, bez názvu, procent a náhledu uzlů. Obrys stránky se začne odhalovat až po prvních mistrovských svědectvích. Plně se zpřístupní teprve při splnění společných podmínek:

- nejméně 80 % mistrovství ve všech 13 světských řemeslech,
- Velká kniha dílny Housky,
- dokončený mezioborový projekt,
- nalezený ztracený rukopis.

## Stavy předmětu

Výrobní fáze nejsou jakost. Předmět nejprve prochází přes `raw_material`, `forged_blank` a `unfinished`; teprve dokončený výrobek dostane jakost.

| Identifikátor | Český význam | Herní důsledek |
| --- | --- | --- |
| `ruined` | Zkažený | nefunkční nebo nebezpečný, může způsobit zranění |
| `provisional` | Provizorní | nouzově použitelný, nízká životnost |
| `common` | Běžný | standardní pracovní kus |
| `honest` | Poctivý | spolehlivější, delší životnost |
| `excellent` | Výborný | přesný výrobek pro náročnou zakázku |
| `masterwork` | Mistrovský | opakovatelná špičková práce a důkaz hodnosti |

## Hotová sada: kovářství 2.0

Balíček obsahuje devět výrobků: hřebík, skobu, pracovní hák, podkovu, tesařské dláto, pracovní nůž, srp, motyku a sekeru. Každý předmět má 9 základních stavů a 2 ukázkové varianty odhalení. Celkem jde o 81 výrobních a jakostních spritů a 18 náhledů pro uzamčené UI. Všechny jednotlivé herní sprity jsou PNG 128 × 128 px se skutečným průhledným pozadím.

Pořadí sloupců v atlasu:

1. surovina,
2. vykovaný polotovar,
3. nedokončený výrobek,
4. zkažený výrobek,
5. provizorní výrobek,
6. běžný výrobek,
7. poctivý výrobek,
8. výborný výrobek,
9. mistrovský výrobek.

Atlas `smithing_items_9states_atlas_1152x1152.png` má devět řádků v pořadí hřebík, skoba, hák, podkova, dláto, nůž, srp, motyka a sekera. Samostatné soubory leží ve stejně pojmenovaných složkách podle anglického identifikátoru z `smithing_items_manifest.json`.

Soubor `smithing_items_contact_sheet.png` je kontrolní obraz se všemi výrobky a českými názvy stavů. Do hry se nenačítá.

## Obrazové milníky pro další hlavní obory

Soubor `craft_milestone_catalog.json` obsahuje připravený seznam šesti reprezentativních obrazů pro všech 13 plných řemesel. Nejde o pouhé odznaky hodností: každý obrázek ukazuje konkrétní materiál, nástroj, výrobek nebo důkaz práce. Tím zůstane postup věcný a řemeslný.

## Doporučené načítání v enginu

- Načíst jen stránku právě otevřeného řemesla a předchozí/následující stránku.
- Držet čisté 128 × 128 sprity jako zdroj pravdy.
- Rozmazané náhledy ukládat do malé mezipaměti s nejvýše 8 položkami; po změně stránky je uvolnit.
- Mlha je jedna sdílená překryvná textura, nikoli kopie každého uzlu.
- Jakost a fázi ukládat jako data (`itemId`, `productionStage`, `qualityTier`, `defects`), ne zakódované pouze v názvu souboru.
- Pro animovaný milník použít 4-6 snímků jen u právě vybraného uzlu. Ostatní uzly mají statický první snímek.

## Doporučené chování animace

Animace má vysvětlovat práci, ne neustále blikat. Po výběru milníku se jednou přehraje krátká sekvence 4-6 snímků (například žár, úder, odlet okuje, zchlazení) a poté zůstane na klidovém snímku. Neobjevené uzly se nikdy neanimují; mlha se může jen velmi pomalu posouvat.
