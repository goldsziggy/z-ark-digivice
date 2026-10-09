# Nearby duels: protocol and limits

The wire contract below remains compatible with installed `f9d8723`. The next source update adds explicit Nearby mode selection and stronger review-to-send validation without changing that contract. [Current update and installation boundary](LANYARD_NEARBY_AUDIO_RELEASE.md).

Nearby is a local, reward-free two-device duel. The native runtime supplies each device's actual selected owned member ID, form ID, level and two derived Care bonuses when it opens the screen. Both devices derive the same five combat stats, types and named moves from **rules 12 / catalog 6**. A duel starts with full duel HP and 100 duel energy; match resolution never changes the companion's care HP, XP, bond, captures, ownership, journal or save. Matching member IDs on different devices do not imply a shared creature or identity.

Care offense is `floor(bond / 50) + (mood >= 80 ? 1 : 0)`; protection is `floor(bond / 50) + (fullness >= 80 ? 1 : 0)`. Each bonus is bounded to 0–5. Offense adds to Attack and Magic; protection adds to Defense and Resistance. Base stats and maximum HP remain unchanged. The runtime copies these bonuses once with the selected fighter before opening the radio session. Discovery, consent, retries and match resolution retain that frozen profile; Challenge, Accept and State packets cannot substitute different bonuses. See [runtime profile capture](../firmware/main/handheld_nearby.cpp) and [shared match rules](../core/nearby_match.cpp).

The challenger chooses Tactical or Auto on the Nearby invitation review, then sends the reviewed offer and becomes host. This ephemeral Nearby choice does not change the saved wild-battle mode. Both outgoing and incoming views show the mode. The recipient sees the frozen matchup and mode, then explicitly accepts. The UI/runtime bind that consent to the exact reviewed peer, nonce/session, mode and fighter profiles. Simultaneous challenges use the lower source MAC's offer; the other device still must accept that displayed offer. An incompatible catalog/rules peer cannot start a duel.

Local Nearby play requires **no service account, service pairing, save-sync, phone, router or Internet connection**. The local Wi-Fi driver must initialize successfully, but it need not join an access point. The runtime pauses ordinary Wi-Fi activity, takes an exclusive radio lease, starts ESP-NOW on channel 1, and restores the previous network policy after teardown. Native service pairing/save-sync is separate and is not a prerequisite for this flow. No location is uploaded.

Tactical exchanges alternate the attacker. The attacker chooses its actual Physical, Heavy or Magic skill; the defender chooses Brace, Counter or Ward. The exchange resolves after both choices are submitted. Heavy requires six remaining duel energy; basic attacks remain legal at zero energy, as in wild battles. Existing damage, type advantages and guard/reflection rules are reused, with the existing practice chip floor of `ceil(defender max HP / 20)`, clamped to 4–32 before type/guard calculations. At most 40 exchanges can occur; surviving participants then draw.

Auto rolls Physical or Magic with equal odds for whichever creature attacks, plus one of the three existing defenses for its opponent. Each authoritative exchange consumes the deterministic match RNG once for each decision and stores the result in the current state. Retries resend that result. There is no new ability or capture action. Both Auto and subsequent Tactical exchanges have at least 2.4 seconds after the recipient first acknowledges the previous state; Tactical choices may be queued during that visible hold.

## Portable API and radio integration

`Protocol` has one main-task owner. `open(mac, fighter, nowMs, nonce)` requires a fresh nonzero random open nonce. `challenge(index, mode, session, seed, nowMs)` requires a fresh nonzero 64-bit session and nonzero match seed. These are session freshness values, not production credentials. `accept`, `choose`, `cancel`, `tick`, `receive` and `pop` implement the rest of the flow. The radio adapter supplies the real source MAC to `receive` and sends each popped datagram to its destination. ESP-NOW MAC delivery callbacks are not application acknowledgments.

Discovery retains at most four peers, advertises only on explicit Nearby entry/handshake, and expires availability after ten seconds. The eight-datagram transmit queue has a hard cap. Unacknowledged messages retry at 500 ms. A five-second communication gap displays reconnecting; a 30-second gap ends the session. A human turn with no resolved progress for two minutes also ends, even if heartbeat traffic is live. A finished result remains readable when the other board leaves.

