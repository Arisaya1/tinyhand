# TinyHand for Windows

A native Win32 port of TinyHand: the same tiny hand on a stick, the same 36 hands and laser dot,
the same drawing and tap sounds, now on Windows 10 and 11 (x64).

It is a single static `TinyHand.exe` (no installer, no runtime to install) plus the `assets` folder
with the hand pictures.

## Use

| | |
|---|---|
| Click the tray icon, or **Ctrl+Alt+H** | Turn the pointer on or off |
| Right-click the tray icon | Menu and Settings |
| Click | Tap (nothing underneath gets clicked) |
| Hold and drag | Draw on the screen |
| Draw a rough shape | It snaps into a clean line, arrow, circle, ellipse, triangle, square, rectangle, diamond, pentagon, hexagon, star or heart |
| Draw a small **V** at the end of a line | The line becomes an arrow |
| Hold **Alt** and click | Click for real, through the hand |
| **Ctrl+Alt+X** | Clear the drawing |
| **Esc** | Clear everything and turn the pointer off |

The taskbar and open menus always get real clicks, and the real cursor comes back while you are over them.

**Settings** (right-click the tray icon) has the same pages as on the Mac: General (shortcuts, the
click-through key, open at login), Pointers (which hands sit in the toolbar: drag to reorder, drag out or
press x to remove, + to add), Appearance (size, shadow), Drawing (ink colour, fade, shape snapping, ink
button), Sound and About. It follows the Windows light or dark app theme. On first run a small welcome
card shows the shortcut.

Unlike macOS, Windows needs no special permission for TinyHand to turn clicks into taps.
Settings live in `HKEY_CURRENT_USER\Software\TinyHand`; the log is `%LOCALAPPDATA%\TinyHand\TinyHand.log`.

## Build

From macOS or Linux, with [mingw-w64](https://www.mingw-w64.org/) (`brew install mingw-w64` or
`sudo apt install mingw-w64`):

```bash
make -C windows            # windows/build/TinyHand.exe
make -C windows dist       # windows/dist/TinyHand-<version>-windows-x64.zip
```

On Windows, in an [MSYS2](https://www.msys2.org/) MinGW64 shell (`pacman -S mingw-w64-x86_64-gcc make zip`):

```bash
make -C windows CROSS=
```

`windows/build/TinyHand.exe` finds the hand pictures in the repository's `assets` folder, so it runs
straight from the build folder.

## Checks

The exe has the same kind of self-test switches as the macOS app. They work on Windows and under Wine:

| | |
|---|---|
| `TinyHand.exe --render-test out.png` | Draws every shape through the snapper and all 37 pointers into one picture; exit code is the number of misses |
| `TinyHand.exe --toolbar-test out.png` | Renders the toolbar, the first-run hint and the ink tray |
| `TinyHand.exe --settings-test out.png` | Renders every Settings page (plus the pointer picker and dark mode) to `out-<page>.png` |
| `TinyHand.exe --settings-ui-test` | Clicks and drags through Settings with window messages and checks each control changes its setting. The real mouse does not move |
| `TinyHand.exe --welcome-test out.png` | Renders the first-run card, light and dark |
| `TinyHand.exe --e2e-test` | Drives real mouse and keyboard input through the hook: taps, drawing and snapping, two-stroke arrows, Alt-click, shortcuts and Esc. It moves the mouse for a few seconds |
| `make -C windows test` | Builds `shape_test.exe`, the shared shape recognizer test |

To try it on a Mac with [Wine](https://www.winehq.org/) (64-bit Wine 9 or later):

```bash
make -C windows all test
wine windows/build/shape_test.exe
wine windows/build/TinyHand.exe --render-test /tmp/tinyhand-render.png
wine windows/build/TinyHand.exe --e2e-test
wine windows/build/TinyHand.exe
```

## How it maps to the macOS app

| macOS | Windows |
|---|---|
| Menu bar item | Tray icon (white or black to match the taskbar) |
| `CGEvent` tap on its own thread | `WH_MOUSE_LL` hook on its own thread |
| Transparent `NSWindow` with Core Animation layers | Per-pixel-alpha layered window, GDI+ drawing, dirty-rect updates |
| `CGDisplayHideCursor` | System cursors swapped for a blank one, restored on exit and after a crash |
| Carbon hot keys | `RegisterHotKey` |
| Option to click for real | Alt (or Ctrl, Shift, off) |
| Menu bar and Dock pass-through | Taskbar, tray overflow, Start and open menus pass through |
| `UserDefaults` | Registry, `HKCU\Software\TinyHand` |
| Open at login | `HKCU\...\CurrentVersion\Run` |

Shape snapping is shared: both apps compile the same C recognizer in `Sources/ShapeKit`.

Not ported yet: making your own hand from a picture, and automatic updates.
