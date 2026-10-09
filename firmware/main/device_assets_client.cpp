#include "device_assets_client.hpp"
#include "../runtime/network.hpp"
#include <cstdio>
#include <cstring>

// Source-only ESP-IDF 5.3.x integration; not a claimed Xtensa/board build.
// Official API references:
// https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/protocols/esp_http_client.html
// https://mbed-tls.readthedocs.io/projects/api/en/v3.6.3/api/file/pk_8h/
// https://github.com/DaveGamble/cJSON/blob/v1.7.18/cJSON.h
#if CONFIG_DIGIVICE_DEVELOPMENT_ASSETS
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include <algorithm>
#include <cmath>
#include <strings.h>
#endif

namespace digivice::assets {
bool DeviceAssetsClient::developmentAssetsEnabled() {
#if CONFIG_DIGIVICE_DEVELOPMENT_ASSETS
    return true;
#else
    return false;
#endif
}
const char* downloadPhaseName(DownloadPhase phase) {
    switch (phase) { case DownloadPhase::Disabled:return "disabled"; case DownloadPhase::Idle:return "idle";
    case DownloadPhase::Queued:return "queued"; case DownloadPhase::Catalog:return "catalog"; case DownloadPhase::Downloading:return "downloading";
    case DownloadPhase::Complete:return "complete"; case DownloadPhase::Failed:return "failed"; case DownloadPhase::Cancelled:return "cancelled"; }
    return "unknown";
}
const char* downloadErrorName(DownloadError error) {
    switch (error) { case DownloadError::None:return "none"; case DownloadError::Config:return "configuration"; case DownloadError::Network:return "network";
    case DownloadError::Catalog:return "catalog"; case DownloadError::Signature:return "signature"; case DownloadError::NotListed:return "not-listed";
    case DownloadError::Rollback:return "rollback"; case DownloadError::Cache:return "cache"; case DownloadError::Cancelled:return "cancelled"; }
    return "unknown";
}

#if CONFIG_DIGIVICE_DEVELOPMENT_ASSETS
namespace {
constexpr std::size_t kEnvelopeBytes = 8192, kPayloadBytes = 6144;
constexpr std::int64_t kRequestUs = 8000000;
// RFC6979 A.2.5 PUBLIC TEST KEY. Its signing scalar is public; never production trust.
constexpr char kPublicKey[] = "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEYP7UuiVanTHJYet0xjVtaMBJuJI7Yfps5mliLmDyn7Z5A/4QCLi8maQa6elWKLxk8vGyDC1+n1F3o8KU1EYimQ==";
constexpr const char* kIds[] = {"sprite-mote-v1","sprite-glint-v1","sprite-lumen-v1","sprite-flicker-v1","sprite-rill-v1","sprite-brine-v1","sprite-pelagia-v1","sprite-cinder-v1","sprite-scoria-v1","sprite-pyrel-v1",
    "scene-meadow-412-v1","scene-forest-412-v1","scene-beach-412-v1","scene-ruins-412-v1","scene-cavern-412-v1","scene-snow-412-v1","scene-volcanic-412-v1","scene-digital-412-v1"};
bool knownId(const char* id) {
    if (!id || ::strnlen(id, 48) >= 48) return false;
    for (const auto* known : kIds) if (std::strcmp(id, known) == 0) return true;
    return false;
}
bool plainJson(const char* data, std::size_t length, unsigned maximumDepth) {
    if (!length || std::memchr(data, '\0', length)) return false;
    bool quoted = false; unsigned depth = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const auto c = static_cast<unsigned char>(data[i]);
        if (c == '\\' || c > 126 || (c < 32 && (quoted || (c != '\n' && c != '\r' && c != '\t')))) return false;
        if (c == '"') quoted = !quoted;
        else if (!quoted && (c == '{' || c == '[')) { if (++depth > maximumDepth) return false; }
        else if (!quoted && (c == '}' || c == ']')) { if (!depth) return false; --depth; }
    }
    return !quoted && depth == 0;
}
cJSON* parse(const char* data, std::size_t length, unsigned depth) {
    if (!plainJson(data, length, depth)) return nullptr;
    const char* end = nullptr;
    auto* value = cJSON_ParseWithLengthOpts(data, length + 1, &end, true);
    if (end != data + length) { cJSON_Delete(value); return nullptr; }
    return value;
}
bool exact(const cJSON* value, const char* const* names, unsigned count) {
    if (!cJSON_IsObject(value) || cJSON_GetArraySize(value) != static_cast<int>(count)) return false;
    unsigned seen = 0;
    for (auto* item = value->child; item; item = item->next) {
        if (!item->string) return false;
        unsigned index = 0; while (index < count && std::strcmp(item->string, names[index]) != 0) ++index;
        if (index == count || (seen & (1u << index))) return false;
        seen |= 1u << index;
    }
    return seen == (1u << count) - 1;
}
const char* string(const cJSON* value, const char* name) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(value, name);
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}
bool equalString(const cJSON* value, const char* name, const char* expected) {
    const auto* text = string(value, name); return text && std::strcmp(text, expected) == 0;
}
bool number(const cJSON* value, const char* name, std::uint32_t maximum, std::uint32_t& result) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(value, name);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble < 1 || item->valuedouble > maximum || std::floor(item->valuedouble) != item->valuedouble) return false;
    result = static_cast<std::uint32_t>(item->valuedouble); return true;
}
bool isNumber(const cJSON* value, const char* name, std::uint32_t expected) { std::uint32_t result = 0; return number(value, name, expected, result) && result == expected; }
int base64Digit(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
bool decode64(const char* encoded, std::uint8_t* output, std::size_t capacity, std::size_t& decoded) {
    if (!encoded) return false;
    const auto length = ::strnlen(encoded, ((capacity + 2) / 3) * 4 + 1);
    if (!length || length % 4 || length > ((capacity + 2) / 3) * 4) return false;
    const unsigned padding = encoded[length - 1] == '=' ? (encoded[length - 2] == '=' ? 2 : 1) : 0;
    for (std::size_t i = 0; i < length - padding; ++i) if (base64Digit(encoded[i]) < 0) return false;
    if ((padding == 2 && (base64Digit(encoded[length - 3]) & 15)) || (padding == 1 && (base64Digit(encoded[length - 2]) & 3))) return false;
    return mbedtls_base64_decode(output, capacity, &decoded, reinterpret_cast<const unsigned char*>(encoded), length) == 0 && decoded > 0 && decoded <= capacity;
}
bool verifySignature(const std::uint8_t* payload, std::size_t length, const std::uint8_t* signature) {
    // mbedTLS pk_verify accepts ASN.1 ECDSA. The wire format is fixed r||s (P1363).
    std::uint8_t der[72]{0x30, 0}; std::size_t used = 2;
    for (unsigned component = 0; component < 2; ++component) {
        const auto* scalar = signature + component * 32; std::size_t first = 0;
        while (first < 31 && scalar[first] == 0) ++first;
        const bool prefix = (scalar[first] & 0x80) != 0;
        der[used++] = 2; der[used++] = static_cast<std::uint8_t>(32 - first + (prefix ? 1 : 0));
        if (prefix) der[used++] = 0;
        std::memcpy(der + used, scalar + first, 32 - first); used += 32 - first;
    }
    der[1] = static_cast<std::uint8_t>(used - 2);
    std::uint8_t publicKey[128]{}, hash[32]{}; std::size_t publicLength = 0;
    if (!decode64(kPublicKey, publicKey, sizeof(publicKey), publicLength)) return false;
    const auto* digest = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!digest || mbedtls_md(digest, payload, length, hash) != 0) return false;
    mbedtls_pk_context key; mbedtls_pk_init(&key);
    const bool valid = mbedtls_pk_parse_public_key(&key, publicKey, publicLength) == 0 &&
        mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA) && mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, hash, sizeof(hash), der, used) == 0;
    mbedtls_pk_free(&key); return valid;
}
bool decodeHash(const char* text, std::uint8_t* result) {
    if (!text || ::strnlen(text, 65) != 64) return false;
    for (unsigned i = 0; i < 32; ++i) {
        unsigned value = 0;
        for (unsigned j = 0; j < 2; ++j) { const char c = text[i * 2 + j]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false; value = value * 16 + (c <= '9' ? c - '0' : c - 'a' + 10); }
        result[i] = static_cast<std::uint8_t>(value);
    }
    return true;
}
DownloadError catalog(const char* envelope, std::size_t length, char* payload, Spec* specs, std::size_t& count, std::uint32_t& release) {
    auto* root = parse(envelope, length, 1); if (!root) return DownloadError::Catalog;
    constexpr const char* names[] = {"keyId","payloadBase64","signature"};
    std::size_t payloadLength = 0, signatureLength = 0; std::uint8_t signature[64]{};
    const bool decoded = exact(root, names, 3) && equalString(root, "keyId", "digivice-dev-v1") &&
        decode64(string(root, "payloadBase64"), reinterpret_cast<std::uint8_t*>(payload), kPayloadBytes, payloadLength) &&
        decode64(string(root, "signature"), signature, sizeof(signature), signatureLength) && signatureLength == 64;
    cJSON_Delete(root);
    if (!decoded) return DownloadError::Catalog;
    if (!verifySignature(reinterpret_cast<const std::uint8_t*>(payload), payloadLength, signature)) return DownloadError::Signature;
    payload[payloadLength] = '\0'; root = parse(payload, payloadLength, 3); if (!root) return DownloadError::Catalog;
    constexpr const char* manifestKeys[] = {"formatVersion","release","rulesVersion","profile","packs"};
    constexpr const char* entryKeys[] = {"id","version","bytes","sha256","kind","width","height","url"};
    const auto* packs = cJSON_GetObjectItemCaseSensitive(root, "packs");
    bool valid = exact(root, manifestKeys, 5) && isNumber(root, "formatVersion", 1) && isNumber(root, "rulesVersion", 1) &&
        number(root, "release", 0x7fffffff, release) && equalString(root, "profile", "s3-146-v1") && cJSON_IsArray(packs) &&
        cJSON_GetArraySize(packs) > 0 && cJSON_GetArraySize(packs) <= 24;
    count = 0;
    for (const auto* entry = valid ? packs->child : nullptr; entry; entry = entry->next) {
        Spec spec{}; const char* id = string(entry, "id");
        if (!exact(entry, entryKeys, 8) || !knownId(id)) { valid = false; break; }
        std::strcpy(spec.id, id); spec.kind = std::strncmp(id, "sprite-", 7) == 0 ? Kind::Sprite : Kind::Background;
        const std::uint16_t dimension = spec.kind == Kind::Sprite ? 32 : 412;
        char path[128]{};
        valid = number(entry, "version", 0x7fffffff, spec.version) && number(entry, "bytes", kMaximumBlobBytes, spec.bytes) && decodeHash(string(entry, "sha256"), spec.sha256) &&
            equalString(entry, "kind", spec.kind == Kind::Sprite ? "sprite" : "background") && isNumber(entry, "width", dimension) && isNumber(entry, "height", dimension);
        spec.width = dimension; spec.height = dimension;
        const int written = std::snprintf(path, sizeof(path), "/api/device/assets/packs/%s/%lu", spec.id, static_cast<unsigned long>(spec.version));
        valid = valid && written > 0 && written < static_cast<int>(sizeof(path)) && equalString(entry, "url", path) && validSpec(spec);
        for (std::size_t i = 0; i < count; ++i) if (std::strcmp(specs[i].id, spec.id) == 0) valid = false;
        if (!valid) break;
        specs[count++] = spec;
    }
    cJSON_Delete(root); return valid ? DownloadError::None : DownloadError::Catalog;
}

