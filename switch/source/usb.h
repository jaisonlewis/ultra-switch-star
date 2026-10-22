#pragma once
/* USB Mass Storage access via libusbhsfs — mounted filesystems appear
   as devoptab volumes ("ums0:/", "ums1:/", ...). Used so a USB stick
   with an ultrapistar/ library works as a media root in docked mode
   (or via a USB-C OTG adapter in handheld). */
#include <string>
#include <vector>

namespace uss {

// init the UMS host interface (idempotent). Returns false if the
// service is unavailable (e.g. fsp-usb running under SX OS).
bool usbInit();
void usbExit();

// mount roots that currently exist — e.g. "ums0:/". Poll again after a
// status-change event or a short delay for hotplug.
std::vector<std::string> usbMounts();

// every mounted device's mount/<subdir> path that exists on disk —
// e.g. "ums0:/ultrapistar". Returns {} when nothing qualifies.
std::vector<std::string> usbLibraryRoots(const std::string& subdir);

} // namespace uss
