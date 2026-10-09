# Animated battle appearance study

A 16-second export of the intended round-screen battle presentation, framed by a solid tapered shell without finger holes. The broad grip tapers below the display; this is a front-view appearance concept, not a new dimensionally verified enclosure. No CAD file was changed.

The original Mote/Flicker pixel sprites come from `assets/starter-v1.json`, with a brighter presentation palette. The renderer uses a 480×480 source framebuffer quantized to RGB565 (32 red × 64 green × 32 blue levels). Final MP4 compression and the GIF palette can slightly change decoded colors. The MP4 export is 24 fps; this is a presentation choice, **not a measured ESP32 frame rate**.

`battle-events.json` holds exact CLI replay snapshots. Before export, `render_battle.py` runs each event prefix through `build/digivice-core` and checks equality with those snapshots. HP and reward values are read from the snapshots. The renderer adds only visual timing, sprite motion, particles, projectiles, hit reactions, and capture choreography.

Sequence: seed 12345 → walk 100 → Spark card → attack → capture. Flicker starts at 27 HP; the boosted hit deals 16; retaliation takes Mote from 100 to 97; capture succeeds, awarding one capture and 12 bond. The attack and retaliation are one atomic core event, staged as two visual beats. No game rules or service behavior changed.

Outputs are in `../../../deliverables/battle/`: the 800×1040 H.264 MP4 showing the shell, native 480×480 screen MP4/GIF, and a PNG preview. `VALIDATION.json` records dimensions, frames, duration, decode checks, core provenance and RGB565 validation. The same existing Pillow environment and ffmpeg installation were used; nothing was installed.

On this Mac, reproduce from the repo root:

```sh
python3 demos/battle/render_battle.py
```

The provisional Waveshare ESP32-S3-Touch-LCD-2.1 supports 480×480 with the RGB565 path documented by its [official FAQ](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1/FAQ). Rich 2D sprites, pre-rendered effects and small particle layers are the intended direction. The current ESP project is still a serial starter; its display adapter is disabled. Firmware frame pacing, concurrent Wi-Fi/PSRAM use, power draw and thermal behavior still require hardware measurement.
