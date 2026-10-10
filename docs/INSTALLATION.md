**Current source package:** `53686e6`, schema 23/rules 16. Read the [current package instructions](../releases/firmware-53686e6/README.md) and [publication validation](../PUBLICATION_VALIDATION.json). The [171cda7 package](../releases/firmware-171cda7/README.md) and the retained checkpoints below are historical.

# Install Digivice development firmware

> Current exported source: `66ceaaf` (schema21/rules14, 60 Digimon). [Current build and migration guide](ROSTER60.md) · [Publication validation](../PUBLICATION_VALIDATION.json). Installation statements below describe their named historical checkpoints; this export preparation does not assert a new physical installation.

> Historical publication checkpoint: source and previously installed firmware were f74ee4c (schema 17 / rules 13). Both units passed bounded installation/save/SD/reboot checks; older entries below are historical. No hardware was accessed for this export. See [verified images and remaining acceptance](../releases/firmware-f74ee4c/README.md).

> This retained runbook documents the older `6e058e1` installer and checkpoints. Its historical packaging/flash commands do not install either the historical f74 package or the current 66ceaaf raw build images. Follow the [f74 installation boundary](../releases/firmware-f74ee4c/README.md) and review a release-specific installer before any update.

**Historical installed release:** **`6e058e1`** uses **schema 15 / rules 12 / 636-byte saves**, service container 14, saved starter offers, care bonuses, carousel controls, capture-result playback and screen idle. The pinned build (historical local evidence omitted) passed app-only installation and the bounded device checks below on both units. [Historical feature/run guide](CARE_CAPTURE_RELEASE.md). The older **`2d9f1ed`** schema-14/rules-11 checkpoint remains historical evidence. Preserve each device’s own backup before any further migration or downgrade.

**Historical two-device installation status:** both units ran **`6e058e1`** at that checkpoint. Each own **600→636-byte** save exactly matched host migration plus one offer event at sequence 1, preserving **62 prior leaf values**. Both were unhatched at that checkpoint, with three distinct saved offers; all eleven starter previews, the scene JPEG and software reboot restored each own snapshot. Each existing pack passed **261 SHA-256 checks / 4,700,573 bytes**, with no additional assets copied. Both recorded **2,589 touch polls / zero errors / zero lock misses** with fresh valid IMU sampling at about 73 seconds. The default 60-second idle reported blanking and stopped rendering while touch/IMU continued; console wake and unchanged saves passed. Physical brightness, finger/motion wake, walking, speaker and RF acceptance remain open. Historical physical evidence (local evidence omitted) · [USB transfer runbook](USB_SD_TRANSFER.md) · [Per-device checkpoint](TWO_DEVICE_PREPARATION.md).

For **Waveshare ESP32-S3-Touch-LCD-1.46, standard glass, SKU29565** only: 412 × 412, 16 MiB flash and 8 MiB PSRAM. The units have independent backups and save records. Preserve each unit’s own identity and save; do not clone one device’s NVS to the other. Unit 2 returned with its exact USB identity, then passed its own app-only upgrade, full asset transfer and saved-egg reboot check. A cable/port swap was not confirmed. Its earlier `90ee501` boot/software-reboot/save results remain in the [per-unit preparation record](TWO_DEVICE_PREPARATION.md).

**Earlier USB command input and saved-state recovery are verified on `006488e`.** The minimal `006488e` repair compiled and its app-only flash hash verified on 8 October 2026. After a USB-triggered reset, the port stopped returning data; the user's USB cable reconnect recovered communication with the battery still attached. Actual `status`, `snapshot` and `capabilities` replies, fragmented input, overlong-line rejection, and a software `reboot` all passed. The game state matches the preflash egg and its canonical snapshot is identical after reboot. No battery disconnect was needed. Historical repair evidence (local evidence omitted) · Sanitized command/boot excerpt (historical local evidence omitted).

