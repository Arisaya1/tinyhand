#include "update.h"

#include <windows.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace {

using th::update::Error;
using th::update::Result;

Result failure(Error code, const wchar_t *message) {
    return {code, message};
}

Result read_file(const wchar_t *path, uint64_t limit, std::vector<uint8_t> &data) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return failure(Error::io, L"could not open updater input");
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && static_cast<uint64_t>(size.QuadPart) <= limit;
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
        const DWORD wanted = static_cast<DWORD>(data.size() - offset);
        ok = ReadFile(file, data.data() + offset, wanted, &read, nullptr) && read != 0;
        offset += read;
    }
    CloseHandle(file);
    return ok && offset == data.size() ? Result{} : failure(Error::io, L"updater input is missing, empty, or too large");
}

Result wait_for_process(const wchar_t *text) {
    if (!text || !*text || *text == L'-') {
        return failure(Error::invalid_arguments, L"wait PID is invalid");
    }
    errno = 0;
    wchar_t *end = nullptr;
    const unsigned long value = wcstoul(text, &end, 10);
    if (errno || !end || *end || value == 0 || value > MAXDWORD) {
        return failure(Error::invalid_arguments, L"wait PID is invalid");
    }
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(value));
    if (!process) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) {
            return {}; // The requested process has already exited.
        }
        return failure(Error::invalid_target, L"could not open the application process for waiting");
    }
    const DWORD status = WaitForSingleObject(process, INFINITE);
    CloseHandle(process);
    return status == WAIT_OBJECT_0 ? Result{} : failure(Error::invalid_target, L"waiting for application exit failed");
}

Result validate_target(const wchar_t *text, std::wstring &target) {
    if (!text || !*text || wcschr(text, L'\"')) {
        return failure(Error::invalid_arguments, L"target directory is invalid");
    }
    const DWORD needed = GetFullPathNameW(text, 0, nullptr, nullptr);
    if (!needed) {
        return failure(Error::invalid_target, L"target directory cannot be resolved");
    }
    target.resize(needed);
    if (!GetFullPathNameW(text, needed, target.data(), nullptr)) {
        return failure(Error::invalid_target, L"target directory cannot be resolved");
    }
    target.resize(needed - 1);
    while (target.size() > 3 && target.back() == L'\\') {
        target.pop_back();
    }
    const DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return failure(Error::invalid_target, L"target must be an existing ordinary directory");
    }
    return {};
}

Result relaunch(const std::wstring &target) {
    const std::wstring executable = target + L"\\TinyHand.exe";
    const DWORD attributes = GetFileAttributesW(executable.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return failure(Error::relaunch_failed, L"installed TinyHand.exe is unavailable");
    }
    std::wstring command = L"\"" + executable + L"\"";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_DEFAULT_ERROR_MODE | CREATE_UNICODE_ENVIRONMENT, nullptr, target.c_str(), &startup, &process)) {
        return failure(Error::relaunch_failed, L"could not relaunch installed TinyHand.exe");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {};
}

int report(const Result &result) {
    if (!result) {
        std::fwprintf(stderr, L"TinyHandUpdater: %ls\n", result.message.c_str());
        return 10 + static_cast<int>(result.code);
    }
    return 0;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc != 6) {
        std::fwprintf(stderr,
                      L"Usage: TinyHandUpdater.exe <wait-pid> <manifest.json> <public-key.blob> <package.zip> "
                      L"<target-directory>\n");
        return 2;
    }
    Result result = wait_for_process(argv[1]);
    if (!result) {
        return report(result);
    }
    std::wstring target;
    result = validate_target(argv[5], target);
    if (!result) {
        return report(result);
    }
    std::vector<uint8_t> manifest_bytes;
    std::vector<uint8_t> public_key;
    result = read_file(argv[2], 1024 * 1024, manifest_bytes);
    if (result) {
        result = read_file(argv[3], 4096, public_key);
    }
    th::update::Manifest manifest;
    if (result) {
        const std::string json(reinterpret_cast<const char *>(manifest_bytes.data()), manifest_bytes.size());
        result = th::update::verify_manifest(json, public_key, manifest);
    }
    // Keep all updater-owned staging names fresh across retries and processes.
    std::wstring staging;
    if (result) {
        staging = target + L".tinyhand-staging-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                  std::to_wstring(GetTickCount64());
        result = th::update::validate_and_extract_package(manifest, argv[4], staging);
    }
    if (result) {
        result = th::update::install_staged_package(manifest, staging, target);
    }
    if (!result) {
        return report(result);
    }
    RemoveDirectoryW((staging + L"\\assets").c_str());
    RemoveDirectoryW(staging.c_str());
    result = relaunch(target);
    return report(result);
}
