#include "update.h"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <utility>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace th::update {
namespace {

Result failure(Error code, const wchar_t *message) {
    return {code, message};
}

bool valid_version(const std::string &value) {
    if (value.empty() || value.size() > 64) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               c == '.' || c == '_' || c == '-';
    });
}

bool trusted_release_url(const std::string &url) {
    static constexpr std::string_view prefix =
        "https://github.com/Arisaya1/tinyhand/releases/download/";
    if (url.size() <= prefix.size() || url.size() > 4096 || url.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    std::string_view rest(url.data() + prefix.size(), url.size() - prefix.size());
    const size_t slash = rest.find('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 >= rest.size() ||
        rest.find('/', slash + 1) != std::string_view::npos) {
        return false;
    }
    const std::string_view tag = rest.substr(0, slash);
    const std::string_view filename = rest.substr(slash + 1);
    if (tag == "." || tag == ".." || filename == "." || filename == ".." ||
        filename.size() < 4 || filename.substr(filename.size() - 4) != ".zip") {
        return false;
    }
    return std::all_of(rest.begin(), rest.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               c == '.' || c == '_' || c == '-' || c == '/';
    });
}

bool lower_hex_sha256(const std::string &hash) {
    return hash.size() == 64 &&
           std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    bool parse(std::array<std::string, 5> &values, uint64_t &size) {
        std::array<bool, 5> seen{};
        skip_space();
        if (!consume('{')) {
            return false;
        }
        skip_space();
        if (consume('}')) {
            return false;
        }
        for (size_t count = 0;; ++count) {
            if (count == 5) {
                return false;
            }
            std::string name;
            if (!string(name)) {
                return false;
            }
            size_t index = 5;
            for (size_t i = 0; i < names.size(); ++i) {
                if (name == names[i]) {
                    index = i;
                    break;
                }
            }
            if (index == 5 || seen[index]) {
                return false;
            }
            seen[index] = true;
            skip_space();
            if (!consume(':')) {
                return false;
            }
            skip_space();
            if (index == 2) {
                if (!number(size) || size == 0 || size > kMaxPackageBytes) {
                    return false;
                }
            } else if (!string(values[index])) {
                return false;
            }
            skip_space();
            if (consume('}')) {
                skip_space();
                return pos_ == input_.size() && std::all_of(seen.begin(), seen.end(), [](bool value) { return value; });
            }
            if (!consume(',')) {
                return false;
            }
            skip_space();
        }
    }

private:
    static constexpr std::array<std::string_view, 5> names = {"version", "url", "size", "sha256", "signature"};
    std::string_view input_;
    size_t pos_ = 0;

    void skip_space() {
        while (pos_ < input_.size() && (input_[pos_] == ' ' || input_[pos_] == '\t' || input_[pos_] == '\r' || input_[pos_] == '\n')) {
            ++pos_;
        }
    }

    bool consume(char c) {
        if (pos_ < input_.size() && input_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool string(std::string &out) {
        out.clear();
        if (!consume('"')) {
            return false;
        }
        while (pos_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[pos_++]);
            if (c == '"') {
                return true;
            }
            if (c < 0x20 || c >= 0x80) {
                return false;
            }
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            // Release manifests are ASCII and need no escaped characters. Rejecting
            // escapes also prevents alternate spellings of the signed payload values.
            return false;
        }
        return false;
    }

    bool number(uint64_t &out) {
        out = 0;
        const size_t start = pos_;
        while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') {
            const unsigned digit = static_cast<unsigned>(input_[pos_++] - '0');
            if (out > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
                return false;
            }
            out = out * 10 + digit;
        }
        return pos_ > start && (pos_ - start == 1 || input_[start] != '0');
    }
};

