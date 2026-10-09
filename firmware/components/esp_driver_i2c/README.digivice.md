# Pinned I2C NACK timeout backport

This project-local component copies the ten build source/header/configuration
files from ESP-IDF v5.3.6, commit
`79e3454c68248bd7d881721c9b2e94561378a8ce`. Upstream test applications are omitted.
Original notices remain; `LICENSE.ESPRESSIF` contains the SDK Apache-2.0 license.
`UPSTREAM.json` records every original and resulting file SHA-256.

The only driver change is the synchronous NACK busy-wait fix from official
[Espressif commit 2523fee9](https://github.com/espressif/esp-idf/commit/2523fee9cd49ca59b90bdb475c50b5277c44c876),
dated 2025-10-13, which closes
[issue 17720](https://github.com/espressif/esp-idf/issues/17720).
`bounded-nack.patch` contains that exact loop change and a modification notice.
No board initialization, caller timeout, public API, asynchronous path or
hardware reset implementation changes are included.

On NACK, the old driver could spin forever while the bus remained busy, even
when the caller supplied a finite timeout. The backport checks elapsed RTOS
ticks against `ticks_to_wait`, sets the internal status to `I2C_STATUS_TIMEOUT`,
calls the existing hardware FSM reset, and reaches the existing event logging
and semaphore release. Current synchronous callers already supply finite
timeouts: expander and raw IMU 20 ms, pedometer 10 ms, and touch a bounded
remaining deadline. Their failure handling is unchanged.

The upstream comparison is strictly `>`: a 20 ms input at 100 Hz becomes two
ticks, with the busy-wait expiry at elapsed tick three. Queue waits, reset work
and scheduling add time; this is not an exact 20 ms wall-clock transaction
deadline. The tick source must advance. Explicit infinite timeout
(`-1`/`portMAX_DELAY`) and the separate asynchronous loop remain upstream
behavior; the application does not use them. Reset completion does not prove
that a physical stuck device or bus recovered.

ESP-IDF selects `firmware/components/esp_driver_i2c` ahead of the installed
component. Reconfigure existing builds when adding this override, then check
the build's component paths/compile commands. The installed SDK remains
unchanged. Reassess/remove this override when changing the pinned SDK; it is
not intended to replace the driver in an arbitrary later IDF version.

Run from the repository root:

```sh
python3 firmware/tests/test_i2c_nack_timeout.py
```

The host test compiles the actual complete `s_i2c_send_commands` function
extracted without edits, with RTOS/HAL doubles, AddressSanitizer and
UndefinedBehaviorSanitizer. It verifies finite stuck-bus expiry, natural
completion at the boundary, repeated samples within a tick, unsigned tick
wrap, zero timeout, reset failure, the prior STOP-command path, event timeout,
subsequent success and semaphore cleanup. Reversing the recorded patch must
reproduce the exact pinned source hash; that old function must trip the test's
100,000-poll guard instead of returning. The guard exists only in the host HAL
double. These tests do not access hardware or claim an electrical recovery.

Host results are recorded in `docs/evidence/i2c-nack-backport-host.json`.
Target compilation and physical acceptance are separate deployment evidence.
