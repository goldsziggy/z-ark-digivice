#!/usr/bin/env bash
# Optional host validation only. Uses existing mbedTLS 3 + cJSON, installs nothing.
# Override MBEDTLS_PREFIX, CJSON_PREFIX, CXX, DIGIVICE_NODE for another existing toolchain.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/digivice-assets-client.XXXXXX")
trap 'rm -rf "$build"' EXIT
mbed=${MBEDTLS_PREFIX:-/opt/homebrew/opt/mbedtls@3}
cjson=${CJSON_PREFIX:-/opt/homebrew}
cxx=${CXX:-clang++}
# npm exports NODE as its own executable, which can be older than the project's
# TypeScript-capable runtime. Use the shared version-checking wrapper instead.
node="$repo/scripts/node.sh"
for header in "$mbed/include/mbedtls/pk.h" "$cjson/include/cjson/cJSON.h"; do
  if [[ ! -f "$header" ]]; then echo "Missing existing host dependency: $header (nothing installed)." >&2; exit 2; fi
done
cd "$repo"
"$node" --input-type=module - "$build" <<'JS'
import {readFileSync,writeFileSync} from 'node:fs';
import {signDevelopmentPayload} from './service/asset-signing.ts';
const directory=process.argv[2], index=JSON.parse(readFileSync('assets/device/index.json','utf8'));
const manifest={formatVersion:1,release:index.release,rulesVersion:1,profile:index.profile,packs:index.packs.map(({file,...pack})=>({...pack,url:`/api/device/assets/packs/${pack.id}/${pack.version}`}))};
const sign=value=>signDevelopmentPayload(Buffer.from(typeof value==='string'?value:JSON.stringify(value)));
const save=(name,value)=>writeFileSync(`${directory}/${name}.json`,typeof value==='string'?value:JSON.stringify(value));
const valid=sign(manifest);save('valid',valid);
const scenes=manifest.packs.filter(pack=>pack.kind==='background');
save('scenes-only',sign({...manifest,packs:scenes}));
save('single-scene',sign({...manifest,packs:scenes.slice(0,1)}));
save('empty',sign({...manifest,packs:[]}));
const signature=Buffer.from(valid.signature,'base64');signature[0]^=1;save('signature',{...valid,signature:signature.toString('base64')});
save('payload',{...valid,payloadBase64:Buffer.from(JSON.stringify({...manifest,release:2})).toString('base64')});
save('duplicate-key',JSON.stringify(valid).replace('{','{"keyId":"digivice-dev-v1",'));
save('extra-key',{...valid,extra:1});save('wrong-key',{...valid,keyId:'unknown'});
for(const [name,edit] of Object.entries({'wrong-profile':m=>m.profile='wrong','duplicate-id':m=>m.packs[1]=m.packs[0],'wrong-path':m=>m.packs[0].url='https://example.com/private','oversize':m=>m.packs[0].bytes=131073,'wrong-dimensions':m=>m.packs[0].width=480,'unknown-id':m=>m.packs[0].id='private-secret'})) {const m=structuredClone(manifest);edit(m);save(name,sign(m));}
save('deep',sign('{"packs":[[[[]]]]}'));save('nul',sign(Buffer.from(JSON.stringify(manifest)).toString()+'\0'));
save('bad-padding',{...valid,signature:valid.signature.slice(0,-3)+'B=='});
JS
stub="$repo/firmware/tests/assets_client_stubs"
"$cxx" -std=c++17 -Wall -Wextra -Werror -DCONFIG_DIGIVICE_DEVELOPMENT_ASSETS=0 -I"$stub" -c firmware/main/device_assets_client.cpp -o "$build/disabled.o"
cat > "$build/disabled.cpp" <<'CPP'
#include "device_assets_client.hpp"
int main() {
  using namespace digivice::assets;
  DeviceAssetsClient client;
  if (client.quiescent()) return 1;
  client.pause(true);
  if (!client.quiescent()) return 1;
  client.pause(false);
  return DeviceAssetsClient::developmentAssetsEnabled() ||
    client.begin(nullptr) != ESP_ERR_NOT_SUPPORTED ||
    client.request("https://example.test", false, "sprite-mote-v1") ||
    client.status().phase != DownloadPhase::Disabled;
}
CPP
"$cxx" -std=c++17 -Wall -Wextra -Werror -DCONFIG_DIGIVICE_DEVELOPMENT_ASSETS=0 -I"$stub" -I"$repo/firmware/main" "$build/disabled.cpp" "$build/disabled.o" -o "$build/disabled"
"$build/disabled"
echo 'PASS default-off gate: no worker, request or cache ownership.'
"$cxx" -std=c++17 -Wall -Wextra -Werror -I"$stub" -I"$mbed/include" -I"$cjson/include/cjson" \
  firmware/tests/device_assets_client_test.cpp firmware/runtime/asset_cache.cpp firmware/runtime/network.cpp \
  -L"$mbed/lib" -L"$cjson/lib" -lmbedcrypto -lcjson -o "$build/test"
"$build/test" "$build" "$repo"