bool base64_decode(std::string_view text, std::vector<uint8_t> &out) {
    out.clear();
    if (text.empty() || text.size() % 4 != 0) {
        return false;
    }
    std::array<int8_t, 256> table{};
    table.fill(-1);
    const std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < alphabet.size(); ++i) {
        table[static_cast<unsigned char>(alphabet[i])] = static_cast<int8_t>(i);
    }
    out.reserve(text.size() / 4 * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        const bool pad2 = text[i + 2] == '=';
        const bool pad3 = text[i + 3] == '=';
        if ((!last && (pad2 || pad3)) || (pad2 && !pad3) || text[i] == '=' || text[i + 1] == '=') {
            return false;
        }
        const int a = table[static_cast<unsigned char>(text[i])];
        const int b = table[static_cast<unsigned char>(text[i + 1])];
        const int c = pad2 ? 0 : table[static_cast<unsigned char>(text[i + 2])];
        const int d = pad3 ? 0 : table[static_cast<unsigned char>(text[i + 3])];
        if (a < 0 || b < 0 || c < 0 || d < 0 || (pad2 && (b & 15)) || (pad3 && !pad2 && (c & 3))) {
            return false;
        }
        const uint32_t value = (uint32_t(a) << 18) | (uint32_t(b) << 12) | (uint32_t(c) << 6) | uint32_t(d);
        out.push_back(static_cast<uint8_t>(value >> 16));
        if (!pad2) {
            out.push_back(static_cast<uint8_t>(value >> 8));
        }
        if (!pad3) {
            out.push_back(static_cast<uint8_t>(value));
        }
    }
    return true;
}

bool read_length(const std::vector<uint8_t> &der, size_t &pos, size_t &length) {
    if (pos >= der.size()) {
        return false;
    }
    const uint8_t first = der[pos++];
    if ((first & 0x80) == 0) {
        length = first;
        return pos + length <= der.size();
    }
    if (first != 0x81 || pos >= der.size()) {
        return false;
    }
    length = der[pos++];
    return length >= 0x80 && pos + length <= der.size();
}

bool der_integer(const std::vector<uint8_t> &der, size_t &pos, std::array<uint8_t, 32> &out) {
    size_t length = 0;
    if (pos >= der.size() || der[pos++] != 0x02 || !read_length(der, pos, length) || length == 0 || length > 33 ||
        pos + length > der.size()) {
        return false;
    }
    const uint8_t *bytes = der.data() + pos;
    const size_t encoded_length = length;
    if ((bytes[0] & 0x80) || (length > 1 && bytes[0] == 0 && !(bytes[1] & 0x80))) {
        return false;
    }
    if (length == 33) {
        if (bytes[0] != 0) {
            return false;
        }
        ++bytes;
        --length;
    }
    out.fill(0);
    std::copy(bytes, bytes + length, out.end() - length);
    pos += encoded_length;
    return true;
}

bool der_to_raw(const std::vector<uint8_t> &der, std::array<uint8_t, 64> &raw) {
    size_t pos = 0;
    size_t length = 0;
    if (der.size() < 8 || der.size() > 128 || pos >= der.size() || der[pos++] != 0x30 ||
        !read_length(der, pos, length) || length != der.size() - pos) {
        return false;
    }
    std::array<uint8_t, 32> r{};
    std::array<uint8_t, 32> s{};
    if (!der_integer(der, pos, r) || !der_integer(der, pos, s) || pos != der.size()) {
        return false;
    }
    std::copy(r.begin(), r.end(), raw.begin());
    std::copy(s.begin(), s.end(), raw.begin() + 32);
    return true;
}

Result hash_payload(std::string_view payload, std::array<uint8_t, 32> &hash) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (NT_SUCCESS(status)) {
        status = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char *>(payload.data())),
                            static_cast<ULONG>(payload.size()), hash.data(), static_cast<ULONG>(hash.size()));
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    return NT_SUCCESS(status) ? Result{} : failure(Error::invalid_signature, L"could not hash manifest");
}

} // namespace

