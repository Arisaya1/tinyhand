#include "custom_hands.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int failures = 0;
std::filesystem::path root;

void check(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

bool save_png(Gdiplus::Bitmap &image, const std::filesystem::path &path) {
    UINT count = 0, bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || !bytes) return false;
    std::vector<uint8_t> buffer(bytes);
    auto *codecs = reinterpret_cast<Gdiplus::ImageCodecInfo *>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            return image.Save(path.wstring().c_str(), &codecs[i].Clsid, nullptr) == Gdiplus::Ok;
        }
    }
    return false;
}

std::unique_ptr<Gdiplus::Bitmap> valid_hand() {
    auto image = std::make_unique<Gdiplus::Bitmap>(200, 320, PixelFormat32bppPARGB);
    Gdiplus::Graphics graphics(image.get());
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    Gdiplus::SolidBrush orange(Gdiplus::Color(255, 210, 120, 30));
    graphics.FillRectangle(&orange, 65, 20, 40, 130);
    graphics.FillEllipse(&orange, 20, 105, 150, 155);
    graphics.FillRectangle(&orange, 35, 155, 120, 90);
    graphics.FillRectangle(&orange, 45, 235, 110, 65);
    return image;
}

void expect_reject(const std::filesystem::path &path, const wchar_t *reason, const char *message) {
    custom_hands::MadeHand made;
    made.image = valid_hand();
    made.ink = 99;
    std::wstring error = L"stale";
    check(!custom_hands::make_from_file(path.wstring(), made, &error), message);
    check(!made.image && made.ink == 0, "failed import resets its complete output");
    if (error.find(reason) == std::wstring::npos) {
        std::fwprintf(stderr, L"unexpected error for %ls: %ls\n", path.wstring().c_str(), error.c_str());
    }
    check(error.find(reason) != std::wstring::npos, "failed import reports the relevant reason");
}

void expect_reject_image(Gdiplus::Bitmap &image, const wchar_t *name, const wchar_t *reason,
                         const char *message) {
    std::filesystem::path path = root / name;
    check(save_png(image, path), "a rejection fixture is saved as a real PNG");
    expect_reject(path, reason, message);
}

void test_malformed_empty_and_size_limits() {
    expect_reject(root / L"missing.png", L"isn't an image", "a missing source path is rejected");
    expect_reject(root, L"isn't an image", "a directory source path is rejected");

    std::ofstream(root / L"malformed.png", std::ios::binary) << "this is not a PNG";
    expect_reject(root / L"malformed.png", L"isn't an image", "malformed image bytes are rejected");
    std::ofstream(root / L"zero.png", std::ios::binary);
    expect_reject(root / L"zero.png", L"isn't an image", "an empty image file is rejected");

    {
        std::ofstream huge(root / L"huge.png", std::ios::binary | std::ios::trunc);
        huge.seekp(std::streamoff(32ll * 1024 * 1024));
        huge.put(0);
    }
    check(std::filesystem::file_size(root / L"huge.png") == 32ull * 1024 * 1024 + 1,
          "the oversized fixture really exceeds 32 MiB");
    expect_reject(root / L"huge.png", L"32 MiB", "a source file over 32 MiB is rejected");

    Gdiplus::Bitmap tiny(64, 63, PixelFormat32bppPARGB);
    expect_reject_image(tiny, L"tiny.png", L"too small", "images smaller than 64 pixels are rejected");
    Gdiplus::Bitmap wide(4097, 64, PixelFormat32bppPARGB);
    expect_reject_image(wide, L"wide.png", L"too large", "images wider than 4096 pixels are rejected");
}