struct Headers {
    char etag[67]{}, range[80]{}, type[64]{};
    std::size_t length = 0, total = 0; unsigned count = 0;
    bool invalid = false, haveLength = false, haveEtag = false, haveRange = false, haveType = false;
};
template <std::size_t N> bool headerText(char (&target)[N], const char* text, bool& seen) {
    const auto length = ::strnlen(text, N); if (seen || length >= N) return false;
    seen = true; std::memcpy(target, text, length + 1); return true;
}
esp_err_t headerEvent(esp_http_client_event_t* event) {
    // These are acceptance checks, not SDK allocation limits: IDF 5.3.2
    // ignores this callback's return and allocates header fragments first.
    // CMake must compile the HTTP parser component with
    // HTTP_MAX_HEADER_SIZE=4096. SDK/TLS/JSON heap remains additional to buffers.
    if (event->event_id != HTTP_EVENT_ON_HEADER) return ESP_OK;
    auto& headers = *static_cast<Headers*>(event->user_data);
    if (!event->header_key || !event->header_value) { headers.invalid = true; return ESP_FAIL; }
    const auto keyLength = ::strnlen(event->header_key, 128), valueLength = ::strnlen(event->header_value, 1024);
    headers.total += keyLength + valueLength;
    if (++headers.count > 32 || keyLength == 128 || valueLength == 1024 || headers.total > 4096) headers.invalid = true;
    else if (::strcasecmp(event->header_key, "ETag") == 0) headers.invalid |= !headerText(headers.etag, event->header_value, headers.haveEtag);
    else if (::strcasecmp(event->header_key, "Content-Range") == 0) headers.invalid |= !headerText(headers.range, event->header_value, headers.haveRange);
    else if (::strcasecmp(event->header_key, "Content-Type") == 0) headers.invalid |= !headerText(headers.type, event->header_value, headers.haveType);
    else if (::strcasecmp(event->header_key, "Content-Encoding") == 0) headers.invalid |= ::strcasecmp(event->header_value, "identity") != 0;
    else if (::strcasecmp(event->header_key, "Transfer-Encoding") == 0) headers.invalid = true;
    else if (::strcasecmp(event->header_key, "Content-Length") == 0) {
        if (headers.haveLength || !valueLength || valueLength > 6) headers.invalid = true;
        headers.haveLength = true;
        for (std::size_t i = 0; i < valueLength && !headers.invalid; ++i) {
            const char c = event->header_value[i]; if (c < '0' || c > '9') { headers.invalid = true; break; }
            headers.length = headers.length * 10 + static_cast<unsigned>(c - '0');
        }
    }
    return headers.invalid ? ESP_FAIL : ESP_OK;
}
bool httpRead(const char* origin, const char* path, const Spec* spec, std::uint32_t offset,
              std::uint8_t* output, std::size_t capacity, std::size_t& received) {
    char url[336]{}, range[48]{}, expectedRange[80]{}, expectedEtag[67]{};
    auto originLength = std::strlen(origin); if (originLength && origin[originLength - 1] == '/') --originLength;
    const int urlLength = std::snprintf(url, sizeof(url), "%.*s%s", static_cast<int>(originLength), origin, path);
    if (urlLength < 1 || urlLength >= static_cast<int>(sizeof(url))) return false;
    std::size_t expected = 0;
    if (spec) {
        if (offset >= spec->bytes) return false;
        expected = std::min(std::size_t(kChunkBytes), std::size_t(spec->bytes - offset));
        std::snprintf(range, sizeof(range), "bytes=%lu-%lu", static_cast<unsigned long>(offset), static_cast<unsigned long>(offset + expected - 1));
        std::snprintf(expectedRange, sizeof(expectedRange), "bytes %lu-%lu/%lu", static_cast<unsigned long>(offset), static_cast<unsigned long>(offset + expected - 1), static_cast<unsigned long>(spec->bytes));
        constexpr char hex[] = "0123456789abcdef"; expectedEtag[0] = '"'; expectedEtag[65] = '"';
        for (unsigned i = 0; i < 32; ++i) { expectedEtag[1 + i * 2] = hex[spec->sha256[i] >> 4]; expectedEtag[2 + i * 2] = hex[spec->sha256[i] & 15]; }
    }
    Headers headers{}; esp_http_client_config_t options{};
    options.url = url; options.method = HTTP_METHOD_GET; options.timeout_ms = 8000;
    options.disable_auto_redirect = true; options.max_authorization_retries = -1;
    options.buffer_size = 1024; options.buffer_size_tx = 512; options.skip_cert_common_name_check = false;
    options.event_handler = headerEvent; options.user_data = &headers;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    options.crt_bundle_attach = esp_crt_bundle_attach;
#else
    if (std::strncmp(origin, "https://", 8) == 0) return false;
#endif
    const auto client = esp_http_client_init(&options); if (!client) return false;
    const auto deadline = esp_timer_get_time() + kRequestUs;
    const auto remaining = [&]() { const auto time = deadline - esp_timer_get_time(); return time > 0 && esp_http_client_set_timeout_ms(client, static_cast<int>((time + 999) / 1000)) == ESP_OK; };
    bool valid = esp_http_client_set_header(client, "Accept-Encoding", "identity") == ESP_OK;
    const char* type = spec ? (spec->kind == Kind::Sprite ? "application/octet-stream" : "image/jpeg") : "application/json";
    valid = valid && esp_http_client_set_header(client, "Accept", type) == ESP_OK;
    if (spec) valid = valid && esp_http_client_set_header(client, "Range", range) == ESP_OK && esp_http_client_set_header(client, "If-Range", expectedEtag) == ESP_OK;
    received = 0;
    // Socket-operation deadlines are NOT hard total SDK deadlines: a peer can
    // trickle headers inside fetch_headers. One worker remains bounded in count;
    // elapsed checks reject late data. Never task-delete or re-enter its handle.
    if (valid && remaining() && esp_http_client_open(client, 0) == ESP_OK && remaining()) {
        const auto declared = esp_http_client_fetch_headers(client);
        valid = remaining() && !headers.invalid && headers.haveLength && headers.haveType && declared > 0 &&
            declared == static_cast<std::int64_t>(headers.length) && headers.length <= capacity &&
            esp_http_client_get_status_code(client) == (spec ? 206 : 200) && !esp_http_client_is_chunked_response(client);
        if (spec) valid = valid && headers.length == expected && headers.haveRange && headers.haveEtag &&
            std::strcmp(headers.range, expectedRange) == 0 && std::strcmp(headers.etag, expectedEtag) == 0 && std::strcmp(headers.type, type) == 0;
        else valid = valid && !headers.haveRange && (std::strcmp(headers.type, type) == 0 || std::strcmp(headers.type, "application/json; charset=utf-8") == 0);
        while (valid && received < headers.length) {
            if (!remaining()) { valid = false; break; }
            const int count = esp_http_client_read(client, reinterpret_cast<char*>(output + received), static_cast<int>(headers.length - received));
            if (count <= 0 || static_cast<std::size_t>(count) > headers.length - received) { valid = false; break; }
            received += static_cast<std::size_t>(count);
        }
        valid = valid && received == headers.length && esp_http_client_is_complete_data_received(client) && esp_timer_get_time() < deadline;
    } else valid = false;
    esp_http_client_close(client); esp_http_client_cleanup(client); return valid;
}
} // namespace

