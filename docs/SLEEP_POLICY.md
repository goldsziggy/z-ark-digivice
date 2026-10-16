# Screen idle

Automatic sleep blanks the backlight on both Waveshare profiles. It does not
enter CPU deep sleep or stop care, touch, motion, usage accounting or radio work.
The existing Off/30/60/120/300-second preference and save schema are unchanged.

Background care applies and verifies its save once per running minute when
eligible, including while blanked. Its sequence is acknowledged only when no
foreground publication is pending. It does not generate input activity or replay
the last game message's sound.

Passive screens may blank: Home, Care, Explore, starter selection/review,
collection/party/statistics, evolution and release reviews/results, settings,
mode review, dungeon selection, inter-floor results and discovery lobbies.
Pending touch/commit, capture, encounters, battle playback, active practice,
setup, invitations, trades and active Nearby sessions retain visibility. A
Nearby or dungeon lobby may blank only during stable discovery with no trade
negotiation. Discovery continues; an incoming invitation wakes the display.
Blanking never cancels or advances a match or transaction.

Temporary audio effects, network probes/scans/joins/backoff, asset work, display
transfers and unavailable sensors defer entry without resetting inactivity or
waking a blank display. Existing save/readiness gates, the forced usage
checkpoint, its safety recheck and bounded failure behavior remain in place.
The 1.46-inch power controller's release-to-arm recovery after a polling gap
does not count as input. Actual PWR contact still wakes/extends visibility;
the conservative long-press shutdown controller is unchanged.

Touch wake still consumes the waking contact and requires release. Motion wake
still requires two fresh qualifying samples. Power/USB suspension retains its
separate stronger barriers. The 2.1-inch profile has no PWR-contact idle gate.

## Regression checks

`bash firmware/tests/test_idle.sh` exercises idle deadlines, clock reversal,
motion freshness/noise, preferences/corruption and backlight/wake failure paths.

`bash firmware/tests/test_idle_runtime.sh` compiles the exact production care,
idle and usage methods, the input-loop prefix and the real power controller
against RAM saves and hardware/UI doubles. It covers ten running minutes at
30/60/120/300 seconds with aligned/offset care deadlines, care persistence,
foreground publication retention, all screens, active/passive transitions,
discovery and protocol stages, temporary blockers, polling gaps, touch and
motion wake, and failed checkpoints. It does not verify physical brightness or
battery current.
