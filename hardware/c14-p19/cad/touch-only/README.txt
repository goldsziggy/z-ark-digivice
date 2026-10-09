C14-P19 — touch only — WHITE SHELL PILOT REVISION

This isolated editable revision changes only the fifteen smooth receiving pilots
in the main white shell: A1–A4, C1–C3, D1–D3, E1–E3 and F1–F2, from diameter
1.6 mm to 1.9 mm. User selected 1.9 mm on 2026-10-09. Physical printed hole size,
actual screw thread compatibility, grip and strength remain unqualified. Enlarging
the pilot reduces available grip. These are smooth bores, not CAD threads.

Reprint only the main white front shell appropriate to your chosen variant.
Black parts, white RF window and red parts remain unchanged and reusable.
G1/G2 receiving pilots in the black top housing remain 1.6 mm; through-clearance
holes remain 2.4 mm. All screw axes, bearing planes, lengths and axial cut spans
are retained. The later PWR guard still shortens A3's centerline clear depth.

The current touch-only first build has no external buttons, NFC, GPS or J9 harness.
It uses 17 M2 screws: 4 x 16 mm, 3 x 4 mm and 10 x 8 mm. The optional NFC tray
adds 3 x 8 mm. The button shell remains a supported mechanical alternative;
button/reader integration and physical hardware fit are not verified by this change.

Publication copy: required Waveshare reference meshes are intentionally omitted
because redistribution terms remain unresolved. This checkout is NOT self-contained.
First resolve the external inputs described in ../../REBUILD.md. In a separate
working copy, install cad/requirements.txt into a suitable CPython 3.11 environment
and run python -B cad/build.py. The builder regenerates printable stl/, assembly
meshes, hardware references and nominal checks. Frozen printable artifacts in this
export are under ../../stl/; only print-oriented STLs are printable. Assembly-coordinate meshes and hardware gauges
must not be substituted into a print plate. Existing basenames such as back_shell_C10
and nfc_reader_tray_C13 identify unchanged legacy parts; the containing revision
folder and reference/C14_P19_CONTRACT.json identify this complete C14-P19 package.

Original C13 files and all existing print projects are preserved. No print was run.
Never test a full 16 mm screw directly in the bare white shell: assembled white
penetration is only 6.4 mm, and the bare-shell test can bottom out. Check the actual
screws and printed grip gently with the correct stack before final assembly.

Publication assembly guide: ../../ASSEMBLY.md. The firmware supports touch controls;
this mechanical export does not certify assembled operation or physical fit. No
CAD regeneration or print was performed by this publication step.