esp_err_t DeviceAssetsClient::begin(Cache* cache) {
    if (worker_ || cache_ || !cache || cache->recoveryRequired()) return ESP_ERR_INVALID_STATE;
    cache_ = cache; cacheMutex_ = xSemaphoreCreateMutexStatic(&mutexStorage_);
    jobs_ = xQueueCreateStatic(1, sizeof(Job), jobStorage_, &jobQueue_);
    updates_ = xQueueCreateStatic(1, sizeof(DownloadStatus), updateStorage_, &updateQueue_);
    if (!cacheMutex_ || !jobs_ || !updates_ || xTaskCreate(worker, "dv-assets", 12288, this, 2, &worker_) != pdPASS) { cache_ = nullptr; return ESP_ERR_NO_MEM; }
    status_.phase = DownloadPhase::Idle; return ESP_OK;
}
bool DeviceAssetsClient::request(const char* origin, bool allowPrivateHttp, const char* id, const char* const* protectedIds, std::size_t count) {
    if (paused_ || !cache_ || !worker_ || pending_.load() || !knownId(id) || count > 4 || (!protectedIds && count)) return false;
#if !CONFIG_DIGIVICE_ALLOW_PRIVATE_HTTP
    allowPrivateHttp = false;
#endif
    // Reserve one generation for cancellation; a saturated counter fails closed.
    if (net::validateEndpoint(origin, allowPrivateHttp) != net::ConfigError::None || generation_.load() >= UINT32_MAX - 1) return false;
    Job job{}; std::strcpy(job.origin, origin); std::strcpy(job.id, id);
    for (std::size_t i = 0; i < count; ++i) {
        if (!knownId(protectedIds[i])) return false;
        bool duplicate = false; for (unsigned j = 0; j < job.protectedCount; ++j) duplicate |= std::strcmp(job.protectedIds[j], protectedIds[i]) == 0;
        if (!duplicate) std::strcpy(job.protectedIds[job.protectedCount++], protectedIds[i]);
    }
    bool protectedRequest = false; for (unsigned i = 0; i < job.protectedCount; ++i) protectedRequest |= std::strcmp(job.protectedIds[i], id) == 0;
    if (!protectedRequest) { if (job.protectedCount == 4) return false; std::strcpy(job.protectedIds[job.protectedCount++], id); }
    job.generation = generation_.fetch_add(1) + 1; pending_.store(true);
    status_ = {}; status_.generation = job.generation; status_.phase = DownloadPhase::Queued; status_.busy = true; std::strcpy(status_.spec.id, id);
    if (xQueueSend(jobs_, &job, 0) != pdTRUE) { pending_.store(false); status_.phase = DownloadPhase::Failed; status_.error = DownloadError::Config; return false; }
    return true;
}
void DeviceAssetsClient::cancel() {
    if (!pending_.load()) return;
    if (generation_.load() != UINT32_MAX) generation_.fetch_add(1);
    status_.generation = generation_.load(); status_.phase = DownloadPhase::Cancelled; status_.error = DownloadError::Cancelled;
}
void DeviceAssetsClient::pause(bool paused) {
    if (paused_ == paused) return;
    paused_ = paused;
    if (paused) cancel();
}
bool DeviceAssetsClient::quiescent() const { return paused_ && !pending_.load(); }
void DeviceAssetsClient::tick() {
    if (!updates_) return;
    DownloadStatus update{}; while (xQueueReceive(updates_, &update, 0) == pdTRUE) if (update.generation == generation_.load()) status_ = update;
    status_.busy = pending_.load();
}
DownloadStatus DeviceAssetsClient::status() const { auto result = status_; result.busy = pending_.load(); return result; }
bool DeviceAssetsClient::cachedSpec(const char* id, Spec& result) {
    if (paused_ || !cache_ || !knownId(id) || xSemaphoreTake(cacheMutex_, 0) != pdTRUE) return false;
    bool found = false; std::uint32_t generation = 0;
    for (std::size_t i = 0; i < kSlotCount; ++i) { const auto& record = cache_->record(i); if (record.present && record.complete && std::strcmp(record.spec.id, id) == 0 && record.generation > generation) { result = record.spec; generation = record.generation; found = true; } }
    xSemaphoreGive(cacheMutex_); return found;
}
bool DeviceAssetsClient::tryRead(const Spec& spec, std::size_t offset, void* bytes, std::size_t length, Result& result) {
    if (paused_ || !cache_ || length > kChunkBytes || xSemaphoreTake(cacheMutex_, 0) != pdTRUE) return false;
    result = cache_->read(spec, offset, bytes, length); xSemaphoreGive(cacheMutex_); return true;
}
void DeviceAssetsClient::publish(const Job& job, DownloadPhase phase, DownloadError error, Result result) {
    if (!current(job)) return;
    DownloadStatus update{}; update.phase = phase; update.error = error; update.cacheResult = result;
    update.spec = selected_; update.generation = job.generation; update.received = received_; update.total = selected_.bytes; update.busy = true;
    xQueueOverwrite(updates_, &update);
}
void DeviceAssetsClient::worker(void* context) {
    auto& self = *static_cast<DeviceAssetsClient*>(context); Job job{};
    for (;;) if (xQueueReceive(self.jobs_, &job, portMAX_DELAY) == pdTRUE) { if (self.current(job)) self.run(job); self.pending_.store(false); }
}
void DeviceAssetsClient::run(const Job& job) {
    selected_ = {}; std::strcpy(selected_.id, job.id); received_ = 0; publish(job, DownloadPhase::Catalog);
    std::size_t length = 0;
    if (!httpRead(job.origin, "/api/device/assets/catalog", nullptr, 0, reinterpret_cast<std::uint8_t*>(envelope_), kEnvelopeBytes, length)) { publish(job, DownloadPhase::Failed, DownloadError::Network); return; }
    if (!current(job)) return;
    envelope_[length] = '\0';
    const auto parsed = catalog(envelope_, length, payload_, catalog_, catalogCount_, catalogRelease_);
    if (parsed != DownloadError::None) { publish(job, DownloadPhase::Failed, parsed); return; }
    if (std::strcmp(catalogOrigin_, job.origin) == 0 && catalogRelease_ < highestRelease_) { publish(job, DownloadPhase::Failed, DownloadError::Rollback); return; }
    bool found = false; for (std::size_t i = 0; i < catalogCount_; ++i) if (std::strcmp(catalog_[i].id, job.id) == 0) { selected_ = catalog_[i]; found = true; break; }
    if (!found) { publish(job, DownloadPhase::Failed, DownloadError::NotListed); return; }
    if (!current(job)) return;
    const char* protectIds[4]{}; for (unsigned i = 0; i < job.protectedCount; ++i) protectIds[i] = job.protectedIds[i];
    xSemaphoreTake(cacheMutex_, portMAX_DELAY);
    if (!current(job)) { xSemaphoreGive(cacheMutex_); return; }
    bool rollback = false;
    for (std::size_t i = 0; i < kSlotCount; ++i) { const auto& record = cache_->record(i); if (record.present && record.complete && std::strcmp(record.spec.id, selected_.id) == 0 &&
        (record.spec.version > selected_.version || (record.spec.version == selected_.version && !sameSpec(record.spec, selected_)))) rollback = true; }
    std::uint32_t token = 0;
    const auto result = rollback ? Result::Invalid : (!cache_->protect(protectIds, job.protectedCount) ? Result::Invalid : cache_->begin(selected_, token));
    const bool complete = result == Result::Ok && cache_->contains(selected_);
    received_ = complete ? selected_.bytes : cache_->transfer().received;
    xSemaphoreGive(cacheMutex_);
    if (rollback) { publish(job, DownloadPhase::Failed, DownloadError::Rollback); return; }
    if (result != Result::Ok) { publish(job, DownloadPhase::Failed, DownloadError::Cache, result); return; }
    std::strcpy(catalogOrigin_, job.origin); highestRelease_ = catalogRelease_; // RAM watermark only; cached version/hash survives reboot.
    if (complete) { publish(job, DownloadPhase::Complete); return; }
    char path[128]{}; std::snprintf(path, sizeof(path), "/api/device/assets/packs/%s/%lu", selected_.id, static_cast<unsigned long>(selected_.version));
    while (received_ < selected_.bytes && current(job)) {
        publish(job, DownloadPhase::Downloading);
        if (!httpRead(job.origin, path, &selected_, received_, chunk_, sizeof(chunk_), length)) { publish(job, DownloadPhase::Failed, DownloadError::Network); return; }
        if (!current(job)) return;
        xSemaphoreTake(cacheMutex_, portMAX_DELAY);
        if (!current(job)) { xSemaphoreGive(cacheMutex_); return; }
        const auto appended = cache_->append(token, received_, chunk_, length);
        if (appended == Result::Ok) received_ = cache_->transfer().received;
        xSemaphoreGive(cacheMutex_);
        if (appended != Result::Ok) { publish(job, DownloadPhase::Failed, DownloadError::Cache, appended); return; }
    }
    if (!current(job)) return;
    xSemaphoreTake(cacheMutex_, portMAX_DELAY);
    if (!current(job)) { xSemaphoreGive(cacheMutex_); return; }
    const auto finished = cache_->finish(token); xSemaphoreGive(cacheMutex_);
    publish(job, finished == Result::Ok ? DownloadPhase::Complete : DownloadPhase::Failed, finished == Result::Ok ? DownloadError::None : DownloadError::Cache, finished);
}
#else
esp_err_t DeviceAssetsClient::begin(Cache* cache) { (void)cache; return ESP_ERR_NOT_SUPPORTED; }
bool DeviceAssetsClient::request(const char* origin, bool allowPrivateHttp, const char* id, const char* const* ids, std::size_t count) { (void)origin; (void)allowPrivateHttp; (void)id; (void)ids; (void)count; return false; }
void DeviceAssetsClient::cancel() {}
void DeviceAssetsClient::pause(bool paused) { paused_ = paused; }
bool DeviceAssetsClient::quiescent() const { return paused_; }
void DeviceAssetsClient::tick() {}
DownloadStatus DeviceAssetsClient::status() const { return status_; }
bool DeviceAssetsClient::cachedSpec(const char* id, Spec& result) { (void)id; (void)result; return false; }
bool DeviceAssetsClient::tryRead(const Spec& spec, std::size_t offset, void* bytes, std::size_t length, Result& result) { (void)spec; (void)offset; (void)bytes; (void)length; (void)result; return false; }
#endif
} // namespace digivice::assets
