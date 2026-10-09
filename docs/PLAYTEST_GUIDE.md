# Local browser playtest

Run the complete simulator on this Mac at **http://127.0.0.1:8787**. Care, walking encounters, cards, battles, capture, individual companions and evolution use the shared C++ rules through the local service. Walking and NFC are simulated. This is a browser test. Three actual ESP firmware profiles also compile; no physical board has been flashed or verified.

## Start and return to a save

```sh
cd .
npm run dev:direct
```

The direct build uses the existing C++17 compiler. The optional `npm run dev` command uses CMake for the same host source. Neither installs a toolchain.

This builds the native core and starts the service through `scripts/node.sh`, which reuses the installed compatible Node runtime. No npm install is needed. Leave that terminal running. After a successful build, `npm start` starts the same service without rebuilding. Keep one service process per data directory.

Open the exact URL above in a normal browser window with site storage enabled. Use the same browser profile and URL on later visits: `localhost` and `127.0.0.1` have separate browser storage. A saved identity restores automatically. For a first visit, choose **Set up device** on the round screen, create a pairing code and claim the virtual device. Choose one of eight eggs, review the named Rookie and confirm the hatch; Impmon is the first choice. Existing identities skip this flow. The original diagnostic controls are available under the collapsed **Playtest tools** section.

The round **Menu** contains Care, Companions, Explore, Cards, Settings, Stats, Type chart and Battle. Tap a choice, use arrows and Enter, or use left tap Next / left hold Back / right tap Confirm. Escape and Backspace also go back. Back cancels a capture only before it has been sent; a pending command must finish or retry. [All screens and navigation](DEVICE_NAVIGATION.md).

For a practice duel, choose **Menu → Battle → Start practice → Tactical**. Pick an attack, inspect the revealed moves and damage, then continue to a defensive choice. Enemy hints name two possibilities committed before your input. Try a card or explicitly retreat. Alternatively, choose **Auto**, review, then **Start Auto battle**: no attack, card or timing inputs are required after confirmation. Practice has no reward or care penalty and does not modify your capture save. [Battle stats and boundaries](PRACTICE_BATTLE.md).

To start fresh without losing the current game, use **Start a new playtest**, then **Keep current & start new**. **Stay here** cancels. Use **Saved playtests → Load save** to return to a retained game. The local service supports eight playtests in total; changing games is blocked while an action still needs reconciliation. Do not clear browser storage or delete `.data/` to start over: those hold development identity and save data.

## A short adventure

Start with your hatched Rookie or an existing home-state save. Wait for each confirmed save before the next action. Capture outcomes vary with the saved seed; a miss is normal.

| Step | What to do | What to look for |
| --- | --- | --- |
| Care and sound | Open **Care**, try **Feed** and **Play**. When health or energy is low, choose **Recover fully → Confirm recovery**. **Rest once** remains available. Sound is optional under **Settings → Sound**. | Recovery uses exactly the native count of ordinary Rest actions in one saved request. Review/Back changes nothing. Full recovery is disabled when health and energy are full; it awards no XP. |
| Explore | Choose **Explore → Choose battle mode → Tactical**, then **Explore +100**. | Exactly 100 simulated steps are added. Native saved credit shows remaining steps or queued encounters; a queue needs a later real step or explicit simulated walk to open the next encounter. No GPS or elapsed-time walking is invented. |
| Card and battle | During the encounter, choose **Cards → Spark** or **Shelter**, then use a named physical, heavy or magic move. Inspect the rival's Brace, Ward or Counter. | One card is available per encounter. Counter reflects a heavy hit. The wild rarity label is authored encounter frequency within the eligible pool; it is separate from capture chance. |
| Cancel, then capture | Once **Capture · N%** is enabled, tap it, then tap **Cancel**. Try again: touch the turquoise orb and flick gently upward into the target ring. In Two buttons mode, hold left Back during its 1.2-second preparation to cancel. | Cancel and short taps spend no attempt. A valid aim miss spends one of three attempts; a hit uses the displayed native capture chance and can still escape. Failed attempts can hurt the partner. Defeating the wild companion ends that encounter. |
| Meet the collection | Open **Companions**, then the individual member number and **Set Partner**. | Each member keeps its own care, XP and form. Three cards per page cover the eight carried slots. Partner switching is blocked during encounters and active practice. |
| Make room | With eight carried members in a new encounter, choose **Make room**, open a non-partner and **Release companion → Review release → Release companion**. | Two review screens identify the individual. Release removes its care/XP record; the journal keeps its discovered form. **Return to encounter** resumes the same opponent, health, guard and attempts. Earlier-rules encounters require finishing the battle before release. |
| Watch Auto | At Home choose **Explore → Choose battle mode → Auto**. Walk, then separately confirm **Start Auto battle**. | Walking never starts Auto by itself. The saved battle plays without combat inputs; Back cannot roll it back. Replaying the record adds no capture or XP. |
| Grow and restore | Battle for XP; use **Progression** to inspect level, next-level XP and **Digivolution choices**. Preview requirements, stats and moves before confirming a legal route. Reload afterward. | Care builds bond but does not award battle XP. Level increases do not change form automatically. A confirmed branch keeps the same member ID and saved XP; reload restores it. |

