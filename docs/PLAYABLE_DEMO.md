# About the playable browser sample

[Open the demo](play/) · [Return to the showcase](index.html#samples) · [Source repository](https://github.com/goldsziggy/z-ark-digivice)

The interactive sample uses the shared game core for battles, capture and companion state. Simulated walking input triggers the same core encounter generation and probabilities used by the game. The demo starts from a deterministic seed; it does not manually select enemies or change production encounter rules.

Progress is saved separately in this browser. The demo does not pair with a physical Digivice or connect to its save. Use **Reset demo** to start the browser sample again. No account, credentials or game backend is required.

The browser sample uses the game’s exact installed sprite pixels and scene backgrounds, optimized for this demo. It uses the current form catalog, native sprite framing and facing, and lazy loads the visible artwork. Forms without exact art in the installed collection are labeled unavailable, with no substitute creature. [Artwork sources, credits and scope](play/ART_SOURCES.md).

New and reset adventures select **Auto** when the partner hatches. Existing saves keep their selected Auto or Manual mode. Auto battle pauses for eligible manual timing capture; it never aims or throws for you. You can switch battle mode before an encounter.

Walking sensors, device radio and actual nearby players are not connected to this sample. The demo supplies simulated steps, not measured walking. The separately labeled recorded simulator screens and videos on the showcase remain from baseline `f74ee4c`; they are not recordings of this browser sample.