The retained hardware-tested artifact **`2d9f1ed` compiled without warnings with ESP-IDF 5.3.6**, producing a **1,531,584-byte app** in the existing 3 MiB app slot, with a **16 KiB main-task stack** and pinned checkpoint source/artifact hashes (historical local evidence omitted). All 231 recorded source hashes match this build; the installer passed 16 checks. The shared-I²C touch fix allows a bounded **20 ms mutex wait**, then starts the separate **16 ms transfer deadline**; 163 sanitizer checks (historical local evidence omitted) cover contention and gesture cancellation. The project-local [official I²C NACK timeout backport](../firmware/components/esp_driver_i2c/README.digivice.md) retains its 75 passing checks (historical local evidence omitted). This target adds physical-step sampling, saved encounter pacing, collection/Digivolution screens, battle presentation and friendly Nearby duels alongside the existing SD/setup/USB features. Neither unit has an NFC reader; GPS, external gameplay buttons and deep sleep remain unavailable. Physical walking accuracy still needs acceptance. Care migrates to schema 14 / 600 bytes; partition boundaries are unchanged. Preserve the device's own backup and use the reviewed installation mode for its existing layout. Historical builds remain archived separately (historical local evidence omitted).

The historical **`6e058e1`** artifact compiled with **zero warnings**, producing a **1,547,280-byte app** with **1,598,448 bytes free in the 3 MiB slot**, **166,831 bytes of static DIRAM** and the existing **16 KiB main-task stack**. The build record (historical local evidence omitted) pins all **241 source hashes** and binaries. Static allocation does not establish runtime memory margins. It upgrades care to schema 15/636 bytes without changing partitions. Both boards passed their own installation verification; physical gameplay acceptance remains separate.

On each unit, after checking its installed source and own save and completing the asset transfer, use **MEET PARTNER → CHOOSE → HATCH** when deliberately ready to hatch, then care and **Home → Explore** for physical walking encounters. Production touch screens do not offer simulated-step buttons; browser/CLI step injection remains a development harness. Choose encounter pace and Tactical/Auto mode in **Settings** before battle; Tactical capture uses an upward flick. SD artwork can replace the original abstract fallback only after valid mounting/decoding. Sound starts on at 15% software volume with mute; gyro starts off and only tilts the avatar, independently of the pedometer. The egg’s **SETUP** and Home’s **SETTINGS → WIFI SETUP** open the [on-device hotspot keyboard](DEVICE_SETUP.md). These are implemented paths awaiting user acceptance. [Current native controls](BATTLE_STATS.md) · [Walking and Nearby](WALKING_NEARBY_RELEASE.md).

For the historical runbook's large-creature screens, swipe horizontally through starters/partners/stat pages and evolution choices. In Tactical battle, horizontal swipes select Physical/Heavy/Magic; an upward swipe commits. Catch opens its own upward-flick orb. Rules-12 throws have no retaliation; three failures end the encounter, with the committed result shown before input unlocks. **Settings → Screen Timeout** defaults to 60 seconds and cycles Off/30/60/120/300; touch and steps stay live while blanked. This is screen idle, not deep sleep. [Full controls, care and saved offers](CARE_CAPTURE_RELEASE.md).

## 1. What you need

- The exact board above, accessible BOOT and PWR controls, and a USB **data** cable. Begin on USB power with other serial monitors closed.
- Python 3.10+ to validate/package/preview. Actual flashing uses the official **esptool 4.12.0** already included in this project's ESP-IDF environment. No Node installation is needed for flashing.
- The extracted installation ZIP, or the source checkout and existing build. Keep the guide, helper and firmware directory together. No software installation or flashing starts when you unzip it.
- Optional for the initial game/console playtest: an existing compatible FAT32 or exFAT microSD card. The current profile supports MBR/unpartitioned cards, not GPT. A mounted card is required for the complete SD pack; [USB installation](USB_SD_TRANSFER.md) does not require a Mac card reader. This guide/helper does not format media.

On this Mac the official tools already exist. In Terminal:

```sh
digivice_python="${DIGIVICE_TOOLCHAINS}/espressif-v5.3.6/python_env/idf5.3_py3.13_env/bin/python"
"$digivice_python" -I -m esptool version
```

On another Mac, set `digivice_python` to your installed IDF Python executable. If tools are missing, follow Espressif's [ESP-IDF 5.3.6 setup instructions](https://docs.espressif.com/projects/esp-idf/en/v5.3.6/esp32s3/get-started/linux-macos-setup.html) as a separate setup task. The helper installs nothing and refuses an unexpected esptool version.

## 2. Choose the USB port

Change to the extracted package directory, then list ports before and after connecting the board:

```sh
cd "/path/to/Digivice_Install_Waveshare_146"
"$digivice_python" flash-device.py ports
```

On macOS the native USB serial device commonly appears as `/dev/cu.usbmodem…`. Select the port that appears for this board; listing does not open it. Do not use a Bluetooth port or assume an example name is real.

