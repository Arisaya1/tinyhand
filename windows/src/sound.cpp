#include "sound.h"

#include "prefs.h"

#include <mmsystem.h>

static void play_id(const std::wstring &id) {
    for (const auto &t : kTapSounds) {
        if (id == t.id) {
            DWORD size = 0;
            const void *wav = th::resource_bytes(t.resource, &size);
            if (wav) {
                PlaySoundW(static_cast<LPCWSTR>(wav), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
            }
            return;
        }
    }
}

void TapSound::play() const {
    if (enabled) {
        play_id(prefs::get().tapSound);
    }
}

void TapSound::preview(const std::wstring &id) { play_id(id); }