Result verify_manifest(std::string_view json, const std::vector<uint8_t> &public_key_blob, Manifest &out) {
    out = Manifest{};
    std::array<std::string, 5> values;
    uint64_t size = 0;
    JsonParser parser(json);
    if (!parser.parse(values, size) || !valid_version(values[0]) || !lower_hex_sha256(values[3])) {
        return failure(Error::invalid_manifest, L"manifest must contain exactly five valid fields");
    }
    if (!trusted_release_url(values[1])) {
        return failure(Error::untrusted_url, L"manifest URL is not a trusted TinyHand GitHub release");
    }
    std::vector<uint8_t> der;
    std::array<uint8_t, 64> raw{};
    if (!base64_decode(values[4], der) || !der_to_raw(der, raw)) {
        return failure(Error::invalid_signature, L"signature is not canonical base64-encoded ECDSA DER");
    }
    const std::string payload = values[0] + "\n" + values[1] + "\n" + std::to_string(size) + "\n" + values[3] + "\n";
    std::array<uint8_t, 32> hash{};
    Result result = hash_payload(payload, hash);
    if (!result) {
        return result;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0);
    if (NT_SUCCESS(status)) {
        status = BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
                                     const_cast<PUCHAR>(public_key_blob.data()), static_cast<ULONG>(public_key_blob.size()), 0);
    }
    if (NT_SUCCESS(status)) {
        status = BCryptVerifySignature(key, nullptr, hash.data(), static_cast<ULONG>(hash.size()), raw.data(),
                                       static_cast<ULONG>(raw.size()), 0);
    }
    if (key) {
        BCryptDestroyKey(key);
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    if (!NT_SUCCESS(status)) {
        return failure(Error::invalid_signature, L"manifest signature verification failed");
    }
    out.version_ = std::move(values[0]);
    out.url_ = std::move(values[1]);
    out.size_ = size;
    out.sha256_ = std::move(values[3]);
    out.signature_ = std::move(values[4]);
    out.verified_ = true;
    return {};
}

namespace {

class Hash256 {
public:
    ~Hash256() {
        if (hash_) {
            BCryptDestroyHash(hash_);
        }
        if (algorithm_) {
            BCryptCloseAlgorithmProvider(algorithm_, 0);
        }
    }

    bool open() {
        ULONG object_size = 0;
        ULONG written = 0;
        return NT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
               NT_SUCCESS(BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
                                            sizeof(object_size), &written, 0)) &&
               (object_.resize(object_size), true) &&
               NT_SUCCESS(BCryptCreateHash(algorithm_, &hash_, object_.data(), object_size, nullptr, 0, 0));
    }

    bool add(const uint8_t *data, size_t size) {
        return NT_SUCCESS(BCryptHashData(hash_, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0));
    }

    bool finish(std::array<uint8_t, 32> &out) {
        return NT_SUCCESS(BCryptFinishHash(hash_, out.data(), static_cast<ULONG>(out.size()), 0));
    }

private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<uint8_t> object_;
};

Result hash_open_file(HANDLE file, std::array<uint8_t, 32> &hash) {
    Hash256 context;
    if (!context.open()) {
        return failure(Error::io, L"could not initialize SHA-256");
    }
    std::array<uint8_t, 64 * 1024> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            return failure(Error::io, L"could not read package while hashing");
        }
        if (read == 0) {
            break;
        }
        if (!context.add(buffer.data(), read)) {
            return failure(Error::io, L"could not update package SHA-256");
        }
    }
    return context.finish(hash) ? Result{} : failure(Error::io, L"could not finish package SHA-256");
}

unsigned hex_digit(char c) {
    return c >= '0' && c <= '9' ? static_cast<unsigned>(c - '0') : static_cast<unsigned>(c - 'a' + 10);
}

bool package_hash_matches(const Manifest &manifest, const std::array<uint8_t, 32> &hash) {
    for (size_t i = 0; i < hash.size(); ++i) {
        const unsigned expected = (hex_digit(manifest.sha256()[i * 2]) << 4) | hex_digit(manifest.sha256()[i * 2 + 1]);
        if (hash[i] != expected) {
            return false;
        }
    }
    return true;
}

} // namespace

