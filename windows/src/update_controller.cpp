#include "update_controller.h"
#include "update.h"
#include "update_key.h"
#include "version.h"
#include <winhttp.h>

namespace {
constexpr wchar_t kFeedPath[] = L"/Arisaya1/tinyhand/releases/latest/download/windows-update.json";

struct Http {
    HINTERNET session = nullptr, connection = nullptr, request = nullptr;
    ~Http() {
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
    }
};

bool download_feed(std::string &out) {
    Http http;
    http.session = WinHttpOpen(L"TinyHand-Update/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!http.session) return false;
    WinHttpSetTimeouts(http.session, 15000, 15000, 15000, 15000);
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(http.session, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    http.connection = WinHttpConnect(http.session, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!http.connection) return false;
    http.request = WinHttpOpenRequest(http.connection, L"GET", kFeedPath, nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!http.request ||
        !WinHttpSendRequest(http.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(http.request, nullptr)) return false;
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(http.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200) return false;
    char buffer[8192];
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(http.request, buffer, sizeof(buffer), &read)) return false;
        if (!read) break;
        if (out.size() + read > 65536) return false;
        out.append(buffer, read);
    }
    return true;
}

bool newer(const std::string &candidate) {
    unsigned a[3]{}, b[3]{};
    char tail = 0;
    if (sscanf_s(candidate.c_str(), "%u.%u.%u%c", &a[0], &a[1], &a[2], &tail, 1) != 3 ||
        sscanf_s(TH_VERSION_A, "%u.%u.%u%c", &b[0], &b[1], &b[2], &tail, 1) != 3) return false;
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

std::wstring quote(const std::wstring &s) {
    std::wstring out = L"\"";
    for (wchar_t c : s) {
        if (c == L'\"') out += L'\\';
        out += c;
    }
    return out + L"\"";
}

bool write_new(const std::wstring &path, const void *data, size_t size) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, data, DWORD(size), &written, nullptr) && written == size && FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}

struct Job {
    HWND window;
    bool manual;
};

void failure(const Job &job, const std::wstring &message) {
    th::log(L"update: %ls", message.c_str());
    if (job.manual) {
        MessageBoxW(job.window, message.c_str(), L"TinyHand updates", MB_OK | MB_ICONWARNING);
    }
}

DWORD WINAPI check_worker(LPVOID parameter) {
    std::unique_ptr<Job> job(static_cast<Job *>(parameter));
    std::string json;
    if (!download_feed(json)) {
        failure(*job, L"TinyHand couldn't reach the update feed. Check your connection and try again.");
        return 0;
    }
    th::update::Manifest manifest;
    th::update::Result result = th::update::verify_manifest(json, update_public_key(), manifest);
    if (!result) {
        failure(*job, result.message);
        return 0;
    }
    if (!newer(manifest.version())) {
        if (job->manual) {
            MessageBoxW(job->window, L"You're already using the latest Windows version.", L"TinyHand updates",
                        MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }
    std::wstring message = L"Version " + std::wstring(manifest.version().begin(), manifest.version().end()) +
                           L" is available. Download and install it now? TinyHand will close and reopen.";
    if (MessageBoxW(job->window, message.c_str(), L"TinyHand update available",
                    MB_YESNO | MB_ICONINFORMATION) != IDYES) return 0;

    std::wstring root = th::data_dir() + L"\\Updates";
    CreateDirectoryW(root.c_str(), nullptr);
    std::wstring directory = root + L"\\" + std::wstring(manifest.version().begin(), manifest.version().end()) +
                             L"-" + std::to_wstring(GetCurrentProcessId());
    if (!CreateDirectoryW(directory.c_str(), nullptr)) {
        failure(*job, L"TinyHand couldn't create a private update folder.");
        return 0;
    }
    std::wstring package = directory + L"\\package.zip";
    result = th::update::download_package(manifest, package);
    if (!result) {
        failure(*job, result.message);
        return 0;
    }
    std::wstring manifestPath = directory + L"\\manifest.json";
    std::wstring keyPath = directory + L"\\public-key.blob";
    std::wstring updater = directory + L"\\TinyHandUpdater.exe";
    std::vector<uint8_t> key = update_public_key();
    if (!write_new(manifestPath, json.data(), json.size()) || !write_new(keyPath, key.data(), key.size()) ||
        !CopyFileW((th::exe_dir() + L"\\TinyHandUpdater.exe").c_str(), updater.c_str(), TRUE)) {
        failure(*job, L"TinyHand couldn't prepare the verified update.");
        return 0;
    }
    std::wstring command = quote(updater) + L" " + std::to_wstring(GetCurrentProcessId()) + L" " +
                           quote(manifestPath) + L" " + quote(keyPath) + L" " + quote(package) + L" " + quote(th::exe_dir());
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(updater.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, directory.c_str(), &startup, &process)) {
        failure(*job, L"TinyHand couldn't start its updater. Your current version is unchanged.");
        return 0;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    PostMessageW(job->window, WM_TH_UPDATE_INSTALL, 0, 0);
    return 0;
}
}

namespace updates {
bool automatically_checks() {
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\TinyHand", L"automaticUpdates", RRF_RT_REG_DWORD,
                 nullptr, &value, &size);
    return value != 0;
}

void set_automatically_checks(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\TinyHand", 0, nullptr, 0, KEY_WRITE,
                        nullptr, &key, nullptr) == ERROR_SUCCESS) {
        DWORD value = enabled ? 1 : 0;
        RegSetValueExW(key, L"automaticUpdates", 0, REG_DWORD, reinterpret_cast<BYTE *>(&value), sizeof(value));
        RegCloseKey(key);
    }
}
}

void UpdateController::init(HWND appWindow) {
    appWindow_ = appWindow;
}

void UpdateController::check(bool manual) {
    if (worker_) {
        if (WaitForSingleObject(worker_, 0) == WAIT_TIMEOUT) {
            if (manual) MessageBoxW(appWindow_, L"An update check is already running.", L"TinyHand updates", MB_OK);
            return;
        }
        CloseHandle(worker_);
        worker_ = nullptr;
    }
    auto job = std::make_unique<Job>();
    job->window = appWindow_;
    job->manual = manual;
    worker_ = CreateThread(nullptr, 0, check_worker, job.get(), 0, nullptr);
    if (worker_) {
        job.release();
    } else if (manual) {
        MessageBoxW(appWindow_, L"TinyHand couldn't start an update check.", L"TinyHand updates", MB_OK | MB_ICONWARNING);
    }
}

void UpdateController::shutdown() {
    if (worker_) CloseHandle(worker_);
    worker_ = nullptr;
}
