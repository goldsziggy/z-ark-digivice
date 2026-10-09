# In-game artwork sources

The playable demo uses the same exact-form sprite pixels and eight scene backgrounds as the installed z-ark game. Character sprites are converted losslessly from its indexed RGB565 device files to compact transparent browser atlases. No creature is replaced with a generic drawing. The original game egg drawing is retained.

Digimon character designs and official game artwork remain with their respective rights holders, including Bandai Namco. Source and conversion credits do not grant or transfer artwork rights. No open-content license or franchise endorsement is asserted.

## Sprite credits

The current exact-form collection contains 241 sprites. Sources are [The Spriters Resource](https://www.spriters-resource.com/) and [EwertonMendes/DigiGame](https://github.com/EwertonMendes/DigiGame/tree/753ff677e127a1d33405102d677a7b733a5c8dd2), pinned to commit `753ff677e127a1d33405102d677a7b733a5c8dd2`.

Recorded ripper/compiler credits include **A.J. Nitro, Atlanta, Dazz, Mighty Jetters, RadSpyro, redblueyellow, Sonicteam24 and Verion**. Individual source links, uploader credits, retained source-credit notes and conversion hashes accompany each form in the [art manifest](art/manifest.json) and [detailed attribution](art/ATTRIBUTION.md). The original ripper and exact originating game remain unverified for the 154 DigiGame additions; their recorded source claims are not independent verification.

The Spriters Resource [Help](https://www.spriters-resource.com/page/help/) discusses free fan games while explaining that the site cannot license the underlying artwork. Its [Terms of Use](https://www.spriters-resource.com/page/tou/) require source credit and distinguish permitted use from original-format rehosting and commercial distribution. The [DigiGame character notice](https://github.com/EwertonMendes/DigiGame/blob/753ff677e127a1d33405102d677a7b733a5c8dd2/assets/characters/README.md) identifies fan/prototype material and disclaims an open-content license. This demo is free; noncommercial status does not itself create a license.

This page displays only optimized runtime art used by the playable experience. It does not offer the source sheets, original import packs or unrelated pose collections as downloads. The presence of artwork here does not authorize further reuse. Historical local-import scope metadata remains unchanged at its source.

## Scene backgrounds

Meadow, forest, beach, ruins, cavern, snow, volcanic and digital are the existing generated environments created for this Digivice project with the built-in image generation tool and approved by its owner. The demo uses the actual in-game 412 × 412 device JPEGs with unchanged composition and hashes. No new scenery was generated for this update and no CC0 or other open-content license is asserted.

## Rendering and availability

Form IDs, audited native facing, animation crop bounds and scene order follow the current game. Home, battle and capture use the native actor anchors and integer nearest-neighbor sprite scaling on a 412 × 412 scene; the surrounding browser controls and text panels are web adaptations.

Only visible artwork is loaded. The game catalog contains forms that do not have an exact sprite in the installed collection. Those show an explicit unavailable label, never a different Digimon or invented art. The rules, encounter pool and evolution choices remain unchanged.