Reconnection can resume only the same live in-memory session and current state. There is no persistent peer pairing or remembered match to restore after reboot. Reboot, leaving Nearby, timeout or cancellation ends the session; a later duel requires fresh discovery and consent. No session snapshot is written to the companion save and no reward can be duplicated. The host advances at most one unacknowledged exchange, so resynchronization requires no state jumps. A guest accepts only the exact next deterministic result, verifies its own submitted move, and never accepts a new active state after the terminal result.

## Wire contract

Every packet uses explicit little-endian integers; no C++ struct is copied onto the wire. The configured maximum is 240 bytes, and the largest current packet is **154 bytes**. The fixed 34-byte header is:

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 4 | `DGN1` |
| 4 | 1 | Protocol version 1 |
| 5 | 1 | Kind: Hello 1, Challenge 2, Accept 3, State 4, Choice 5, ACK 6, Cancel 7, Sync 8 |
| 6 | 2 | Total packet length |
| 8 | 6 | Sender MAC, checked against the actual radio source |
| 14 | 2 | Rules 12 |
| 16 | 2 | Catalog 6 |
| 18 | 8 | Session ID; zero only for Hello |
| 26 | 4 | State sequence |
| 30 | 4 | Sender's current open nonce |

Payload follows, then CRC32 of every preceding byte. A fighter is **20 bytes**: five 32-bit words for member ID, form ID, level, offense bonus and protection bonus. Hello carries one fighter and an availability byte, for 59 bytes including header and CRC. Challenge/Accept carry both fighters, mode, seed, and the recipient's open nonce, for 87 bytes total. State carries the **116-byte** canonical match encoding, for 154 bytes total. Choice and ACK each carry one byte. Cancel/Sync have no payload. Rules 11's shorter packets cannot enter a rules 12 match.

CRC detects corruption; source MAC, open nonces, session and ordered sequence reject stale or cross-session traffic. This is not cryptographic authentication or an anti-cheat service. The transport must remain explicitly scoped to nearby friendly play; no claim of encrypted or authenticated ESP-NOW is made by this protocol.

## Verification boundary

The portable test injects dropped, duplicated, reordered and malformed messages; lost challenge/accept/state/ACK; concurrent challenges; a wrong source, session or open nonce; future-state jumps; substituted guest choices; terminal resurrection; bounded queue pressure; result retries; interruption/reconnection; timeout and cancellation. It resolves 512 actual Auto duels through the existing combat resolver and tests exhausted Tactical energy and both ACK paths' visible hold. Rules 12 coverage also checks bounded Care bonuses, profile substitution at each handshake/state boundary, and rejection of rules 11 wire formats. The recorded run passed **49,695 checks**; see rules 12 runtime evidence (historical local evidence omitted).

Current host layouts are `Fighter` **20 bytes**, `Match` **88 bytes** and `Protocol` **2,456 bytes**; canonical wire state is **116 bytes**. Host object sizes include host ABI padding and are separate from wire sizes or device heap measurements. The older rules 11 evidence (historical local evidence omitted) remains historical.

**Physical two-board ESP-NOW exchange remains unverified by the retained installation checks.** The older egg-only checkpoint was not a radio acceptance test. [`beginNearby()`](../firmware/main/handheld_nearby.cpp) requires `Phase::Home` and an actual active owned member, and the egg screen has no Nearby entry. Installed firmware has no diagnostic that starts radio discovery with an egg or a disposable test fighter: `device status` observes status, and `nearby close` only closes an active session. Wi-Fi access-point scanning does not exercise ESP-NOW. No player save is hatched, replaced or otherwise altered by protocol/host tests to bypass this gate.

After normal first-time partner selection, physical acceptance still needs discovery, explicit challenge/accept, Tactical and Auto exchanges, interruption/reconnect, exit/network restoration, and visible/sound checks on both boards. The existing native UI, runtime, radio adapter and deterministic protocol are wired together; these hardware checks are not blocked on service pairing or save-sync.
