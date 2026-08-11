# Planovany strom remesel

Datovy zdroj: `data/crafting/craft_tree.json`

Zdrojovy navrh: `Planovany_strom_remesel_Remeslo_stredoveku.docx`, navrh 1.0 z
11. srpna 2026.

## Zakladni princip

Strom remesel neni obchod se schopnostmi za XP. Uzel se ma odemknout az ve
chvili, kdy hrac postup videl, pochopil, prakticky vyzkousel a prokazal na realne
praci. Ciselny postup pomaha enginu a UI, ale sam o sobe nema nahradit mistra,
pracoviste, material, kvalitu vyrobku ani svedectvi.

Chyby jsou soucast uceni. Rozpoznani vady, oprava a hospodarne zachraneni prace
jsou samostatne doklady postupu, ne jen trest za spatny pokus.

## Spolecne hodnosti

- Pozorovatel: zna nazvy, bezpecnost, material a poradi kroku.
- Ucen: dela zakladni operace pod dohledem.
- Tovarys: zvladne cely bezny vyrobek a opravi vlastni chyby.
- Specialista: voli hlubsi vetev a resi neobvykle vady nebo pozadavky.
- Mistr: navrhuje postup, ruci za jakost a predava znalost.
- Inovator: dobove zlepsi nastroj, pripravek, konstrukci nebo postup.
- Mistr dilny: ridi lidi, zasoby, terminy, bezpecnost a standard jakosti.

## Model odemykani

Kazdy hodnostni uzel muze vyzadovat sest typu dokladu:

- Znalost: hrac postup pozoroval, slysel vysvetleni nebo ho spravne odvodil.
- Praxe: provedl dilci operace v pozadovanem poctu a rozmanitosti.
- Jakost: vysledky dosahly stanovene urovne bez skryte pomoci.
- Porucha a naprava: poznal typickou vadu a napravil ji nebo praci zastavil.
- Pristup: ma nastroj, pracoviste, material a svoleni majitele nebo mistra.
- Svedectvi: hodnostni postup prijme mistr, objednavatel nebo komunita.

Postup v domene se pocita z vah:

- znalost 20 %
- prakticke operace 35 %
- kvalita vyrobku 20 %
- vady a opravy 15 %
- svedectvi a uceni 10 %

## Stavy znalosti postupu

- Neznamy: hrac o postupu nevi.
- Zaslechnuty: zna ucel nebo nazev.
- Pozorovany: videl cely postup nebo dulezitou cast.
- Vyzkouseny: ma za sebou alespon jeden pokus.
- Osvojeny: postup funguje opakovane v beznych podminkach.
- Zvladnuty: stabilni jakost, diagnostika vad a schopnost vysvetlit postup.

## Hlavni domeny

- Kovarstvi: nastroje, podkovy, stavebni kovani; uci Benes kovar.
- Tesarstvi a truhlarstvi: stavby, nabytek, kola; spolecny zaklad do tovaryse.
- Hrncirstvi: nadoby, zasobnice, kachle a pece; chyba se casto ukaze az pri vypalu.
- Tkani a textil: len, nit, platno, odevy, lana a site.
- Kozeluzstvi a prace s kuzi: obuv, brasny, pochvy, remeni a postroje.
- Kamenictvi a zednictvi: zdi, osteni, schody, pece a skupinove stavby.
- Bylinkarstvi: urcovani rostlin, sber, suseni, smesi a bezpecne zameny.
- Ranhojicstvi: rany, krvaceni, dlahy, horecky, otravy a dlouhodoba pece.
- Vareni a uchovani potravin: kase, polevky, chleb, suseni, soleni a kvaseni.
- Tajemstvi artefaktu: skryta vetev navratu odemykana remeslnym porozumenim.

## Vetev artefaktu

Artefakt zustava zamerne nejednoznacny. V UI se muze ukazat jako Tajemstvi
artefaktu, Pamet mista nebo Poznani motyky.

Hlavni uzly:

- Prvni ozvena: 3 svetske domeny alespon na 40 %.
- Spojeni stop: 6 domen alespon na 60 %.
- Pamet prace: 3 domeny na hodnosti Mistr.
- Devet svedectvi: vsech 9 svetskych domen alespon na 80 %.
- Obnova artefaktu: mezioborovy mistrovsky projekt obnovy motyky.
- Navrat / setrvani: zaverecny pribehovy uzel na Housce.

Zaverecne smerovani:

- Hmota: presnost vyrobku a obnova artefaktu.
- Pamet: historie, symboly, sny a mista.
- Spolecenstvi: predani remesel, duvera a pomoc lidi.

## Mezioborove projekty

- Kolarstvi: drevo + kov.
- Truhla s kovanim: presna truhlarina + kovani.
- Pec a kamna: zednictvi + hrncirstvi.
- Lecebna souprava: bylinky + ranhojicstvi + textil.
- Postroj a vuz: kuze + drevo + kov.
- Hradni dvere: drevo + kov + kamen.
- Zasoby na zimu: vareni + hrncirstvi + bylinky.
- Obnova motyky: kovar a drevo na mistrovske urovni, ostatni obory jako svedectvi.

## Vedlejsi okruhy

- Zemedelstvi
- Mlynarstvi
- Rybarstvi a rybnikarstvi
- Chov zvirat
- Lesni prace a uhlirstvi
- Stavba cest a komunitni prace

Tyto okruhy nejsou prvni implementacni cile. Slouzi jako navazne zdroje zakazek,
materialu, povesti a sezonnich potreb.

## Implementacni poradi

1. Bylinkarstvi do Tovaryse.
2. Vareni do Ucne.
3. Drevo do Ucne.
4. Kovarstvi do Ucne.
5. Lecebna souprava jako prvni mezioborovy projekt.
6. Textil, kuze, hrncirstvi a kamen.
7. Mistrovstvi a artefakt.

## Technicky smer

Trvaly stav stromu patri do samostatneho `CraftProgressionState`, ne do
`PlayerStats`. Statistiky hrace maji menit obtiznost, rychlost uceni nebo riziko
chyby, ale nemaji samy odemykat remeslne uzly.

Planovane datove casti:

- `CraftDomain`
- `CraftRank`
- `CraftNodeDef`
- `CraftNodeState`
- `ProcedureKnowledge`
- `CraftEvidence`
- `SpecializationState`

Prvni hotova vetev musi byt znovupouzitelny vzor: jasny herni ukon, podminky,
vysledek, zpusob overeni, alespon jedna chyba behem prace, jedna chyba po
dokonceni, reakce mentora a save/load bez ztraty dukazu.
