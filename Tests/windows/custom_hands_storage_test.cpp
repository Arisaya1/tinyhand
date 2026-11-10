#include "custom_hands.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int failures = 0;
std::wstring root;
std::wstring directory;

void check(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void use_fresh_directory(const wchar_t *name) {
    std::filesystem::path path = std::filesystem::path(root) / name;
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    directory = path.wstring();
    SetEnvironmentVariableW(L"TH_HANDS_DIR", directory.c_str());
    std::filesystem::create_directories(path);
}

std::string metadata() {
    std::ifstream stream(std::filesystem::path(directory) / L"hands.json", std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void test_persistent_add_and_remove() {
    use_fresh_directory(L"persistence");
    check(custom_hands::all().empty(), "missing metadata starts with an empty list");

    Gdiplus::Bitmap image(64, 64, PixelFormat32bppPARGB);
    custom_hands::Hand first;
    std::wstring error = L"stale";
    check(custom_hands::add(&image, 0x123456, L"  12345678901234567890  ", first, &error),
          "a generated hand is persisted");
    if (first.id.empty()) std::fwprintf(stderr, L"add error: %ls\n", error.c_str());
    check(first.name == L"1234567890123456", "names are trimmed and limited to 16 characters");
    check(first.ink == 0x123456, "ink is persisted in the returned hand");
    check(custom_hands::valid_id(first.id), "generated ids use the canonical form");
    check(error.empty(), "success clears a stale error");
    check(std::filesystem::is_regular_file(custom_hands::asset_path(first.id)), "the hand PNG is written");
    check(metadata().find("\"ink\":1193046") != std::string::npos, "JSON metadata contains numeric ink");

    custom_hands::Hand second;
    check(custom_hands::add(&image, 0xABCDEF, L"\"A\\B\"\nC", second, &error),
          "a second hand requiring JSON escaping is persisted");
    check(second.id != first.id, "generated ids are unique");
    std::vector<custom_hands::Hand> loaded = custom_hands::all();
    check(loaded.size() == 2, "all reads both persisted hands");
    if (loaded.size() == 2) {
        check(loaded[0].id == first.id && loaded[0].name == first.name, "addition order and long name survive reload");
        check(loaded[1].name == L"\"A\\B\"\nC" && loaded[1].ink == 0xABCDEF,
              "JSON strings round-trip quotes, slashes, newlines, and ink");
    }
    check(metadata().find("\\n") != std::string::npos, "newlines are escaped in persistent JSON");
    check(custom_hands::suggested_name(L".png") == L"My hand 3", "numbered suggestions include saved hands");

    check(!custom_hands::remove(L"custom-../escape", &error), "path-like ids cannot be removed");
    check(!error.empty(), "invalid removal reports an error");
    check(std::filesystem::is_regular_file(custom_hands::asset_path(first.id)), "invalid removal cannot delete a confined asset");

    check(custom_hands::remove(first.id, &error), "a valid saved hand can be removed");
    check(!std::filesystem::exists(custom_hands::asset_path(first.id)), "removal deletes its PNG");
    loaded = custom_hands::all();
    check(loaded.size() == 1 && loaded.front().id == second.id, "removal is persisted in JSON");

    custom_hands::Hand reset;
    reset.id = L"stale";
    reset.name = L"stale";
    reset.ink = 99;
    check(!custom_hands::add(nullptr, 0, L"bad", reset, &error), "null images are rejected");
    check(reset.id.empty() && reset.name.empty() && reset.ink == 0, "failed add resets its output");
    check(!error.empty(), "failed add reports an error");
}

void test_unicode_and_atomic_storage() {
    use_fresh_directory(L"unicode");
    Gdiplus::Bitmap image(64, 64, PixelFormat32bppPARGB);
    custom_hands::Hand blank;
    std::wstring error;
    check(custom_hands::add(&image, 0x010203, L" \t\r\n ", blank, &error), "a blank-named hand is added");
    check(blank.name == L"My hand 1", "blank names use the persisted-list fallback");

    const std::wstring unicode_name = L"Caf\u00e9 \U0001F9AA";
    custom_hands::Hand unicode;
    check(custom_hands::add(&image, 0xA0B0C0, unicode_name, unicode, &error), "a Unicode-named hand is added");
    std::vector<custom_hands::Hand> loaded = custom_hands::all();
    check(loaded.size() == 2 && loaded[1].name == unicode_name,
          "UTF-8 metadata round-trips accented characters and surrogate-pair emoji");
    check(metadata().find("\xF0\x9F\xA6\xAA") != std::string::npos, "metadata stores emoji as valid UTF-8");

    const std::wstring sixteen_characters = L"123456789012345\U0001F9AA";
    custom_hands::Hand limited;
    check(custom_hands::add(&image, 0x111111, sixteen_characters + L" discarded", limited, &error),
          "a name at the Unicode character boundary is added");
    check(limited.name == sixteen_characters, "the 16-character limit counts a surrogate pair as one character");
    check(custom_hands::all().back().name == limited.name, "a name limited at an emoji survives metadata reload");

    bool temporary_files_left = false;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        std::wstring name = entry.path().filename().wstring();
        temporary_files_left = temporary_files_left || name.find(L".tmp") != std::wstring::npos;
    }
    check(!temporary_files_left, "atomic writes leave no temporary files behind");
}

void test_malformed_metadata_and_confined_paths() {
    use_fresh_directory(L"malformed-json");
    {
        std::ofstream stream(std::filesystem::path(directory) / L"hands.json", std::ios::binary);
        stream << "not json";
    }
    check(custom_hands::all().empty(), "malformed JSON is ignored safely");

    {
        std::ofstream stream(std::filesystem::path(directory) / L"hands.json", std::ios::binary);
        stream << "[{\"id\":\"custom-../escape\",\"name\":\"bad\",\"ink\":1},"
                  "{\"id\":\"custom-0123ABCD\",\"name\":\"bad\",\"ink\":2},"
                  "{\"id\":\"custom-0123abcd\",\"name\":123,\"ink\":3},"
                  "{\"id\":\"custom-deadbeef\",\"name\":\"fine\",\"ink\":4294967295},"
                  "{\"id\":\"custom-aaaaaaaa\",\"name\":\"negative\",\"ink\":-1}]";
    }
    std::vector<custom_hands::Hand> loaded = custom_hands::all();
    check(loaded.size() == 1 && loaded.front().id == L"custom-deadbeef" &&
              loaded.front().name == L"fine" && loaded.front().ink == 0xFFFFFFFFu,
          "metadata entries with invalid ids, types, and ink are discarded");
}

} // namespace

int main() {
    Gdiplus::GdiplusStartupInput gdiplus_input;
    ULONG_PTR gdiplus_token = 0;
    Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);

    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    root = (std::filesystem::path(temp) /
            (L"TinyHandCustomHandsStorageTests-" + std::to_wstring(GetCurrentProcessId()))).wstring();

    test_persistent_add_and_remove();
    test_unicode_and_atomic_storage();
    test_malformed_metadata_and_confined_paths();

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    Gdiplus::GdiplusShutdown(gdiplus_token);
    if (failures) {
        std::fprintf(stderr, "%d custom-hand storage check(s) failed\n", failures);
        return 1;
    }
    std::puts("All custom-hand storage checks passed");
    return 0;
}
