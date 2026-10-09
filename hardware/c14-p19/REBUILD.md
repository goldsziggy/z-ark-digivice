# C14-P19 editable CAD prerequisites

Both variants include `cad/build.py`, `cad/direct_pwr_access.py`, `cad/parameters.json`, requirements and contracts. [publication-manifest.json](publication-manifest.json) records original and exported hashes. Only the two copied `README.txt` files were adapted for the source-only export; selected C14 JSON provenance is already relative and needed no path changes. All CAD Python, parameter, contract, STL and guide bytes remain unchanged.

**A clean checkout cannot rebuild either variant until the external vendor inputs are supplied.** Each builder loads ten Waveshare-derived STLs from its own `reference/official_meshes/` directory. They are excluded because redistribution terms remain unresolved. The [expected file inventory](vendor-inputs.json) records filenames, byte sizes and SHA-256 values; these external inputs are ignored by Git.

Use the [official Waveshare resources](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46/Resources-And-Documents) and the recorded [standard-cover structural archive](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.46/ESP32-S3-Touch-LCD-1.46_Structural%20Diagram-With%20Cover.rar). Merely downloading the archive does not populate the extracted reference meshes. The prior STEP extraction workflow depended on an absent runtime and was not fully pinned. **No verified turnkey converter is included.** Resolve that conversion/acquisition gap and match the expected hashes before claiming an equivalent rebuild; do not silently substitute approximate board meshes.

The [Elechouse manual](https://www.elechouse.com/wp-content/uploads/2024/07/Elechouse-Pn5321-Mini-Product-Manual-V1.pdf) is documentation only, not a CAD dependency. Its official URL/hash is preserved in `vendor-inputs.json`; its PDF is excluded. The assembly PDF in `docs/` is the enclosure project's separate guide, not this vendor manual.

After resolving the external inputs, work in a separate copy because the builder writes generated geometry, contracts and checks:

```sh
cd hardware/c14-p19/cad/touch-only  # choose two-button instead for that variant
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r cad/requirements.txt
python -B cad/build.py
```

The CAD handoff recorded CPython 3.11.11, numpy 2.4.6, trimesh 5.1.1, manifold3d 3.5.4 and scipy 1.17.1; optional validation/report tools were pymeshlab 2025.7.post1 and reportlab 5.0.1. This publication step did not install dependencies or rerun that environment.

Frozen printable artifacts are centrally organized in [stl/](stl/). A rebuild writes a new `stl/` tree within the working variant. The `gameplay_buttons` parameter distinguishes the two fronts; white shell pilots are 1.9 mm in both, black pilots 1.6 mm and clearances 2.4 mm. Historical C13 filenames/provenance identify unchanged geometry, not the superseded white fronts.

No project license has been selected. Dependencies retain their own terms, and publishing enclosure sources does not authorize redistribution of excluded vendor inputs. Physical fit and printed screw retention remain unverified even after a successful nominal rebuild.