```sh
digivice_port="/dev/cu.usbmodemREPLACE_WITH_YOUR_PORT"
```

If the port never appears or automatic connection fails, Waveshare documents this download-mode sequence: hold **BOOT**, reconnect the USB cable, then release BOOT after reconnection. Re-list ports because its name may change. Use this with the board actually cycling USB power; a connected battery can keep it running. Do not substitute PWR for BOOT or improvise battery wiring. See the [board FAQ](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46/FAQ).

## 3. Preview, then choose initial install or upgrade

All helper flash commands are **dry runs unless `--execute` is present**. Previewing checks the package and prints a command without opening a serial device. Its checks cover the selected board/profile, exact release hashes, binary partition table, file bounds and flash layout. Hashes detect differences from this development release; they are not a publisher signature.

For the first installation on a board, preview:

```sh
"$digivice_python" flash-device.py flash --package firmware \
  --port "$digivice_port" --python "$digivice_python" \
  --initial-install --dry-run
```

Initial installation replaces the bootloader, partition table, app and OTA boot-selection records. It selects this release's first app slot. **It is not a migration from an arbitrary vendor firmware:** old layouts may store data in regions this install replaces, and existing unrelated NVS is not cleared. Keep any previous firmware/data backup separately before replacing it. Do not proceed on encrypted/Secure Boot boards or a different board revision using this helper.

When you deliberately choose to write the physical board, the command is:

```sh
"$digivice_python" flash-device.py flash --package firmware \
  --port "$digivice_port" --python "$digivice_python" \
  --initial-install --acknowledge-boot-selection-reset --execute
```

For an **existing Digivice install with this same partition table and `ota_0` active**, use app-only upgrade instead:

```sh
"$digivice_python" flash-device.py flash --package firmware \
  --port "$digivice_port" --python "$digivice_python" --dry-run

# Run separately, only after checking the preview and existing installation:
"$digivice_python" flash-device.py flash --package firmware \
  --port "$digivice_port" --python "$digivice_python" \
  --acknowledge-existing-layout --execute
```

There is no OTA updater in this firmware. The helper cannot prove the board's existing partition layout or active slot. An app-only write does not change boot selection. Unknown layouts or an `ota_1` selection require a reviewed recovery plan, not repeated blind upgrades.

The helper uses official esptool `write_flash`, explicit ESP32-S3 selection and the build's DIO / 80 MHz / 16 MiB settings. It never passes `erase_flash`, `--erase-all` or `--force`. Esptool erases the sectors it writes; this is expected and differs from a whole-chip erase. [Espressif command behavior](https://docs.espressif.com/projects/esptool/en/release-v4/esp32/esptool/basic-commands.html).

These offsets come from the existing build's `flasher_args.json` and binary partition table; they are reference information, not commands to copy from another board:

| Region | Build-derived offset | Written by |
| --- | --- | --- |
| Bootloader | `0x0` | Initial install only |
| Partition table | `0x8000` | Initial install only |
| NVS, 64 KiB | `0x9000` | Neither helper mode |
| OTA selection, 8 KiB | `0x19000` | Initial install only; resets selection |
| App `ota_0`, 3 MiB slot | `0x20000` | Both modes |
| App `ota_1` | `0x320000` | Neither mode |
| Historical raw asset reservation | `0x620000` | Neither mode |

Do not write `digivice.bin` at zero. Waveshare's single-file factory example has its own packaging and is not interchangeable with these separate build artifacts.

## 4. Open the console and check first boot

After flashing completes, allow the board to restart, then re-list ports if needed. Open its USB Serial/JTAG console with the IDF environment's serial terminal:

```sh
"$digivice_python" -I -m serial.tools.miniterm \
  "$digivice_port" 115200 --dtr 0 --rts 0 --raw --eol LF
```

Use **Ctrl+]** to exit. Local echo is off by default. Commands belong in this serial console, not in your shell. Standard firmware accepts network credentials only through the device’s masked setup keyboard; never enter them in this console.

Check the Waveshare board name and embedded version against the package/build record (`6e058e1` for this historical pinned artifact; `2d9f1ed` for the retained hardware checkpoint), `Boot storage:`, practice-storage and SD-cache/fallback messages, then the `Device init:` diagnostics. `device identity` reports the board, firmware version and full chip MAC for USB installer matching; keep identity logs private. `device status` reports LCD/touch/audio/IMU initialization, frames, touch presses/releases, calibration, maximum frame time and free/largest heap. Successful initialization or a frame counter alone does not prove visible colors, matching coordinates or audible sound. An unchanged saved egg should remain an egg. Quiet screens may intentionally blank after the configured idle timeout (60 seconds by default); check reported idle state before diagnosing display failure. An unexpected blank screen at boot or failed wake still needs investigation.

