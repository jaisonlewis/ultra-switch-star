#pragma once
/* Catalog scanner — discovers the prepared Ultra Pi Star media layout on
   the SD card:
     sdmc:/ultrapistar/
       manifest.json            (station bundle index, optional)
       <Artist - Title>/        (one folder per song)
         yt-<id>.mp4            media
         yt-<id>.inst.mp4       karaoke backing
         yt-<id>.analysis.json  pitch-curve sidecar (unused on device)
       charts — *.upsong        (station flat-chart fallback)
   Folder-scan fallback: when no manifest exists, any media file under a
   named subdirectory counts; charts resolve by stem anywhere under the
   root (song dir first, then charts/ and chart-library/ recursively). */
#include <string>
#include <vector>

namespace uss {

struct Song {
    std::string artist, title;
    std::string dir;         // containing folder
    std::string mediaPath;   // absolute sdmc path to media file
    std::string instPath;    // karaoke backing, when present
    std::string chartPath;   // .upsong when resolved
    std::string artPath;     // cover.jpg/.png when present
    std::string sourceId;    // provider id (yt-<id>) when known
};

// Scan `root` (usually "sdmc:/ultrapistar"); returns songs with media.
std::vector<Song> scanCatalog(const std::string& root);

// Locate the .upsong for a media stem: song dir first, then a recursive
// walk of `root` (charts/, chart-library/ included).
std::string findChart(const std::string& root, const std::string& dir,
                      const std::string& stem);

} // namespace uss
