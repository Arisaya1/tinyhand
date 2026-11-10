// Isolated native Windows update-backend tests. No application or input self-tests.
#include "update.h"

#include <windows.h>
#include <bcrypt.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace {

int failures = 0;

void expect(bool condition, const char *name) {
    if (condition) {
        std::printf("PASS %s\n", name);
    } else {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

struct Key {
    BCRYPT_KEY_HANDLE key = nullptr;
    std::vector<uint8_t> public_blob;

    Key() {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        ULONG bytes = 0;
        if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0)) ||
            !NT_SUCCESS(BCryptGenerateKeyPair(algorithm, &key, 256, 0)) ||
            !NT_SUCCESS(BCryptFinalizeKeyPair(key, 0)) ||
            !NT_SUCCESS(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &bytes, 0))) {
            return;
        }
        public_blob.resize(bytes);
        if (!NT_SUCCESS(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, public_blob.data(), bytes, &bytes, 0))) {
            public_blob.clear();
        }
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }

    ~Key() {
        if (key) {
            BCryptDestroyKey(key);
        }
    }

    Key(const Key &) = delete;
    Key &operator=(const Key &) = delete;
};

std::vector<uint8_t> sha256(const std::string &input) {
    std::vector<uint8_t> hash(32);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return {};
    }
    NTSTATUS status = BCryptHash(algorithm, nullptr, 0,
                                 reinterpret_cast<PUCHAR>(const_cast<char *>(input.data())),
                                 static_cast<ULONG>(input.size()), hash.data(), static_cast<ULONG>(hash.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!NT_SUCCESS(status)) {
        hash.clear();
    }
    return hash;
}

std::string hex_hash(const std::string &input) {
    static constexpr char digits[] = "0123456789abcdef";
    const std::vector<uint8_t> bytes = sha256(input);
    std::string out;
    for (uint8_t byte : bytes) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 15]);
    }
    return out;
}

std::vector<uint8_t> sign_raw(BCRYPT_KEY_HANDLE key, const std::string &payload) {
    std::vector<uint8_t> hash = sha256(payload);
    ULONG size = 0;
    if (hash.empty() || !NT_SUCCESS(BCryptSignHash(key, nullptr, hash.data(), static_cast<ULONG>(hash.size()),
                                                  nullptr, 0, &size, 0))) {
        return {};
    }
    std::vector<uint8_t> signature(size);
    if (!NT_SUCCESS(BCryptSignHash(key, nullptr, hash.data(), static_cast<ULONG>(hash.size()),
                                   signature.data(), size, &size, 0))) {
        return {};
    }
    signature.resize(size);
    return signature;
}

std::vector<uint8_t> der_integer(const uint8_t *value, size_t size) {
    while (size > 1 && value[0] == 0) {
        ++value;
        --size;
    }
    bool pad = (value[0] & 0x80) != 0;
    std::vector<uint8_t> out{0x02, static_cast<uint8_t>(size + (pad ? 1 : 0))};
    if (pad) {
        out.push_back(0);
    }
    out.insert(out.end(), value, value + size);
    return out;
}

std::vector<uint8_t> raw_to_der(const std::vector<uint8_t> &raw) {
    if (raw.size() != 64) {
        return {};
    }
    std::vector<uint8_t> r = der_integer(raw.data(), 32);
    std::vector<uint8_t> s = der_integer(raw.data() + 32, 32);
    std::vector<uint8_t> body = r;
    body.insert(body.end(), s.begin(), s.end());
    std::vector<uint8_t> der{0x30, static_cast<uint8_t>(body.size())};
    der.insert(der.end(), body.begin(), body.end());
    return der;
}

std::string base64(const std::vector<uint8_t> &bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        uint32_t value = static_cast<uint32_t>(bytes[i]) << 16;
        if (i + 1 < bytes.size()) value |= static_cast<uint32_t>(bytes[i + 1]) << 8;
        if (i + 2 < bytes.size()) value |= bytes[i + 2];
        out.push_back(alphabet[(value >> 18) & 63]);
        out.push_back(alphabet[(value >> 12) & 63]);
        out.push_back(i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=');
        out.push_back(i + 2 < bytes.size() ? alphabet[value & 63] : '=');
    }
    return out;
}