These commands inspect without changing game state. `status`, `snapshot` and `capabilities` replies were physically verified on `006488e`. Do not erase or reflash repeatedly if both the console and esptool stop receiving data:

```text
status
capabilities
starter status
assets status
motion status
net status
power status
device status
```

If both save slots are empty, firmware creates a saved egg. Existing or uncertain saves do not silently restart onboarding. To try a fresh egg, `starter confirm` advances from the egg to selection; `starter next` cycles choices and `starter confirm` opens the confirmation step. Check each printed prompt and confirm the intended starter. `starter back` goes back before committing. `help` lists care, simulated `walk 100` / `card 1`, encounters and practice commands. These actions change the local save.

If the console reports `RECOVERY` or an uncertain save, stop gameplay and preserve the output and device. Do not erase to dismiss the message. The project has host save-failure tests, but real flash brownout recovery remains unverified.

## 5. microSD layout and assets

Unit 1’s historical `5edec43` run mounted its existing exFAT card without formatting and completed **261 destination SHA-256 checks** for **4,700,573 bytes**. That transfer measured **733.237 seconds**, **6,410.7136 acknowledged bytes/second**, and **zero command retries**. After its app-only upgrade to `8a420c7`, all **261 hashes passed again**, and the installation pause was released. The board decoded all eight starter DVA files and the egg-background JPEG, then restored its unchanged own snapshot across a software reboot. Historical deployment evidence (historical local evidence omitted). The `2d9f1ed` checkpoint repeated all **261 hashes**, eight starter decodes, JPEG decode and saved-egg reboot verification; it copied **no new assets** and released the inspection lease. Retained checkpoint evidence (historical local evidence omitted). These results verify the files and tested decode/reboot paths; visual appearance and arbitrary sudden-power-loss recovery remain separate checks.

Unit 2’s earlier `90ee501` firmware mounted a roughly **62.9 GB** card and reported `cacheReady=1`, but its directory inventory was inconclusive: **187 entries, 171 lookup errors and two matching cache-name entries** do not establish an empty or corrupt card. The later `2d9f1ed` run resolved access, copied/verified all 261 files and decoded eight starter sprites plus the scene JPEG. The earlier inventory remains an inconclusive historical observation.

The private staged pack contains **261 files / 4,700,573 bytes**, covering **251 of 276 forms plus eight scenes**; the other **25 forms retain fallback art**. Use the [USB-to-SD installation runbook](USB_SD_TRANSFER.md): validate the complete local pack, select the unit by its full USB serial/chip MAC, inspect destination hashes, then execute missing-file transfers. The firmware pauses play during installation, uses bounded resumable staging and verifies the complete file hash before publication. The host checks every final destination hash. Identical files are reused; differing or unknown files are preserved and stop conflicting operations. A card reader remains an optional alternative. No formatting, repair or save cloning is performed.

Current FAT/exFAT support comes from the [pinned project-local component](../firmware/components/fatfs/README.digivice.md); MBR/unpartitioned cards are supported, GPT is not. Keep stable power: FatFs publication refuses an existing destination but is not guaranteed atomic under sudden power loss. The USB runbook describes retained partials, recovery limits and private reports. [Earlier prepared-pack checkpoint](TWO_DEVICE_PREPARATION.md).

The combined standard build enables the user-requested local SD artwork path. The physical renderer requests the selected form and animation, validates DVA metadata/CRC and draws masked frames; scenery comes from fixed JPEG paths. A missing, mismatched or invalid asset retains the original abstract fallback. No private sprite packs are bundled or uploaded, and the installer grants no artwork rights.

```text
card root/
  DVASSET1.CCH       firmware-owned cache, exactly 696,832 bytes
  DSF00011.DVA       example exact-form filename; no private pack supplied here
  INDEX.JSON        local export/provenance records
  ATTRIB.TXT
  MEADOW.JPG        optional reviewed meadow background; other allowlisted scenes below
```

