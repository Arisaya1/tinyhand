#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace th::update {

inline constexpr uint64_t kMaxPackageBytes = 200ull * 1024ull * 1024ull;

// Stable categories let the UI present failures without parsing diagnostic text.
enum class Error {
    none,
    invalid_manifest,
    untrusted_url,
    invalid_signature,
    network,
    io,
    size_mismatch,
    hash_mismatch,
    invalid_archive,
    invalid_target,
    relaunch_failed,
    invalid_arguments,
};

struct Result {
    Error code = Error::none;
    std::wstring message;

    bool ok() const { return code == Error::none; }
    explicit operator bool() const { return ok(); }
};

// Only verify_manifest can mark a manifest verified. Package operations reject
// default-constructed or otherwise unverified instances.
class Manifest {
public:
    Manifest() = default;

    const std::string &version() const { return version_; }
    const std::string &url() const { return url_; }
    uint64_t size() const { return size_; }
    const std::string &sha256() const { return sha256_; }
    const std::string &signature() const { return signature_; }
    bool is_verified() const { return verified_; }

private:
    std::string version_;
    std::string url_;
    uint64_t size_ = 0;
    std::string sha256_;
    std::string signature_;
    bool verified_ = false;

    friend Result verify_manifest(std::string_view, const std::vector<uint8_t> &, Manifest &);
};

// The JSON object must contain exactly version, url, size, sha256 and signature.
// signature is base64-encoded OpenSSL ECDSA DER. public_key_blob is a CNG
// BCRYPT_ECCPUBLIC_BLOB for ECDSA P-256.
Result verify_manifest(std::string_view json, const std::vector<uint8_t> &public_key_blob, Manifest &out);

// Download to a new file, requiring HTTPS throughout redirects. The file is
// published at destination only after its exact signed size and SHA-256 match.
Result download_package(const Manifest &manifest, const std::wstring &destination);

// Local/offline package verification used after download and by the updater.
Result verify_package_file(const Manifest &manifest, const std::wstring &package_path);

// Validate and extract into a path that must not already exist. Only regular
// package files named TinyHand.exe, TinyHandUpdater.exe, LICENSE.txt, VERSION,
// or assets/hand_*.png (plus an optional assets/ directory) are accepted.
Result validate_and_extract_package(const Manifest &manifest, const std::wstring &package_path,
                                    const std::wstring &staging_directory);

// Move only validated package members from staging into an existing target.
Result install_staged_package(const Manifest &manifest, const std::wstring &staging_directory,
                              const std::wstring &target_directory);

} // namespace th::update