void test_transparency_and_shape_validation() {
    Gdiplus::Bitmap opaque(200, 320, PixelFormat32bppRGB);
    expect_reject_image(opaque, L"opaque.png", L"background isn't transparent", "opaque images are rejected");

    Gdiplus::Bitmap empty(200, 320, PixelFormat32bppPARGB);
    expect_reject_image(empty, L"empty.png", L"empty", "fully transparent images are rejected as empty");

    auto touching = valid_hand();
    {
        Gdiplus::Graphics graphics(touching.get());
        Gdiplus::SolidBrush ink(Gdiplus::Color(255, 210, 120, 30));
        graphics.FillRectangle(&ink, 0, 100, 4, 100);
    }
    expect_reject_image(*touching, L"touching.png", L"touches the edge", "a hand touching the image edge is rejected");

    auto multiple = valid_hand();
    {
        Gdiplus::Graphics graphics(multiple.get());
        Gdiplus::SolidBrush ink(Gdiplus::Color(255, 30, 80, 180));
        graphics.FillRectangle(&ink, 165, 20, 30, 30);
    }
    expect_reject_image(*multiple, L"multiple.png", L"more than one thing", "disconnected extra objects are rejected");

    Gdiplus::Bitmap sideways(200, 320, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics graphics(&sideways);
        Gdiplus::SolidBrush ink(Gdiplus::Color(255, 210, 120, 30));
        graphics.FillRectangle(&ink, 15, 150, 170, 100);
    }
    expect_reject_image(sideways, L"sideways.png", L"upright", "wide sideways artwork is rejected");

    Gdiplus::Bitmap undersized(200, 640, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics graphics(&undersized);
        graphics.DrawImage(valid_hand().get(), 0, 160, 200, 320);
    }
    expect_reject_image(undersized, L"undersized.png", L"Crop it closer", "artwork occupying under half the image height is rejected");

    auto forked = valid_hand();
    {
        Gdiplus::Graphics graphics(forked.get());
        Gdiplus::SolidBrush ink(Gdiplus::Color(255, 210, 120, 30));
        graphics.FillRectangle(&ink, 110, 20, 40, 130);
    }
    expect_reject_image(*forked, L"forked.png", L"single finger", "a broad or multiple-finger top is rejected");

    auto pointed_wrist = valid_hand();
    {
        Gdiplus::Graphics graphics(pointed_wrist.get());
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        Gdiplus::SolidBrush clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.FillRectangle(&clear, 45, 235, 110, 65);
    }
    expect_reject_image(*pointed_wrist, L"pointed-wrist.png", L"flat wrist", "a wrist narrower than the hand is rejected");

    Gdiplus::Bitmap shadow(200, 320, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics graphics(&shadow);
        Gdiplus::SolidBrush glow(Gdiplus::Color(100, 0, 0, 0));
        graphics.FillRectangle(&glow, 10, 10, 180, 300);
        graphics.DrawImage(valid_hand().get(), 0, 0, 200, 320);
    }
    expect_reject_image(shadow, L"shadow.png", L"soft shadow", "large translucent shadows and glows are rejected");
}

void test_generated_hand_is_composed() {
    std::filesystem::create_directories(root);
    std::filesystem::path source = root / L"valid.png";
    check(save_png(*valid_hand(), source), "the real transparent PNG fixture is saved");

    custom_hands::MadeHand made;
    made.ink = 123;
    std::wstring error = L"stale";
    check(custom_hands::make_from_file(source.wstring(), made, &error), "a generated valid hand is imported");
    if (made.image) {
        check(made.image->GetWidth() == 1024 && made.image->GetHeight() == 1536,
              "the composed pointer has the required dimensions");
        Gdiplus::Color pixel;
        made.image->GetPixel(0, 0, &pixel);
        check(pixel.GetA() == 0, "the composed canvas stays transparent at its corner");
        made.image->GetPixel(512, 1000, &pixel);
        check(pixel.GetA() == 255 && pixel.GetR() < 100 && pixel.GetG() < 100 && pixel.GetB() < 100,
              "the wand has an opaque dark handle below the hand");
        made.image->GetPixel(470, 170, &pixel);
        check(pixel.GetA() == 255 && pixel.GetR() > 150 && pixel.GetG() > 60,
              "the original hand artwork is drawn into the composition");
    }
    check((made.ink >> 16) > ((made.ink >> 8) & 0xFF) && ((made.ink >> 8) & 0xFF) > (made.ink & 0xFF),
          "ink is derived from the dominant hand colour");
    check(error.empty(), "successful import clears a stale error");
}

} // namespace

int main() {
    Gdiplus::GdiplusStartupInput gdiplus_input;
    ULONG_PTR gdiplus_token = 0;
    Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    root = std::filesystem::path(temp) /
           (L"TinyHandCustomHandsImageTests-" + std::to_wstring(GetCurrentProcessId()));

    std::filesystem::create_directories(root);
    test_malformed_empty_and_size_limits();
    test_transparency_and_shape_validation();
    test_generated_hand_is_composed();

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    Gdiplus::GdiplusShutdown(gdiplus_token);
    if (failures) {
        std::fprintf(stderr, "%d custom-hand image check(s) failed\n", failures);
        return 1;
    }
    std::puts("All custom-hand image checks passed");
    return 0;
}