Result verify_package_file(const Manifest &manifest, const std::wstring &package_path) {
    if (!manifest.is_verified()) {
        return failure(Error::invalid_arguments, L"package requires a verified manifest");
    }
    HANDLE file = CreateFileW(package_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return failure(Error::io, L"could not open package");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) != manifest.size()) {
        CloseHandle(file);
        return failure(Error::size_mismatch, L"package size does not match signed manifest");
    }
    std::array<uint8_t, 32> hash{};
    Result result = hash_open_file(file, hash);
    CloseHandle(file);
    if (!result) {
        return result;
    }
    if (!package_hash_matches(manifest, hash)) {
        return failure(Error::hash_mismatch, L"package SHA-256 does not match signed manifest");
    }
    return {};
}

namespace {

uint16_t zip16(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<uint16_t>(data[offset] | (static_cast<uint16_t>(data[offset + 1]) << 8));
}

uint32_t zip32(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

bool archive_range(size_t offset, size_t length, size_t size) {
    return offset <= size && length <= size - offset;
}

bool hand_asset_name(std::string_view name) {
    static constexpr std::string_view prefix = "assets/hand_";
    static constexpr std::string_view suffix = ".png";
    if (name.size() <= prefix.size() + suffix.size() || name.compare(0, prefix.size(), prefix) != 0 ||
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return false;
    }
    const std::string_view stem = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    return std::all_of(stem.begin(), stem.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-';
    });
}

bool allowed_member(std::string_view name, bool directory) {
    if (directory) {
        return name == "assets/";
    }
    return name == "TinyHand.exe" || name == "TinyHandUpdater.exe" || name == "LICENSE.txt" || name == "VERSION" ||
           hand_asset_name(name);
}

struct ZipMember {
    std::string name;
    uint64_t local_offset = 0;
    uint64_t uncompressed_size = 0;
    bool directory = false;
    uint32_t compressed_size = 0;
    uint16_t flags = 0;
    uint16_t method = 0;
};

bool read_package_bytes(const std::wstring &path, std::vector<uint8_t> &data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= static_cast<LONGLONG>(kMaxPackageBytes);
    if (ok) {
        try {
            data.resize(static_cast<size_t>(size.QuadPart));
        } catch (const std::bad_alloc &) {
            ok = false;
        }
    }
    size_t offset = 0;
    while (ok && offset < data.size()) {
        DWORD read = 0;
        const DWORD wanted = static_cast<DWORD>(std::min<size_t>(data.size() - offset, 1024 * 1024));
        ok = ReadFile(file, data.data() + offset, wanted, &read, nullptr) && read != 0;
        offset += read;
    }
    CloseHandle(file);
    return ok && offset == data.size();
}

bool validate_zip(const std::vector<uint8_t> &data, std::vector<ZipMember> &members) {
    if (data.size() < 22 || data.size() > kMaxPackageBytes) {
        return false;
    }
    const size_t minimum = data.size() > 22 + 65535 ? data.size() - 22 - 65535 : 0;
    size_t end = std::numeric_limits<size_t>::max();
    for (size_t pos = data.size() - 22;; --pos) {
        if (zip32(data, pos) == 0x06054b50 && zip16(data, pos + 20) == data.size() - pos - 22) {
            end = pos;
            break;
        }
        if (pos == minimum) {
            break;
        }
    }
    if (end == std::numeric_limits<size_t>::max() || zip16(data, end + 4) != 0 || zip16(data, end + 6) != 0 ||
        zip16(data, end + 8) != zip16(data, end + 10)) {
        return false;
    }
    const size_t count = zip16(data, end + 10);
    const size_t central_size = zip32(data, end + 12);
    const size_t central_offset = zip32(data, end + 16);
    if (count == 0 || count > 4096 || central_offset + central_size != end) {
        return false;
    }
    std::set<std::string> unique;
    uint64_t expanded = 0;
    bool executable = false;
    size_t pos = central_offset;
    for (size_t index = 0; index < count; ++index) {
        if (!archive_range(pos, 46, end) || zip32(data, pos) != 0x02014b50) {
            return false;
        }
        const uint16_t made_by = zip16(data, pos + 4);
        const uint16_t flags = zip16(data, pos + 8);
        const uint16_t method = zip16(data, pos + 10);
        const uint32_t compressed = zip32(data, pos + 20);
        const uint32_t uncompressed = zip32(data, pos + 24);
        const size_t name_length = zip16(data, pos + 28);
        const size_t extra_length = zip16(data, pos + 30);
        const size_t comment_length = zip16(data, pos + 32);
        const uint32_t attributes = zip32(data, pos + 38);
        const uint32_t local_offset = zip32(data, pos + 42);
        if (name_length == 0 || !archive_range(pos + 46, name_length + extra_length + comment_length, end) ||
            (flags & ((1 << 0) | (1 << 5) | (1 << 6) | (1 << 13))) || (method != 0 && method != 8) ||
            compressed == 0xffffffffu || uncompressed == 0xffffffffu || local_offset == 0xffffffffu ||
            zip16(data, pos + 34) != 0) {
            return false;
        }
        const std::string name(reinterpret_cast<const char *>(data.data() + pos + 46), name_length);
        const bool directory = !name.empty() && name.back() == '/';
        if (!allowed_member(name, directory) || (directory && (compressed != 0 || uncompressed != 0))) {
            return false;
        }
        const uint16_t host = static_cast<uint16_t>(made_by >> 8);
        if (host == 3 || host == 19) {
            const uint32_t type = (attributes >> 16) & 0xf000u;
            if (type != 0 && type != (directory ? 0x4000u : 0x8000u)) {
                return false;
            }
        } else if (!!(attributes & 0x10) != directory) {
            return false;
        }
        std::string folded = name;
        std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
        });
        if (!unique.insert(folded).second) {
            return false;
        }
        expanded += uncompressed;
        if (expanded > 8ull * kMaxPackageBytes) {
            return false;
        }
        executable = executable || name == "TinyHand.exe";
        members.push_back({name, local_offset, uncompressed, directory, compressed, flags, method});
        pos += 46 + name_length + extra_length + comment_length;
    }
    if (pos != end || !executable) {
        return false;
    }
    std::vector<size_t> order(members.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return members[a].local_offset < members[b].local_offset; });
    for (size_t index = 0; index < order.size(); ++index) {
        const ZipMember &member = members[order[index]];
        const size_t local = static_cast<size_t>(member.local_offset);
        if (!archive_range(local, 30, central_offset) || zip32(data, local) != 0x04034b50) {
            return false;
        }
        const uint16_t local_flags = zip16(data, local + 6);
        const uint16_t method = zip16(data, local + 8);
        const size_t name_length = zip16(data, local + 26);
        const size_t extra_length = zip16(data, local + 28);
        if (name_length != member.name.size() ||
            !archive_range(local + 30, name_length + extra_length, central_offset) ||
            std::string(reinterpret_cast<const char *>(data.data() + local + 30), name_length) != member.name) {
            return false;
        }
        if (local_flags != member.flags || method != member.method ||
            !archive_range(local + 30 + name_length + extra_length, member.compressed_size, central_offset)) {
            return false;
        }
        const size_t data_end = local + 30 + name_length + extra_length + member.compressed_size;
        if (index + 1 < order.size() && data_end > members[order[index + 1]].local_offset) {
            return false;
        }
        if (index > 0 && member.local_offset == members[order[index - 1]].local_offset) {
            return false;
        }
    }
    return true;
}

