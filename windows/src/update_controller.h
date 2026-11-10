#pragma once
#include "common.h"

namespace updates {
bool automatically_checks();
void set_automatically_checks(bool enabled);
}

class UpdateController {
public:
    void init(HWND appWindow);
    void check(bool manual);
    void shutdown();

private:
    HWND appWindow_ = nullptr;
    HANDLE worker_ = nullptr;
};
