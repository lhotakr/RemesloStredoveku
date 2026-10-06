# Zvuky kování hřebíku v1

Malý původní zvukový balík pro modul výroby hřebíku v projektu **Řemeslo středověku**. Zvuky jsou vytvořené pro krátkou herní smyčku, bez hlasů a bez moderního průmyslového dojmu.

## Co balík obsahuje

- 4 lehké údery do žhavého železa;
- 3 těžké údery do žhavého železa;
- 2 ostřejší údery do chladnoucího kovu;
- 2 sevření kleští, 2 zvuky čtvrtotáčky a 1 dosednutí;
- opadávání okují;
- krátké a silné ochlazení ve vodě;
- stlačení měchů;
- smyčku ohně výhně a tlumenou smyčku celé kovárny;
- WAV zdroje 48 kHz / 16 bit / mono a runtime OGG;
- manifestační soubor a jednoduchý řadič pro `AudioManager`.

Rychlý poslech celé sady je v `preview/smithing_nail_audio_demo.ogg`.

## Kam soubory zkopírovat

Obsah adresáře:

```text
assets/audio/smithing/nail
```

zkopíruj do stejné cesty v projektu. Soubory `SmithingNailAudioController.h/.cpp` patří k ostatní logice minihry. Uprav pouze relativní include `../audio/AudioManager.h` podle skutečného umístění.

## Vazba na stávající animace

| Událost | Okamžik | Zvuk |
| --- | --- | --- |
| `impact` | snímek 6 `hammer_strike_heavy` | obměna podle teploty a síly úderu |
| `grip_closed` | snímek 2 `tongs_rotate_quarter` | sevření kleští a začátek valivého škrábnutí |
| `rotation_complete` | snímek 6 `tongs_rotate_quarter` | krátké kovové dosednutí |
| `water_contact` | skutečný průchod hřebíku hladinou | krátké nebo silné syčení podle teploty |
| `scale_flakes` | stejný `impact`, jen při větší vrstvě okují | drobné kovové odštěpky |
| `bellows_push` | pracovní zdvih měchů | proud vzduchu a zavrzání kůže |

Zvuk úderu se nesmí spouštět při stisku tlačítka. Spouští se až z události `impact`, aby přesně souhlasil s dotykem kladiva a kovu.

## Doporučené volání

```cpp
void NailForgingController::onAnimationEvent(std::string_view event)
{
    if (event == "impact")
    {
        m_audio.onHammerImpact(
            m_workpiece.temperature01,
            m_pendingStrike.power01,
            m_workpiece.scaleAmount01);

        resolvePendingStrike();
    }
    else if (event == "grip_closed")
    {
        m_audio.onGripClosed();
        m_workpiece.heldByTongs = true;
    }
    else if (event == "rotation_complete")
    {
        m_audio.onRotationComplete();
        ++m_workpiece.quarterTurns;
    }
}

void NailForgingController::onWaterSurfaceCrossed()
{
    m_audio.onWaterContact(m_workpiece.temperature01);
}
```

Při vytažení hřebíku z vody zavolej `resetQuench()`. Tím se zabrání opakovanému syčení v každém snímku, ale další samostatné ponoření bude opět slyšet.

## Směšování

- Doporučená hlasitost úderů: 100 %.
- Kleště a okuje: přibližně 55–70 % hlasitosti úderu.
- Ochlazení: 80–90 %.
- Oheň výhně: 18–25 %; prostor kovárny: 10–18 %.
- Současně dovol nejvýše tři zvuky úderu a hlídej minimální odstup 65 ms.
- Pokud `AudioManager` neumí smyčky ani oddělené kanály, v prvním zapojení načti jen jednorázové zvuky. Smyčky přidej až po rozšíření zvukového správce.

## Poznámka k výsledku

Jde o původní procedurálně vytvořenou první sadu, takže ji lze bez licenčního omezení dál stříhat a vrstvit. Je vhodná pro prototyp a první hratelný průřez. Později ji lze nahradit terénními nahrávkami kladiva, kovadliny, měchů a vody při zachování stejných identifikátorů a herní logiky.