std::wstring quote_argument(const std::wstring &value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
        } else if (c == L'\"') {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(c);
            slashes = 0;
        } else {
            out.append(slashes, L'\\');
            slashes = 0;
            out.push_back(c);
        }
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

bool run_tar(const std::wstring &archive, const std::wstring &destination, const std::vector<ZipMember> &members) {
    wchar_t windows_directory[MAX_PATH]{};
    if (!GetWindowsDirectoryW(windows_directory, MAX_PATH)) {
        return false;
    }
    const std::wstring tar = std::wstring(windows_directory) + L"\\System32\\tar.exe";
    std::wstring command = quote_argument(tar) + L" -xf " + quote_argument(archive) + L" -C " +
                           quote_argument(destination) + L" --no-same-owner --no-same-permissions --";
    for (const ZipMember &member : members) {
        if (!member.directory) {
            command += L" " + quote_argument(std::wstring(member.name.begin(), member.name.end()));
        }
    }
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nul == INVALID_HANDLE_VALUE) {
        return false;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = nul;
    startup.hStdOutput = nul;
    startup.hStdError = nul;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(0);
    const BOOL created = CreateProcessW(tar.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup, &process);
    CloseHandle(nul);
    if (!created) {
        return false;
    }
    const DWORD waited = WaitForSingleObject(process.hProcess, 5 * 60 * 1000);
    DWORD exit_code = 1;
    if (waited == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
    } else if (waited == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &exit_code);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return waited == WAIT_OBJECT_0 && exit_code == 0;
}