std::string signed_manifest(Key &key, const std::string &version = "1.2.3",
                            const std::string &url =
                                "https://github.com/Arisaya1/tinyhand/releases/download/v1.2.3/TinyHand-1.2.3-windows-x64.zip",
                            uint64_t size = 12345, const std::string &hash = std::string(64, '0')) {
    std::string payload = version + "\n" + url + "\n" + std::to_string(size) + "\n" + hash + "\n";
    std::vector<uint8_t> signature = raw_to_der(sign_raw(key.key, payload));
    return "{\"version\":\"" + version + "\",\"url\":\"" + url + "\",\"size\":" +
           std::to_string(size) + ",\"sha256\":\"" + hash + "\",\"signature\":\"" + base64(signature) + "\"}";
}

struct TempDirectory {
    std::wstring path;

    TempDirectory() {
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executable, MAX_PATH);
        std::wstring base(executable);
        base.resize(base.find_last_of(L"\\/") + 1);
        path = base + L"update-test-data-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
               std::to_wstring(GetTickCount64());
        CreateDirectoryW(path.c_str(), nullptr);
    }

    std::wstring child(const std::wstring &name) const { return path + L"\\" + name; }
};

bool write_bytes(const std::wstring &path, const std::string &bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

void put16(std::string &out, uint16_t value) {
    out.push_back(static_cast<char>(value));
    out.push_back(static_cast<char>(value >> 8));
}

void put32(std::string &out, uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8) {
        out.push_back(static_cast<char>(value >> shift));
    }
}

uint32_t crc32(const std::string &data) {
    uint32_t crc = 0xffffffffu;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (unsigned bit = 0; bit != 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xffffffffu;
}

struct ZipEntry {
    std::string name;
    std::string data;
    uint16_t made_by = 20;
    uint32_t attributes = 0x81a40000u;
};

std::string make_zip(const std::vector<ZipEntry> &entries) {
    std::string out;
    std::vector<uint32_t> offsets;
    for (const ZipEntry &entry : entries) {
        offsets.push_back(static_cast<uint32_t>(out.size()));
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put32(out, crc32(entry.data));
        put32(out, static_cast<uint32_t>(entry.data.size()));
        put32(out, static_cast<uint32_t>(entry.data.size()));
        put16(out, static_cast<uint16_t>(entry.name.size()));
        put16(out, 0);
        out += entry.name;
        out += entry.data;
    }
    const uint32_t central_offset = static_cast<uint32_t>(out.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry &entry = entries[i];
        put32(out, 0x02014b50);
        put16(out, entry.made_by);
        put16(out, 20);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put32(out, crc32(entry.data));
        put32(out, static_cast<uint32_t>(entry.data.size()));
        put32(out, static_cast<uint32_t>(entry.data.size()));
        put16(out, static_cast<uint16_t>(entry.name.size()));
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put32(out, entry.attributes);
        put32(out, offsets[i]);
        out += entry.name;
    }
    const uint32_t central_size = static_cast<uint32_t>(out.size()) - central_offset;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<uint16_t>(entries.size()));
    put16(out, static_cast<uint16_t>(entries.size()));
    put32(out, central_size);
    put32(out, central_offset);
    put16(out, 0);
    return out;
}

th::update::Result manifest_for(Key &key, const std::string &package, th::update::Manifest &manifest) {
    return th::update::verify_manifest(
        signed_manifest(key, "1.2.3",
                        "https://github.com/Arisaya1/tinyhand/releases/download/v1.2.3/TinyHand-1.2.3-windows-x64.zip",
                        package.size(), hex_hash(package)),
        key.public_blob, manifest);
}

