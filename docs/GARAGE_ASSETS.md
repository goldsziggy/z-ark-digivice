# Private Garage asset mirror

The local game and its original art work without Garage. The connector and authenticated download routes are implemented, but **no assets have been uploaded**. Read-only discovery on 5 October 2026 found that the existing key can access `jpgen` and `pocket-pals`; both have website hosting enabled. Neither is an eligible private destination. No bucket, policy, grant, credential or existing object was changed.

Garage website hosting exposes a bucket through a separate web endpoint. An anonymous S3 request returning 403 does not establish privacy, and a separate `digivice/` prefix cannot override bucket website publication. Garage uses its own per-key/per-bucket permissions rather than AWS bucket policies or ACLs. See the official [website hosting documentation](https://garagehq.deuxfleurs.fr/documentation/cookbook/exposing-websites/) and [S3 compatibility reference](https://garagehq.deuxfleurs.fr/documentation/reference-manual/s3-compatibility/).

## Existing connection and required decision

Remote Garage access is disabled until an explicit `DIGIVICE_GARAGE_ENV_FILE` or `envFile` is supplied. The environment file and optional SDK package remain outside this repository. No endpoint or credentials are bundled. The service does not search another project for configuration.

The remaining requirement is an explicitly selected **existing private bucket that the existing key is already authorized to use**. The connector requires `DIGIVICE_GARAGE_BUCKET`; it does not create buckets, keys or grants, or disable website hosting on an existing bucket. The current environment's bucket is consulted only by the server-side `diagnose` command. Normal library and upload operations refuse missing explicit selection before loading credentials or the SDK.

Optional local configuration:

| Name | Purpose |
| --- | --- |
| `DIGIVICE_GARAGE_BUCKET` | Explicit private destination; no default for library access or upload |
| `DIGIVICE_GARAGE_ENV_FILE` | Existing server environment file; defaults to Monstopia's `server/.env` |
| `DIGIVICE_GARAGE_SDK_PACKAGE` | Existing package.json location from which the installed SDK resolves; defaults beside that environment file |

No new persistent secrets are needed. Keep these server settings out of browser files. The existing service continues to require loopback access and refuses production mode; Garage storage is not browser hosting or phone HTTPS setup.

## Concrete upload scope

The earlier dry run recorded **6 objects, 284,121 bytes**: the signed original catalog, three original packs, and the curated `personal-ds-line` pack/provenance pair. The exact IDs, lengths and SHA-256 hashes are recorded in the historical upload plan (historical local evidence omitted). This is a local plan, not an upload receipt.

Local catalog release 2 adds eight approved backgrounds for the local game. Those scenes are outside the existing Garage publication scope. The planner now refuses this larger catalog with `garage_catalog_scope`, before preparing or transferring any objects; it does not silently filter or re-sign it. A future Garage publication needs an explicitly reviewed catalog and scope as well as an eligible private destination.

Only these sources are eligible:

- `assets/packs/catalog.json` and the three packs named by its valid signed manifest: `starter-v2`, `tide-v1`, `ember-v1`.
- Direct `.personal-assets/personal-*/pack.json` and `provenance.json` pairs that pass the same personal-art validator as the browser.

Raw source sheets, screenshots, ZIP archives, models and unrelated directories are excluded. The planner rejects symlinked source roots or file ancestors and validates that files remain inside the selected repository. The limits are five personal pairs plus three original packs, 14 objects including catalog/provenance, 4 MiB total, 256 KiB per pack, 64 KiB per provenance and a bounded signed catalog. The library returns at most eight packs. Personal files must be valid UTF-8; malformed bytes are refused rather than replaced during decoding. Personal provenance records stay paired with the art and do not grant redistribution rights.

Keys are content addressed under the dedicated `digivice/v1/` namespace:

```text
digivice/v1/original/<id>/<version>/<kind>-<sha256>.json
digivice/v1/personal/<id>/<version>/<kind>-<sha256>.json
```

Before upload, the connector must receive an explicit `NoSuchWebsiteConfiguration` result from `GetBucketWebsite`. Website-enabled, inaccessible, unsupported or ambiguous results fail closed. It checks again before each write. Existing objects are downloaded and compared; matching bytes are reused, conflicting bytes stop the operation. New objects use conditional creation (`If-None-Match: *`) and are downloaded for full SHA-256/length verification. There is no delete or overwrite command. A failed partial transfer may leave immutable objects but does not install a new local receipt; rerunning can verify and reuse matching objects.

Only a completely verified transfer atomically writes `.data/garage-assets.json` with the fixed namespace, destination and allowed hashes/lengths. That ignored local receipt is the server's download allowlist. It cannot redirect downloads into Monstopia's namespace. Reads recheck private bucket status and bound the response before verifying every byte. SDK requests use a 2.5-second deadline with no retry; failures expose constant sanitized messages. Bucket visibility checks are point-in-time checks, so operating the destination as private remains necessary.

## Commands and application behavior

From this repository:

```sh
./scripts/node.sh scripts/garage-assets.ts dry-run
./scripts/node.sh scripts/garage-assets.ts diagnose
npm run test:garage
```

`dry-run` never reads credentials or contacts Garage. `diagnose` is read-only and emits only status, reason, hostname, bucket and prefix. Once an eligible existing destination has been selected and transfer is authorized, the `upload` command creates the scoped objects; `verify` checks every object pinned by the resulting receipt. Neither was run against the live instance during this task. Configure the same explicit destination when starting the service.

All routes require the existing paired-device bearer token before initializing Garage:

| Route | Response |
| --- | --- |
| `GET /api/garage/catalog` | `{status:"available",packs:[{id,version,kind,bytes,sha256}]}` |
| `GET /api/garage/packs/<id>/<version>` | Verified original pack JSON with SHA-256 ETag |
| `GET /api/garage/personal/<id>/<version>` | `{packText,provenanceText}` for one pinned personal pair |

There is no browser upload, arbitrary-key, diagnostic or bucket-management route. Browser responses contain no endpoint, bucket, object key, credential or presigned URL. The personal shelf verifies the catalog hash and validates the provenance before activation in its separate local database. An unavailable Garage shelf preserves existing art and gameplay. The original signed catalog stays local with its original URLs; it is not rewritten or re-signed for Garage.

## Verification evidence

- Sanitized read-only discovery record (historical local evidence omitted): both accessible buckets website-enabled; no private destination selected and zero remote writes.
- Adapter test output (historical local evidence omitted): synthetic in-memory transport tests cover private/public gates, missing configuration, exact scope, conditional reuse, integrity failures, receipt confinement, symlinks, bounds and invalid UTF-8. These tests do not imply a live upload passed.
- Background scope regression (historical local evidence omitted): eight adapter tests pass, including rejection of the larger local catalog with zero object reads or writes through the mock transport.
- HTTP integration tests cover authentication before connector creation, safe catalog projection, unavailable/default configuration, download limits and continued local play. The web library uses authenticated local routes.

The next concrete action is to identify an eligible existing private destination. The current public-website buckets remain untouched.
