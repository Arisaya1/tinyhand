#pragma once
#include "common.h"

namespace custom_hand_ui {
bool import_file(HWND owner, const std::wstring &path);
void choose_file(HWND owner);
void copy_prompt();
void open_chatgpt();
void remove_current(HWND owner);
void install_drag_drop(HWND hwnd);
void handle_drop_files(HWND owner, HDROP drop);
}
