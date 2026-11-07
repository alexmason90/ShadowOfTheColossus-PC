// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_settings.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
int main() {
    char path[]="/tmp/sotc-settings-XXXXXX";
    const int fd=mkstemp(path); assert(fd>=0); close(fd);
    {
        std::ofstream f(path);
        f<<"SOTC_GAME_DIR=/games/My Dump\nSOTC_FPS_CAP=off\nSOTC_FRAMEGEN=fsr\nBB_VBLANK_HZ=120\nkey.cross=Space\npad.cross=a\nupscaler=off\n";
    }
    setenv("BB_CONFIG",path,1);
    (void)BbSettings::Get();
    BbSettings::Save();
    std::ifstream f(path);
    std::string text{std::istreambuf_iterator<char>{f},{}};
    for (const char* value : {"SOTC_GAME_DIR=/games/My Dump", "SOTC_FPS_CAP=off", "SOTC_FRAMEGEN=fsr", "BB_VBLANK_HZ=120", "key.cross=Space", "pad.cross=a"})
        assert(text.find(value)!=std::string::npos);
    unlink(path);
}
