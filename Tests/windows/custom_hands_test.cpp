#include "custom_hands.h"

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void test_ids_and_asset_paths() {
    check(custom_hands::valid_id(L"custom-0123abcd"), "canonical custom id is accepted");
    check(custom_hands::valid_id(L"custom-deadbeef"), "all-lowercase hex id is accepted");
    check(!custom_hands::valid_id(L"custom-0123ABCD"), "uppercase hex is rejected");
    check(!custom_hands::valid_id(L"custom-0123abc"), "short id is rejected");
    check(!custom_hands::valid_id(L"custom-0123abcde"), "long id is rejected");
    check(!custom_hands::valid_id(L"other-0123abcd"), "wrong prefix is rejected");
    check(!custom_hands::valid_id(L"custom-0123abcg"), "non-hex id is rejected");
    check(!custom_hands::valid_id(L"custom-../..\\x"), "path traversal is not a valid id");

    SetEnvironmentVariableW(L"TH_HANDS_DIR", L"C:\\TinyHandTests\\Hands");
    check(custom_hands::asset_path(L"custom-0123abcd") ==
              L"C:\\TinyHandTests\\Hands\\custom-0123abcd.png",
          "asset path stays in the hands directory");
    check(custom_hands::asset_path(L"custom-../escape").empty(),
          "traversal id cannot produce an asset path");
    check(custom_hands::asset_path(L"lavender").empty(), "built-in style cannot produce a custom asset path");
}

void test_prompt_and_suggested_names() {
    std::wstring standard = custom_hands::prompt(L"  \n");
    check(standard.find(L"a soft vinyl toy in a bright colour") != std::wstring::npos,
          "blank idea uses the default style");
    check(standard.find(L"[describe your hand") == std::wstring::npos, "prompt placeholder is replaced");
    check(standard.find(L"BACK of the hand") != std::wstring::npos, "prompt describes the back of the hand");
    check(standard.find(L"Transparent background") != std::wstring::npos, "prompt requires transparency");
    check(standard.find(L"No shadow") != std::wstring::npos, "prompt forbids shadows");

    std::wstring custom = custom_hands::prompt(L"  knitted wool in mustard yellow  ");
    check(custom.find(L"style: knitted wool in mustard yellow\n") != std::wstring::npos,
          "custom style is trimmed and inserted");

    check(custom_hands::suggested_name(L"C:\\imports\\mustard-wool_hand.png") == L"Mustard wool",
          "suggested name uses two cleaned filename words");
    check(custom_hands::suggested_name(L"/tmp/silver.PNG") == L"Silver", "first letter is capitalized");
    check(custom_hands::suggested_name(L"Chateau.png") == L"Chateau", "ordinary chat-prefixed names are preserved");
    check(custom_hands::suggested_name(L"ChatGPT Image 2026-01-01.png") == L"My hand 1",
          "generated image filename falls back to a numbered name");
    check(custom_hands::suggested_name(L"extraordinarily long_filename.png") == L"My hand 1",
          "overlong suggested name falls back");
    check(custom_hands::suggested_name(L".png") == L"My hand 1", "empty basename falls back");
}

} // namespace

int main() {
    test_ids_and_asset_paths();
    test_prompt_and_suggested_names();
    if (failures) {
        std::fprintf(stderr, "%d custom-hand check(s) failed\n", failures);
        return 1;
    }
    std::puts("All custom-hand checks passed");
    return 0;
}
