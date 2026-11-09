#pragma once

#include "prefs.h"

// Settings, laid out like the macOS Settings window: a sidebar of sections
// (General, Pointers, Appearance, Drawing, Sound, About), each page made of
// titled cards with rows, switches, menus and swatches. Everything is drawn
// with GDI+ so it looks the same on every Windows version and under Wine,
// follows the light or dark app theme, and applies changes at once.
class SettingsWindow {
public:
    enum Section { General, Pointers, Appearance, Drawing, Sound, About, SectionCount };

    // Returns false when the shortcut could not be registered.
    std::function<bool(const Hotkey &, bool isToggle)> applyHotkey;
    // True while a shortcut is being recorded, so global hotkeys step aside.
    std::function<void(bool)> onRecording;

    void show(HINSTANCE instance, int section = -1);
    // Draws one page offscreen at 1x, for --settings-test.
    bool snapshot(const std::wstring &path, int section, bool dark, bool picker = false);
    HWND hwnd() const { return hwnd_; }
    int section() const { return section_; }
    // For --settings-ui-test: where the widget labelled `label` is (a sidebar
    // row, sound tile, pointer id, "+"), or with control=true the switch,
    // slider or menu on the row titled `label`. Client pixels.
    bool locate(const std::wstring &label, bool control, RECT *out) const;

private:
    enum class Kind {
        Sidebar,
        Text,
        Card,
        Separator,
        Switch,
        Popup,
        Slider,
        Recorder,
        Swatch,
        SoundTile,
        SlotRow,
        Slot,
        AddSlot,
        EmptySlot,
        TextButton,
        LinkRow,
        Picker,
        PickerCell,
        AppIcon,
        Glyph,
    };
    enum Font { Body, BodyBold, Detail, Header, Title, Name, Caption, Medium, Key, FontCount };

    struct Widget {
        Kind kind = Kind::Text;
        Gdiplus::RectF r;
        int id = 0;
        int font = Body;
        bool secondary = false;
        bool error = false;
        bool right = false;
        bool on = false;
        bool enabled = true;
        bool selected = false;
        float value = 0;
        std::wstring text;
        std::wstring detail;
        std::vector<std::wstring> options;
        std::function<void()> click;
        std::function<void(int)> pick;
        std::function<void(float)> slide;
    };

    struct Palette {
        uint32_t window, sidebar, text, secondary, accent, control, popover;
        bool dark;
        Gdiplus::Color ink(float alpha) const;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT proc(UINT msg, WPARAM wp, LPARAM lp);
    void create();
    void resize();
    void build(Gdiplus::Graphics &g, float width, float height);
    void paint(Gdiplus::Graphics &g, float width, float height);
    void draw(Gdiplus::Graphics &g, const Widget &w, int index);
    void drawGlyph(Gdiplus::Graphics &g, int glyph, const Gdiplus::RectF &r, Gdiplus::Color c);
    void drawShortcut(Gdiplus::Graphics &g, const Widget &w);
    void redraw();

    float textHeight(Gdiplus::Graphics &g, const std::wstring &text, int font, float width);
    float textWidth(Gdiplus::Graphics &g, const std::wstring &text, int font);
    Widget &add(Kind kind, Gdiplus::RectF r);
    Widget &text(Gdiplus::Graphics &g, const std::wstring &s, int font, float x, float y, float width,
                 bool secondary = false);
    float card(Gdiplus::Graphics &g, float y, const wchar_t *title, std::vector<std::function<float(float)>> rows);
    float row(Gdiplus::Graphics &g, float y, const std::wstring &title, const std::wstring &detail, Kind trailing,
              float trailW, float trailH, std::function<void(Widget &)> setup);

    void pageGeneral(Gdiplus::Graphics &g, float y);
    void pagePointers(Gdiplus::Graphics &g, float y);
    void pageAppearance(Gdiplus::Graphics &g, float y);
    void pageDrawing(Gdiplus::Graphics &g, float y);
    void pageSound(Gdiplus::Graphics &g, float y);
    void pageAbout(Gdiplus::Graphics &g, float y);

    int hit(float x, float y) const;
    void mouseDown(float x, float y);
    void mouseMove(float x, float y);
    void mouseUp(float x, float y);
    void startRecording(int which);
    void stopRecording();
    bool recordKey(UINT vk);
    void commitDrag();
    void preloadThumbnails();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    th::Surface surface_;
    float scale_ = 1;
    Palette pal_{};
    std::unique_ptr<Gdiplus::FontFamily> family_;
    std::unique_ptr<Gdiplus::FontFamily> semibold_;
    std::unique_ptr<Gdiplus::Font> fonts_[FontCount];
    float fontScale_ = 0;
    bool listening_ = false;

    std::vector<Widget> widgets_;
    int section_ = General;
    int hover_ = -1;
    int pressed_ = -1;
    int sliding_ = -1;
    int recording_ = -1;
    std::wstring shortcutError_;
    std::wstring pinMessage_;
    bool picking_ = false;
    bool dim_ = false;
    int forceTheme_ = -1;
    bool leftDown_ = false;

    // Toolbar row drag (reorder, or drag out to remove).
    int dragIndex_ = -1;
    bool dragging_ = false;
    bool dragOut_ = false;
    float dragStartX_ = 0;
    float dragStartY_ = 0;
    float dragX_ = 0;
    float dragY_ = 0;
    std::vector<std::wstring> draft_;
    Gdiplus::RectF slotRow_;
    float slotStep_ = 0;
};
