#include "usb.h"
#include <usbhsfs.h>
#include <switch.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <vector>

namespace uss {

static bool g_usbOn = false;
static unsigned int g_rc = 0;
static FILE* g_log = nullptr;

// first line of defence: opened at main() entry, so even a hang inside
// fb/usb init leaves a trail. Never logs secrets — diagnostics only.
void diag(const char* fmt, ...) {
    if (!g_log)
        g_log = fopen("sdmc:/ultraswitchstar.log", "a");
    if (!g_log) return;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(g_log, "[%6ld.%03ld] ", ts.tv_sec, ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);
    fflush(g_log);
    va_end(ap);
}

bool usbInit() {
    if (g_usbOn) return true;
    diag("hos %u.%u.%u — usbHsFsInitialize...",
         HOSVER_MAJOR(hosversionGet()),
         HOSVER_MINOR(hosversionGet()),
         HOSVER_MICRO(hosversionGet()));
    g_rc = usbHsFsInitialize(0);
    diag("usbHsFsInitialize rc=0x%x", g_rc);
    if (R_FAILED(g_rc)) return false;
    g_usbOn = true;
    return true;
}

unsigned int usbInitRc() { return g_rc; }

void usbExit() {
    if (g_usbOn) usbHsFsExit();
    g_usbOn = false;
    if (g_log) { fclose(g_log); g_log = nullptr; }
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

// one diagnostic dump to the SD log — call when the counts change
void usbLogDetail() {
    auto [seen, mnt] = usbCounts();
    diag("usb state: %u physical, %u mounted", seen, mnt);
    if (!mnt) return;
    UsbHsFsDevice devs[16];
    u32 n = usbHsFsListMountedDevices(devs, 16);
    for (u32 i = 0; i < n; ++i)
        diag("  %s vid=%04x pid=%04x fs=%u cap=%lluMB %s %s",
             devs[i].name, devs[i].vid, devs[i].pid, devs[i].fs_type,
             (unsigned long long)(devs[i].capacity >> 20),
             devs[i].manufacturer, devs[i].product_name);
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