`DVASSET1.CCH` is a journaled binary store, created only if absent on a mounted compatible card. Do not put loose JPEG/DVA files into it, replace it with another file or delete it as routine recovery. Game saves live in onboard NVS, not on the card. No automatic formatting or hot-remount is implemented; power down before changing cards and preserve a problem card before separate recovery.

The existing `scripts/prepare-world-ds-sd.py` stages audited local `DSFnnnnn.DVA` packs, `INDEX.JSON` and `ATTRIB.TXT`; it does not stage scenery. The renderer uses exact form IDs rather than substituting a different creature. Those packs remain local and separate from firmware/source bundles.

Optional background aliases at the card root are `MEADOW.JPG`, `FOREST.JPG`, `BEACH.JPG`, `RUINS.JPG`, `CAVERN.JPG`, `SNOW.JPG`, `VOLCANIC.JPG` and `DIGITAL.JPG`. Existing long paths are also allowlisted: for meadow, `scene-meadow-412-v1.jpg`, `assets/device/scene-meadow-412-v1.jpg`, `assets/backgrounds/jpeg/meadow-480.jpg` or `meadow-480.jpg`, with the corresponding scene names for the other seven. Only validated, bounded baseline JPEGs are accepted and resampled to 412² RGB565. This path support does not prove that these files are present on the inserted card.

The loader keeps at most one decoded background and one exact form’s animation frames in PSRAM; animation does not reread the card per frame. The renderer itself performs no filesystem I/O and keeps controls opaque. Actual combined memory/timing and SD-removal behavior still need measurement.

## 6. Optional on-device Wi-Fi setup

Use the egg’s **SETUP**, or **SETTINGS → WIFI SETUP** after hatching. **SCAN** selects a 2.4 GHz WPA2-Personal hotspot; **HIDDEN SSID** permits manual entry. Enter the masked password privately on the device, then **SAVE**. **RECONNECT** retries, **CLOSE** returns to play, and **FORGET → FORGET WI-FI** removes network settings while keeping the game. [Exact keyboard, iPhone compatibility, clock/service states and storage details](DEVICE_SETUP.md).

The game works offline. The combined build implements bounded SNTP clock bootstrap before HTTPS requests; certificate verification remains enabled. No reachable production service is verified. The development service defaults to loopback; its explicit private-LAN HTTP mode does not satisfy standard firmware's HTTPS requirement or implement native pairing/save sync. Leave **SERVER: NOT SET** until a reviewed origin is supplied. Actual hotspot, clock and TLS checks remain pending on both units. At that earlier checkpoint, both units completed the `6e058e1` app-only upgrade, own-save migration and asset verification. These checks do not verify hotspot, clock or TLS connectivity.

Standard USB credential entry is disabled. Do not type real passwords in the serial terminal, shell, chat or logs. Read-only `net status` and `device status` remain available once the app responds. Credentials are stored locally in unencrypted prototype NVS, separate from pet/practice saves. The new 304-byte `DNET` v2 record permits Wi-Fi without an endpoint; existing endpoint-bearing v1 records remain compatible.

## 7. Build or reproduce the package

From the source checkout on this Mac:

```sh
cd .

# Package the existing reviewed build into a new directory; never opens a port:
mkdir -p ../deliverables/my-digivice-install
"$digivice_python" scripts/flash-device.py package \
  --output ../deliverables/my-digivice-install/firmware

# Copy the helper and editable guide alongside that firmware directory:
cp scripts/flash-device.py ../deliverables/my-digivice-install/flash-device.py
cp docs/INSTALLATION.md ../deliverables/my-digivice-install/INSTALLATION.md
```

The package command refuses to overwrite an existing output directory. It checks against `docs/evidence/esp-build-physical-playtest.json`; only the four allowlisted firmware binaries and package metadata are copied. Source, SDK, credentials, NVS dumps, SD caches and private art are not swept into the package.

**Package and preserve the reviewed binaries before rebuilding.** A fresh rebuild can change binary bytes, including its embedded version. Packaging deliberately refuses a changed build until a maintainer refreshes and reviews build evidence and the release pins. Do not change hashes merely to silence a mismatch. This historical helper only packages its pinned older artifacts; it cannot install the bundled f74 or 66ceaaf images. A later release needs its own reviewed installer.

For source development, after preserving that package, this optional command rebuilds with the already-installed official SDK and never flashes:

```sh
bash scripts/build-esp.sh waveshare
```

Build wrappers regenerate `sdkconfig` from committed defaults; manual edits inside the generated build directory are replaced.

