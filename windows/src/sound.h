#pragma once

#include "common.h"

// Tap sounds are embedded WAVs played straight from the resource section.
class TapSound {
public:
    bool enabled = true;
    void play() const;
    static void preview(const std::wstring &id);
};
