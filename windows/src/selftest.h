#pragma once

#include "common.h"

// Command-line checks that need no human: offscreen renders to PNG and an
// end-to-end run that drives real input through the mouse hook. They work
// the same on Windows and under Wine.
namespace selftest {

int render(const std::wstring &out);
int toolbar(const std::wstring &out);
int settings(HINSTANCE instance, const std::wstring &out);
int welcome(const std::wstring &out);
int settings_ui(HINSTANCE instance);
int e2e(HINSTANCE instance);

void print(const char *fmt, ...);

} // namespace selftest