If three capture attempts miss, finish with a combat move, recover at home and explore again. Defeat causes a gentle retreat; missed care causes no death or decay. Digivolution gates are native level/bond requirements on each reviewed route. The short care/capture loop can be tried quickly; reaching later forms requires additional battles, so this guide does not promise a five-minute evolution. See [collection rules](COLLECTION_RULES.md).

## Art and audio

The **Pack library** provides **Little Forest**, **Tidal Pools** and **Ember Hollow**. Select a pack and choose **Download / resume pack**. Preview its creatures and six animation states below. Download the Tide and Ember packs to see those families during play; their absence does not change encounter or collection rules. **Pause** stops a download and keeps validated partial progress. Verified original packs remain in this browser, with a built-in starter fallback.

For this Mac's optional private appearance pack, use **Your personal version → Choose pack.json + provenance.json** and select both files from:

```text
./.personal-assets/personal-ds-line/
```

In the Mac file chooser, Command-Shift-G opens the folder-path field. Choose **Import and use locally**. Toggle **Use personal appearances** to compare with original art; **Remove personal art** removes only that browser's appearance import. The file-import path does not upload those files. The Agumon/Greymon/MetalGreymon appearances can replace the existing original founder line visually. The importer also permits the checked Agumon frame alias for a new Agumon starter; training does not select Champion artwork. These are appearance choices and never change native species or progression. Unavailable action poses are explicitly labelled fallbacks. Credits and restrictions remain attached. See [personal imports](PERSONAL_ART_IMPORT.md).

Browser music and effects are original oscillator compositions, with no franchise recordings or downloaded audio. Music is optional and initially off; the master **Sound on / Sound off** control and volume apply to both music and effects. Audio pauses in a hidden tab. The selected 1.46 board has a fitted speaker, PCM5101 DAC and NS8002 amplifier. Our firmware has no compiled audio driver yet; browser playback does not prove device sound. No vibration hardware appears in the official schematic/BOM. [Actual hardware and bounded next steps](MOTION_AND_SOUND.md).

## Small failure checks

Use a retained test save. These are manual checks to perform, not a claim that opening this guide has executed them.

| Check | Expected result |
| --- | --- |
| Cancel capture before submission | Encounter, health, capture count and save revision stay unchanged. There is no late-cancel promise once a request was sent. |
| Reload after a confirmed action | Active member, collection, stats and save revision restore. No duplicate care or capture appears. |
| Stop the service, then try one care action | The browser keeps the pending action and offers **Retry saved action**. Restart the same service, then retry; the stable request ID prevents duplicate application. Resolve pending work before switching playtests. |
| Reload a paired browser while the API is unavailable | **Save not loaded → Restore saved game** keeps the existing identity. Reconnect to load the same save; do not create another identity or clear storage. |
| Lose the reply to Recover fully or a confirmed release | The complete original batch stays pending. Back cannot remove it. Retry after reconnecting; recovery or release is not applied twice. |
| Open a second tab for the same save | One tab owns gameplay input; the other does not race writes. Closing the first allows the remaining tab to reconnect and continue. |
| Pause an original-pack download, then resume | Only validated partial data is reused. Slow the browser network in developer tools if local transfers finish too quickly to pause. Gameplay saves remain separate from asset downloads. |
| Keep a loaded gallery open, then disconnect requests | Cached artwork still renders. Browser gameplay and a cold page load require the local service; there is no service worker or claim of full browser offline play. |
| Import a mismatched pack/sidecar pair | Validation rejects it while preserving the previous usable appearance and game save. Test copies only; keep the source pair intact. |
| Mute, change tab, then reload | Mute stops sound; backgrounding pauses audio. Reload does not start audible playback without another user gesture. |

If the UI reports a save conflict, first read the notice. **Discard local action** intentionally abandons that pending action after fetching the current save; it is not a general repair or reset button. A storage error should preserve existing data. Do not delete identity or pending-action records as troubleshooting. Record the visible error, action, save revision and browser; exclude bearer tokens and private artwork from shared evidence.

## Phone and Garage status

**No phone URL is configured.** The current service accepts only loopback connections with matching Host/Origin checks. The Mac's observed LAN address is not a supported playtest URL. Phone use needs a separately configured trusted HTTPS origin, an explicit access policy and actual device verification. The browser uses `crypto.randomUUID()` and `crypto.subtle`; these require a secure context, whereas plain LAN HTTP does not receive localhost's special treatment. No firewall, router, tunnel, certificate or public hosting changes were made. [Web Crypto specification](https://w3c.github.io/webcrypto/#Crypto-interface), [secure-context origin rules](https://w3c.github.io/webappsec-secure-contexts/#is-origin-trustworthy)

Garage is optional; local original packs and personal file import work without it. The currently authorized `jpgen` and `pocket-pals` buckets have website hosting enabled and are ineligible for this connector. It must refuse them. No Digivice objects were uploaded and no bucket settings, grants or credentials were changed. A future target must be an explicitly selected private bucket already authorized for the existing key; this work does not create a bucket, key or grant. A Garage endpoint is storage, not a phone-browser hosting URL.

When an eligible target is available, the connector exposes its catalog and downloads through the paired-device-authenticated local service, not through public S3 URLs or browser credentials. Until then, a disabled/unavailable Garage shelf is expected and should not block the adventure. See [service API](../service/API.md) for the final connector contract.
