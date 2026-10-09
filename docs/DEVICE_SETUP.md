# On-device Wi-Fi and phone-hotspot setup

**Current installed build:** clean firmware source **`6e058e1`** is compiled and pinned (historical local evidence omitted) and passed independent app-only installation on both units. Each own save migrated to **636 bytes / schema 15 / rules 12**, preserved 62 prior values and saved three offers at sequence 1 without hatching. All 261 asset hashes, eleven starter previews and own-save software reboot passed. At about 73 seconds, both recorded **2,589 touch polls / zero errors / zero lock misses** with fresh IMU samples. Screen-idle diagnostics reported blanking after 60 seconds with sampling still active; console wake passed. Finger typing, physical touch/motion wake and hotspot/TLS remain unverified. Current physical evidence (historical local evidence omitted) · [Release guide](CARE_CAPTURE_RELEASE.md).

**Retained `2d9f1ed` checkpoint:** the walking/nearby build adds a separate radio lease: entering Nearby pauses hotspot activity, and leaving restores the previous network setting after teardown. [Release and migration notes](WALKING_NEARBY_RELEASE.md). Unit 1 passed **`2d9f1ed`** with its own unhatched **600-byte** egg save restored after reboot, all **261 SD hashes** verified and **868 touch polls / zero errors / zero lock misses** during sampled IMU coexistence. Unit 2 also passed the `2d9f1ed` upgrade, its own 600-byte egg/reboot check, all 261 file hashes and 877 touch polls with zero errors/lock misses. Retained device evidence (historical local evidence omitted). The keyboard below is implemented; physical finger use and an actual hotspot/TLS connection remain unverified.

The game works offline. Setup changes network settings separately from the pet/practice saves; opening setup does not hatch the egg or change progression. No native phone app is required.

## Connect to a hotspot