bool write_verified_snapshot(const std::wstring &path, const std::vector<uint8_t> &bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    size_t offset = 0;
    bool ok = true;
    while (ok && offset < bytes.size()) {
        DWORD written = 0;
        const DWORD wanted = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1024 * 1024));
        ok = WriteFile(file, bytes.data() + offset, wanted, &written, nullptr) && written == wanted;
        offset += written;
    }
    ok = ok && FlushFileBuffers(file);
    CloseHandle(file);
    return ok && offset == bytes.size();
}

void cleanup_staging(const std::wstring &staging, const std::vector<ZipMember> &members) {
    DeleteFileW((staging + L"\\verified-package.zip").c_str());
    for (const ZipMember &member : members) {
        if (!member.directory) {
            DeleteFileW((staging + L"\\" + std::wstring(member.name.begin(), member.name.end())).c_str());
        }
    }
    RemoveDirectoryW((staging + L"\\assets").c_str());
    RemoveDirectoryW(staging.c_str());
}

} // namespace

Result validate_and_extract_package(const Manifest &manifest, const std::wstring &package_path,
                                    const std::wstring &staging_directory) {
    Result verified = verify_package_file(manifest, package_path);
    if (!verified) {
        return verified;
    }
    if (staging_directory.empty() || staging_directory.find(L'\"') != std::wstring::npos ||
        GetFileAttributesW(staging_directory.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return failure(Error::invalid_target, L"staging path must be new");
    }
    std::vector<uint8_t> bytes;
    if (!read_package_bytes(package_path, bytes)) {
        return failure(Error::io, L"could not read package for ZIP validation");
    }
    std::vector<ZipMember> members;
    Hash256 context;
    std::array<uint8_t, 32> digest{};
    if (bytes.size() != manifest.size() || !context.open() || !context.add(bytes.data(), bytes.size()) ||
        !context.finish(digest) || !package_hash_matches(manifest, digest)) {
        return failure(Error::hash_mismatch, L"package changed while being read for extraction");
    }
    if (!validate_zip(bytes, members)) {
        return failure(Error::invalid_archive, L"package ZIP failed strict member validation");
    }
    if (!CreateDirectoryW(staging_directory.c_str(), nullptr)) {
        return failure(Error::io, L"could not create fresh staging directory");
    }
    const std::wstring snapshot = staging_directory + L"\\verified-package.zip";
    if (!write_verified_snapshot(snapshot, bytes)) {
        cleanup_staging(staging_directory, members);
        return failure(Error::io, L"could not snapshot verified package for extraction");
    }
    if (!run_tar(snapshot, staging_directory, members) || !DeleteFileW(snapshot.c_str())) {
        cleanup_staging(staging_directory, members);
        return failure(Error::invalid_archive, L"System32 tar.exe could not securely extract package");
    }
    for (const ZipMember &member : members) {
        const std::wstring path = staging_directory + L"\\" + std::wstring(member.name.begin(), member.name.end());
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !!(attributes & FILE_ATTRIBUTE_DIRECTORY) != member.directory) {
            cleanup_staging(staging_directory, members);
            return failure(Error::invalid_archive, L"extracted package member has an unsafe type");
        }
    }
    return {};
}