void test_zip_validation() {
    TempDirectory temp;
    Key key;
    int index = 0;
    auto attempt = [&](const char *name, std::vector<ZipEntry> entries, bool success) {
        const std::string package = make_zip(entries);
        const std::wstring archive = temp.child(L"archive-" + std::to_wstring(index) + L".zip");
        const std::wstring staging = temp.child(L"staging-" + std::to_wstring(index));
        ++index;
        th::update::Manifest manifest;
        const auto verified = manifest_for(key, package, manifest);
        const bool wrote = write_bytes(archive, package);
        const auto extracted = th::update::validate_and_extract_package(manifest, archive, staging);
        expect(wrote && verified && (success ? extracted.ok() : extracted.code == th::update::Error::invalid_archive), name);
        if (success) {
            DWORD attributes = GetFileAttributesW((staging + L"\\TinyHand.exe").c_str());
            expect(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY),
                   "extract expected executable");
        } else {
            expect(GetFileAttributesW(staging.c_str()) == INVALID_FILE_ATTRIBUTES, "reject archive before creating staging");
        }
    };

    attempt("accept allowed ZIP members", {{"TinyHand.exe", "new exe"}, {"VERSION", "1.2.3\n"},
                                           {"assets/", "", static_cast<uint16_t>((3 << 8) | 20), 0x41ed0010u},
                                           {"assets/hand_main.png", "png"}}, true);
    attempt("reject traversal ZIP member", {{"TinyHand.exe", "new exe"}, {"../escape", "bad"}}, false);
    attempt("reject absolute ZIP member", {{"/TinyHand.exe", "bad"}}, false);
    attempt("reject backslash ZIP member", {{"assets\\hand_main.png", "bad"}}, false);
    attempt("reject alternate data stream member", {{"TinyHand.exe:evil", "bad"}}, false);
    attempt("reject duplicate ZIP members", {{"TinyHand.exe", "one"}, {"TinyHand.exe", "two"}}, false);
    attempt("reject unexpected ZIP members", {{"evil.txt", "bad"}}, false);
    attempt("reject symbolic links", {{"TinyHand.exe", "target", static_cast<uint16_t>((3 << 8) | 20), 0xa1ff0000u}},
            false);
}

std::string read_bytes(const std::wstring &path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    std::string out;
    char buffer[256];
    DWORD count = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &count, nullptr) && count != 0) {
        out.append(buffer, count);
    }
    CloseHandle(file);
    return out;
}

void test_install() {
    TempDirectory temp;
    Key key;
    const std::string package = make_zip({{"TinyHand.exe", "new exe"}, {"TinyHandUpdater.exe", "new updater"},
                                          {"LICENSE.txt", "license"}, {"VERSION", "1.2.3\n"},
                                          {"assets/hand_main.png", "new png"}});
    const std::wstring archive = temp.child(L"install.zip");
    const std::wstring staging = temp.child(L"install-staging");
    const std::wstring target = temp.child(L"install-target");
    th::update::Manifest manifest;
    const auto verified = manifest_for(key, package, manifest);
    CreateDirectoryW(target.c_str(), nullptr);
    const bool fixtures = verified && write_bytes(archive, package) && write_bytes(temp.child(L"install-target\\TinyHand.exe"), "old exe") &&
                          write_bytes(temp.child(L"install-target\\unrelated.dll"), "keep me");
    const auto extracted = th::update::validate_and_extract_package(manifest, archive, staging);
    const auto installed = th::update::install_staged_package(manifest, staging, target);
    if (!extracted || !installed) {
        std::fwprintf(stderr, L"extract: %d %ls; install: %d %ls\n", static_cast<int>(extracted.code),
                      extracted.message.c_str(), static_cast<int>(installed.code), installed.message.c_str());
    }
    expect(fixtures && extracted && installed && read_bytes(temp.child(L"install-target\\TinyHand.exe")) == "new exe" &&
               read_bytes(temp.child(L"install-target\\VERSION")) == "1.2.3\n" &&
               read_bytes(temp.child(L"install-target\\assets\\hand_main.png")) == "new png" &&
               read_bytes(temp.child(L"install-target\\unrelated.dll")) == "keep me",
           "install only allowed staged files");
}