1. Enable a **2.4 GHz WPA2-Personal** hotspot. ESP32-S3 Wi-Fi operates in the 2.4 GHz band ([Espressif datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)). On iPhone 12 or later, turn on **Maximize Compatibility** in Personal Hotspot settings: Apple documents that this restricts the hotspot to 2.4 GHz and WPA2 Personal ([Apple security guide](https://support.apple.com/guide/security/wi-fi-security-with-apple-devices-secfd166f620/web)).
2. On the Digivice egg screen, tap **SETUP**. From a hatched partner’s Home, use **SETTINGS → WIFI SETUP**.
3. Tap **SCAN**, then select the hotspot on **CHOOSE WI-FI**. Use **< PREV / NEXT >** for more results. Unsupported security choices are disabled. For a hidden network, choose **BACK → HIDDEN SSID**, enter the network name and tap **NEXT**.
4. Enter the password privately on the device’s **WI-FI PASSWORD** screen, then tap **SAVE**. It remains masked. **ABC**, **123/#**, **MORE #** and **abc** cycle keyboard pages; **SPACE** and **DELETE** edit the draft. **BACK** discards it and returns to the status screen.
5. The status screen can show **SAVED - CONNECTING**, then **WI-FI: CONNECTED**. Use **RECONNECT** for a deliberate retry or **CLOSE** to return to the game. Connection may fail without changing your game save.

The bounded keyboard accepts an SSID of 1–32 bytes and a password of 8–63 printable ASCII characters, or exactly 64 hexadecimal characters. The scan retains at most 16 networks and displays three per page. Closing setup or suspending power clears drafts and cancels an active scan; changed input context requires a fresh touch.

## Clock and service status

| Display | Meaning |
|---|---|
| **WI-FI: NOT SET / CONNECTING / OFFLINE / CONNECTED** | Whether settings exist and whether the device has an IP connection |
| **CLOCK: NOT SET / WAITING / FAILED / READY** | Bounded SNTP clock bootstrap; HTTPS requests wait for a usable clock |
| **SERVER: NOT SET** | No service origin saved; local play remains available |
| **SERVER: UNREACHABLE / READY** | Result of the configured service health check, separate from Wi-Fi and clock status |

**Leave SERVER: NOT SET for now.** No reachable production endpoint has been verified. The development service defaults to `127.0.0.1:8787`; a phone hotspot cannot expose that loopback address to the handheld. Its explicit private-LAN mode is HTTP development access and does not satisfy the standard firmware's HTTPS requirement. This describes available configuration, not a currently running service. Wi-Fi or clock readiness alone does not prove service access, pairing or save sync; native pairing/save sync remains unimplemented. No new hosting, subscription or deployment has been configured.

Once a reviewed, reachable HTTPS origin is supplied, tap **SERVICE**, enter the origin and tap **SAVE**. The field starts with `https://` and allows at most 192 bytes; supply an origin rather than a path. Certificate verification remains enabled. Standard firmware offers HTTPS only; the separate development build’s private-HTTP opt-in is outside this guide. The new clock bootstrap and TLS path still need physical verification.

## Credentials and forgetting a network

Enter the real hotspot password **only on the device**. The standard build disables USB credential-entry commands; do not put a password in chat, shell commands, screenshots or diagnostic logs. `net status` and `device status` remain useful read-only console diagnostics. The keyboard never displays the password, and drafts are wiped after transfer, cancel or close. Saved credentials remain local in **unencrypted prototype NVS**; this is not a production credential vault.

**FORGET → FORGET WI-FI** explicitly removes the saved Wi-Fi and service address. **CANCEL** keeps them. The confirmation screen states **YOUR GAME SAVE IS KEPT**; pet/practice progress is not erased. This is separate from removing a card, resetting a game or erasing flash.

Network configuration remains a **304-byte `DNET` record**. Existing v1 records with a service endpoint retain their format; the new v2 record permits Wi-Fi-only setup with an empty endpoint. Older firmware that does not understand v2 reports an unsupported configuration instead of silently wiping it. Network setup does not change pet saves. Separately, current source migrates care through schema 14 / 600 bytes to schema 15 / 636 bytes; the partition layout is unchanged. [Migration details](WALKING_NEARBY_RELEASE.md#save-migration-and-durability).

## Checks and remaining work

The current native visual review records **1,454 UI checks** and **2,382 setup checks** across **50 synthetic states**. The retained `2d9f1ed` build record separately has **757 network policy**, **96 network adapter** and **163 shared-I²C touch-poll** checks. They cover bounded entry, masking/draft lifecycle, explicit forget, stale touches, Wi-Fi-only configuration, network policies and touch cancellation/recovery. They do not establish real hotspot/radio/TLS behavior or comfortable typing on the panel. Native review (historical local evidence omitted) · Retained build evidence (historical local evidence omitted).

Unit 1's existing exFAT card mounts after enabling pinned exFAT support; its earlier mount error is resolved. Its USB installation completed **261 destination hashes**, eight starter-sprite decodes and the egg-background JPEG decode without formatting. The private pack contains **261 files / 4,700,573 bytes**, covering **251 of 276 forms plus eight scenes**; **25 forms retain fallback art**. A Mac card reader is optional for the USB installation path. Unit 2 also passed the full-pack/decode checks on `2d9f1ed`; both units then reverified all 261 hashes and eleven starter decodes on `6e058e1` without copying additional assets. Its historical `90ee501` observation—roughly **62.9 GB**, `cacheReady=1`, **187 entries, 171 lookup errors and two cache-name entries**—does not establish an empty/corrupt card or verified artwork. [Two-device preparation](TWO_DEVICE_PREPARATION.md) · [Card layout](INSTALLATION.md#5-microsd-layout-and-assets).


Current source provides **Settings → Screen Timeout**, default **60 seconds**, cycling Off/30/60/120/300. This blanks the display only; touch and step sampling continue, and a wake contact cannot select a menu item. Active setup/keyboard work blocks blanking. Physical wake behavior and current savings await acceptance; no deep sleep is implemented.