namespace {

std::wstring canonical_path(const std::wstring &path) {
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) {
        return {};
    }
    std::wstring out(needed, 0);
    if (!GetFullPathNameW(path.c_str(), needed, out.data(), nullptr)) {
        return {};
    }
    out.resize(needed - 1);
    std::replace(out.begin(), out.end(), L'/', L'\\');
    while (out.size() > 3 && out.back() == L'\\') {
        out.pop_back();
    }
    return out;
}

bool ordinary_directory(const std::wstring &path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

bool narrow_ascii(const wchar_t *wide, std::string &out) {
    out.clear();
    for (; *wide; ++wide) {
        if (*wide > 0x7f) {
            return false;
        }
        out.push_back(static_cast<char>(*wide));
    }
    return true;
}

bool collect_files(const std::wstring &directory, const std::wstring &prefix, bool assets,
                   std::vector<std::pair<std::wstring, std::wstring>> &files, bool &executable) {
    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW((directory + L"\\*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = true;
    do {
        const std::wstring name = found.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const bool directory_entry = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const std::wstring relative = prefix + name;
        std::string ascii;
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !narrow_ascii(name.c_str(), ascii)) {
            ok = false;
            break;
        }
        if (directory_entry) {
            if (assets || name != L"assets" || !collect_files(directory + L"\\" + name, L"assets\\", true, files, executable)) {
                ok = false;
                break;
            }
            continue;
        }
        std::string narrow_relative;
        if (!narrow_ascii(relative.c_str(), narrow_relative)) {
            ok = false;
            break;
        }
        std::replace(narrow_relative.begin(), narrow_relative.end(), '\\', '/');
        if (!allowed_member(narrow_relative, false)) {
            ok = false;
            break;
        }
        executable = executable || narrow_relative == "TinyHand.exe";
        files.push_back({relative, name});
    } while (FindNextFileW(search, &found));
    const DWORD error = GetLastError();
    FindClose(search);
    return ok && (error == ERROR_NO_MORE_FILES || error == ERROR_SUCCESS);
}

bool nested_or_same_path(const std::wstring &left, const std::wstring &right) {
    if (_wcsicmp(left.c_str(), right.c_str()) == 0) {
        return true;
    }
    const std::wstring prefix = right + L"\\";
    return _wcsnicmp(left.c_str(), prefix.c_str(), prefix.size()) == 0;
}

} // namespace

Result install_staged_package(const Manifest &manifest, const std::wstring &staging_directory,
                              const std::wstring &target_directory) {
    if (!manifest.is_verified()) {
        return failure(Error::invalid_arguments, L"installation requires a verified manifest");
    }
    const std::wstring staging = canonical_path(staging_directory);
    const std::wstring target = canonical_path(target_directory);
    if (staging.empty() || target.empty() || !ordinary_directory(staging) || !ordinary_directory(target) ||
        nested_or_same_path(staging, target) || nested_or_same_path(target, staging)) {
        return failure(Error::invalid_target, L"staging and target must be separate ordinary directories");
    }
    std::vector<std::pair<std::wstring, std::wstring>> files;
    bool executable = false;
    if (!collect_files(staging, L"", false, files, executable) || !executable) {
        return failure(Error::invalid_archive, L"staging contains an unexpected member or no TinyHand.exe");
    }
    bool needs_assets = false;
    for (const auto &file : files) {
        needs_assets = needs_assets || file.first.compare(0, 7, L"assets\\") == 0;
    }
    if (needs_assets) {
        const std::wstring assets = target + L"\\assets";
        if (GetFileAttributesW(assets.c_str()) == INVALID_FILE_ATTRIBUTES && !CreateDirectoryW(assets.c_str(), nullptr)) {
            return failure(Error::io, L"could not create target assets directory");
        }
        if (!ordinary_directory(assets)) {
            return failure(Error::invalid_target, L"target assets is not an ordinary directory");
        }
    }
    for (const auto &file : files) {
        const std::wstring source = staging + L"\\" + file.first;
        const std::wstring destination = target + L"\\" + file.first;
        if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return failure(Error::io, L"could not replace an allowed target file");
        }
    }
    return {};
}