void test_package_hash_and_size() {
    TempDirectory temp;
    Key key;
    const std::string package = "offline package bytes";
    const std::wstring path = temp.child(L"package.zip");
    expect(write_bytes(path, package), "write local package fixture");

    th::update::Manifest correct;
    auto verified = th::update::verify_manifest(
        signed_manifest(key, "1.2.3",
                        "https://github.com/Arisaya1/tinyhand/releases/download/v1.2.3/TinyHand-1.2.3-windows-x64.zip",
                        package.size(), hex_hash(package)),
        key.public_blob, correct);
    expect(verified && th::update::verify_package_file(correct, path).ok(), "local package hash and size");

    th::update::Manifest wrong_size;
    verified = th::update::verify_manifest(
        signed_manifest(key, "1.2.3",
                        "https://github.com/Arisaya1/tinyhand/releases/download/v1.2.3/TinyHand-1.2.3-windows-x64.zip",
                        package.size() + 1, hex_hash(package)),
        key.public_blob, wrong_size);
    expect(verified && th::update::verify_package_file(wrong_size, path).code == th::update::Error::size_mismatch,
           "reject local package size mismatch");

    th::update::Manifest wrong_hash;
    verified = th::update::verify_manifest(
        signed_manifest(key, "1.2.3",
                        "https://github.com/Arisaya1/tinyhand/releases/download/v1.2.3/TinyHand-1.2.3-windows-x64.zip",
                        package.size(), std::string(64, '1')),
        key.public_blob, wrong_hash);
    expect(verified && th::update::verify_package_file(wrong_hash, path).code == th::update::Error::hash_mismatch,
           "reject local package hash mismatch");

    th::update::Manifest unverified;
    expect(th::update::verify_package_file(unverified, path).code == th::update::Error::invalid_arguments,
           "reject package for unverified manifest");
    expect(th::update::download_package(unverified, temp.child(L"download.zip")).code ==
               th::update::Error::invalid_arguments,
           "refuse download for unverified manifest");
}

void test_manifest_rejections() {
    Key key;
    Key other_key;
    th::update::Manifest manifest;
    std::string json = signed_manifest(key);
    expect(th::update::verify_manifest(json, key.public_blob, manifest) &&
               !th::update::verify_manifest(json, other_key.public_blob, manifest) && !manifest.is_verified(),
           "reject wrong public key and clear output");

    std::string tampered = json;
    tampered.replace(tampered.find("1.2.3"), 5, "9.9.9");
    expect(th::update::verify_manifest(tampered, key.public_blob, manifest).code == th::update::Error::invalid_signature,
           "reject modified signed payload");

    std::string bad_signature = json;
    const size_t signature = bad_signature.find("\"signature\":\"") + 13;
    bad_signature[signature] = bad_signature[signature] == 'A' ? 'B' : 'A';
    expect(th::update::verify_manifest(bad_signature, key.public_blob, manifest).code ==
               th::update::Error::invalid_signature,
           "reject bad DER signature");

    th::update::Result result = th::update::verify_manifest(
        signed_manifest(key, "1.2.3", "https://example.com/Arisaya1/tinyhand/releases/download/v1.2.3/a.zip"),
        key.public_blob, manifest);
    expect(result.code == th::update::Error::untrusted_url, "reject non-GitHub host");
    result = th::update::verify_manifest(
        signed_manifest(key, "1.2.3", "http://github.com/Arisaya1/tinyhand/releases/download/v1.2.3/a.zip"),
        key.public_blob, manifest);
    expect(result.code == th::update::Error::untrusted_url, "reject insecure release scheme");
    result = th::update::verify_manifest(signed_manifest(key, "1.2.3",
            "https://github.com/Arisaya1/tinyhand/releases/download/v1.2.3/a.zip", th::update::kMaxPackageBytes + 1),
        key.public_blob, manifest);
    expect(result.code == th::update::Error::invalid_manifest, "reject package over 200 MiB");

    std::string extra = json;
    extra.insert(extra.size() - 1, ",\"extra\":true");
    expect(th::update::verify_manifest(extra, key.public_blob, manifest).code == th::update::Error::invalid_manifest,
           "reject extra manifest field");
    std::string duplicate = json;
    duplicate.insert(duplicate.size() - 1, ",\"size\":12345");
    expect(th::update::verify_manifest(duplicate, key.public_blob, manifest).code == th::update::Error::invalid_manifest,
           "reject duplicate manifest field");
}

void test_valid_manifest() {
    Key key;
    th::update::Manifest manifest;
    th::update::Result result = th::update::verify_manifest(signed_manifest(key), key.public_blob, manifest);
    if (!result) {
        std::fwprintf(stderr, L"verify_manifest: %ls\n", result.message.c_str());
    }
    expect(result.ok() && manifest.version() == "1.2.3" && manifest.size() == 12345 &&
               manifest.sha256() == std::string(64, '0') && manifest.is_verified(),
           "valid signed manifest");
}

} // namespace

int main() {
    test_valid_manifest();
    test_manifest_rejections();
    test_package_hash_and_size();
    test_zip_validation();
    test_install();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