For an already installed SDK elsewhere, the build wrapper accepts `DIGIVICE_IDF_PATH`, `DIGIVICE_IDF_TOOLS_PATH` and `DIGIVICE_IDF_PYTHON_ENV`. See `scripts/build-esp.sh --help`. Neither the wrapper nor the flashing helper downloads tools.

## 8. Upgrades, recovery and troubleshooting

| Symptom | What to do |
| --- | --- |
| No serial port | Try a known data cable and direct USB port. Use BOOT + USB reconnect, then list ports again. Do not install arbitrary drivers automatically. |
| Port busy / failed connection | Exit other monitors, confirm the port and board, enter download mode, retry deliberately. |
| Package/hash/profile mismatch | Stop and recover the intact package; do not bypass validation or edit its manifest. |
| Flash completed, screen blank | Check reported screen-idle state first: blanking after the configured timeout is intentional. For unexpected boot blanking or failed wake, inspect `device status`, exact app version and initialization errors. Preserve the save and backup; do not erase or repeatedly flash. The earlier USB-only release intentionally had no renderer. |
| No console output | Observe whether the screen/app responds before another USB open. A later Unit 2 probe reproduced silence after bootloader output; the cause and recovery remain unresolved. Use the [bounded diagnostic](USB_BOOT_RELIABILITY.md), not a blind reopen/reconnect loop or an unframed Enter that could submit stale input. A cable reconnect recovered one historical session but is not a verified reliability fix. SKU29565 has PWR and BOOT, with no documented RESET button; do not substitute PWR for a hard reset. |
| Reboot loop | Preserve serial output and identify the exact board. Vendor examples reference IDF 5.3.2; our 5.3.6 compilation is not physical proof. Do not blindly downgrade over saves or erase flash. |
| SD unavailable / corrupt cache | Continue with resident art; stop, power down and back up the card before review. No automatic formatting or cache deletion. |
| Network remains offline | Offline gameplay remains usable; check the documented clock/service/profile limitations. Never paste credentials into an issue report. |
| Save recovery / newer schema | Keep NVS intact and return to the matching or newer supported firmware. Do not flash an older release to force a fresh game. |

The app-only flash command does not write boot selection, NVS or SD. Running firmware can update saves, including its checkpoint before reboot. Firmware migrations preserve known saved versions; unknown/corrupt states stop in recovery. Save retention across a normal software reboot is verified; power-loss and endurance tests remain. A **whole-chip erase would destroy onboard pet/practice saves, Wi-Fi configuration and boot metadata**; an NVS erase would destroy pet/practice saves and network configuration. Neither is offered by this helper. Initial install also does not wipe unrelated NVS left by other firmware; that may need a separate, explicitly reviewed migration or reset.

The implemented PWR gesture holds for three seconds, saves/drains work, then waits for release before lowering the battery latch. Its countdown is serial-only. With USB supplying power the processor may remain in quiet standby; a fresh short PWR press/release resumes. This is not measured deep sleep. Battery startup, latch shutdown, SD durability, USB survival and runtime current require the actual board.

## Verification boundary

**Historical `6e058e1` checkpoint:** each board had a fresh **98,304-byte layout/NVS backup**, exact identity match, valid boot selection, matching partition layout and verified prior app digest before its app-only update. The new app hash verified; neither a whole-chip erase nor SD formatting occurred. Each own save migrated **600→636 bytes**, retained all **62 prior values**, appended one starter-offer event and survived software reboot unchanged at sequence 1. Eleven starter DVA previews and the scene JPEG decoded; render counters advanced **13→39** on unit 1 and **14→39** on unit 2. All **261 existing asset hashes / 4,700,573 bytes** passed with inspection leases released and no extra asset copies. Neither egg was hatched.

At about **73 seconds**, both units reported **2,589 touch polls / zero errors / zero lock misses**, fresh valid IMU samples, incomplete calibration and writable zero lifetime/session usage. Unit 1/2 respectively reported **173,887 / 173,823 internal bytes free** (both largest **77,824**), **7,690,188 PSRAM bytes free** (largest **7,602,176**) and **8,384 / 8,288 bytes minimum free main-task stack**. Maximum observed frames were **516,842 / 524,079 µs**, including initial JPEG decoding; these are not steady FPS or gesture latency. The 60-second idle reported backlight blanking and stopped rendering while touch/IMU sampling continued; console wake kept the snapshot unchanged. Brightness and physical touch/motion wake were not observed, and no current measurement was taken. Historical sanitized device evidence (local evidence omitted).