namespace {

struct WinHttpHandles {
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;

    ~WinHttpHandles() {
        if (request) {
            WinHttpCloseHandle(request);
        }
        if (connection) {
            WinHttpCloseHandle(connection);
        }
        if (session) {
            WinHttpCloseHandle(session);
        }
    }
};

Result network_failure() {
    return failure(Error::network, L"secure package download failed");
}

} // namespace

Result download_package(const Manifest &manifest, const std::wstring &destination) {
    if (!manifest.is_verified()) {
        return failure(Error::invalid_arguments, L"download requires a verified manifest");
    }
    if (destination.empty() || GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return failure(Error::invalid_target, L"download destination must be new");
    }
    const std::wstring partial = destination + L".partial";
    if (GetFileAttributesW(partial.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return failure(Error::invalid_target, L"download partial file already exists");
    }
    const std::wstring url(manifest.url().begin(), manifest.url().end());
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components) ||
        components.nScheme != INTERNET_SCHEME_HTTPS || components.nPort != INTERNET_DEFAULT_HTTPS_PORT) {
        return failure(Error::untrusted_url, L"download URL is not default-port HTTPS");
    }
    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    const std::wstring path = std::wstring(components.lpszUrlPath, components.dwUrlPathLength) +
                              std::wstring(components.lpszExtraInfo, components.dwExtraInfoLength);
    WinHttpHandles http;
    http.session = WinHttpOpen(L"TinyHand-Update/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!http.session) {
        return network_failure();
    }
    WinHttpSetTimeouts(http.session, 30000, 30000, 30000, 30000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    if (!WinHttpSetOption(http.session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(http.session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(http.session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy))) {
        return network_failure();
    }
    http.connection = WinHttpConnect(http.session, host.c_str(), components.nPort, 0);
    if (!http.connection) {
        return network_failure();
    }
    http.request = WinHttpOpenRequest(http.connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!http.request || !WinHttpSendRequest(http.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(http.request, nullptr)) {
        return network_failure();
    }
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(http.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX) ||
        status != HTTP_STATUS_OK) {
        return network_failure();
    }
    DWORD declared_size = 0;
    DWORD declared_size_size = sizeof(declared_size);
    if (WinHttpQueryHeaders(http.request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &declared_size, &declared_size_size, WINHTTP_NO_HEADER_INDEX) &&
        declared_size != manifest.size()) {
        return failure(Error::size_mismatch, L"server package length does not match signed manifest");
    }
    HANDLE file = CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return failure(Error::io, L"could not create download partial file");
    }
    uint64_t downloaded = 0;
    std::array<uint8_t, 64 * 1024> buffer{};
    Result result;
    while (downloaded < manifest.size()) {
        DWORD read = 0;
        const DWORD wanted = static_cast<DWORD>(std::min<uint64_t>(buffer.size(), manifest.size() - downloaded));
        if (!WinHttpReadData(http.request, buffer.data(), wanted, &read)) {
            result = network_failure();
            break;
        }
        if (read == 0) {
            result = failure(Error::size_mismatch, L"download ended before signed package size");
            break;
        }
        DWORD written = 0;
        if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read) {
            result = failure(Error::io, L"could not write download partial file");
            break;
        }
        downloaded += read;
    }
    if (result && downloaded == manifest.size()) {
        DWORD extra = 0;
        if (!WinHttpReadData(http.request, buffer.data(), 1, &extra)) {
            result = network_failure();
        } else if (extra != 0) {
            result = failure(Error::size_mismatch, L"download exceeded signed package size");
        }
    }
    if (result && !FlushFileBuffers(file)) {
        result = failure(Error::io, L"could not flush download partial file");
    }
    CloseHandle(file);
    if (!result) {
        DeleteFileW(partial.c_str());
        return result;
    }
    result = verify_package_file(manifest, partial);
    if (!result) {
        DeleteFileW(partial.c_str());
        return result;
    }
    if (!MoveFileExW(partial.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(partial.c_str());
        return failure(Error::io, L"could not publish verified download");
    }
    return {};
}

} // namespace th::update