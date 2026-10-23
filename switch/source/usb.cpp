#include "usb.h"
#include <usbhsfs.h>
#include <dirent.h>
#include <vector>

namespace uss {

static bool g_usbOn = false;
static unsigned int g_rc = 0;

bool usbInit() {
    if (g_usbOn) return true;
    g_rc = usbHsFsInitialize(0);
    if (R_FAILED(g_rc)) return false;
    g_usbOn = true;
    return true;
}

unsigned int usbInitRc() { return g_rc; }

void usbExit() {
    if (g_usbOn) usbHsFsExit();
    g_usbOn = false;
}

std::vector<std::string> usbMounts() {
    std::vector<std::string> out;
    if (!g_usbOn) return out;
    UsbHsFsDevice devs[16];
    u32 n = usbHsFsListMountedDevices(devs, 16);
    for (u32 i = 0; i < n; ++i)
        out.push_back(std::string(devs[i].name) + "/");
    return out;
}

// physical = mass-storage devices the OS sees; mounted = filesystems
// that actually mounted. seen>0 & mounted==0 -> FS/partition problem;
// seen==0 -> the stick never enumerated at all (port/power/drive).
std::pair<unsigned, unsigned> usbCounts() {
    if (!g_usbOn) return {0, 0};
    return {usbHsFsGetPhysicalDeviceCount(),
            usbHsFsGetMountedDeviceCount()};
}

std::vector<std::string> usbLibraryRoots(const std::string& subdir) {
    std::vector<std::string> out;
    for (auto& m : usbMounts()) {
        std::string p = m + subdir;
        if (DIR* d = opendir(p.c_str())) {
            closedir(d);
            out.push_back(p);
        }
    }
    return out;
}

} // namespace uss