**Retained `2d9f1ed` installation boundary:** unit 1 received the `2d9f1ed` app-only upgrade after its own fresh backup, identity, digest and layout checks; the one written app image verified. Boot/commands, exFAT mount, all 261 destination hashes, eight starter DVA decodes and the egg-scene JPEG decode passed. Preview frame counters advanced **14 → 33**. Its own **600-byte snapshot stayed unchanged through the checks and software reboot**, preserving the unhatched egg at sequence 0; orderly reboot checkpointed the migrated format. A roughly **27.4-second post-reboot observation**, including 20 seconds of idle coexistence, recorded **868 touch polls / zero errors / zero lock misses**, fresh valid IMU samples and writable zero lifetime/session usage. Gyro calibration remained incomplete. The final sample reported **179,055 internal bytes free** (largest block **81,920**), **7,690,188 PSRAM bytes free** (largest **7,602,176**) and **8,960 bytes minimum free main-task stack**. The **523,784 µs maximum frame sample includes initial JPEG decoding**; it is not steady FPS or gesture latency. Unit 2 also passed `2d9f1ed`, its own 600-byte saved-egg reboot, all 261 hashes, eight starter decodes and the scene JPEG. Its final sample recorded 877 touch polls / zero errors / zero lock misses, 179,119 internal bytes free, 7,690,188 PSRAM bytes free and 8,864 bytes minimum free main-task stack. Retained checkpoint evidence (historical local evidence omitted).

**Earlier unit 2 `90ee501` checkpoint:** independent backup and four flash-image hashes verified; boot, command response, software reboot and restoration of its own 576-byte egg snapshot passed. LCD/touch/audio/IMU initialized, with **148 touch samples / zero errors** and **140,989 µs maximum frame cost**. Calibration remained incomplete; a 6.95°/s sample exceeded the 5°/s stillness gate, with actual stillness unknown. Retained per-device evidence (historical local evidence omitted).

**Earlier `006488e` USB repair, retained historical result:** its installer passed 16 focused tests, real packaging and an app-only dry run. The identified board received exactly one new image at `0x20000`; esptool verified its hash. The original private 16 MiB backup remains intact, and a new 98,304-byte backup preserved the current table, NVS and boot metadata before this update. The table matched, and the valid sequence-1 OTA entry selected `ota_0`. No bootloader, table, OTA-data or NVS image was flashed. After USB reconnection, command replies and input bounds passed. The saved egg matches the preflash game state, and state plus the canonical 576-byte snapshot remain identical after one software reboot. That reboot intentionally checkpoints NVS; raw NVS byte equality is not asserted. The earlier failed postflash read remains recorded in the evidence.

The historical combined app’s sizes/hashes remain in local evidence; the published f74 sizes and hashes are in its [firmware manifest](../releases/firmware-f74ee4c/manifest.json). The renderer uses a **339,488-byte PSRAM framebuffer** and **13,184-byte internal DMA stripe**. At the earlier `59bf076` checkpoint, free memory was **206,143 internal bytes / 8,044,504 PSRAM bytes**, with **75,653 µs** maximum observed frame cost. These are snapshot measurements before the new SD/setup workload, not sustained FPS, minimum heap or battery-life figures. That earlier build did not migrate the game save. The walking checkpoint migrated care to 600 bytes; the historical rules-12 source migrated to 636 bytes as described above. The partition layout remains unchanged.

Source references: `docs/ESP_BUILD.md`, `docs/POWER_CONTROL.md`, `docs/SD_ASSETS.md`, `docs/FIRMWARE.md`, [USB_SD_TRANSFER.md](USB_SD_TRANSFER.md), `firmware/build-waveshare/flasher_args.json` and the bundled build/package records. Remaining acceptance covers visible artwork/orientation, finger touch, audible output, gyro interaction, the local game loop, hotspot/TLS, battery/PWR behavior and controlled recovery. Unit 1's decode, frame-counter and software-reboot results do not establish those observations or sudden-power-loss durability. Both units passed the bounded rules-12 installation, migration, asset, reboot and diagnostic-idle checks above; physical gameplay, brightness and touch/motion-wake acceptance remain open. The retained `2d9f1ed` results are a separate historical checkpoint. USB-triggered reset endurance remains unverified after the recoverable stalls. The touch path is implemented; external gameplay buttons are not required for its first playtest.
