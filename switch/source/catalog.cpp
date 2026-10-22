#include "catalog.h"
#include "json.h"
#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace uss {

static std::string join(const std::string& a, const std::string& b) {
    if (a.empty() || a.back() == '/') return a + b;
    return a + "/" + b;
}

static bool isFile(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static std::string stem(const std::string& name) {
    auto dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

static std::string ext(const std::string& name) {
    auto dot = name.rfind('.');
    if (dot == std::string::npos) return "";
    std::string e = name.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    return e;
}

static std::string findArt(const std::string& dir) {
    for (auto* n : {"cover.jpg", "cover.png", "cover.jpeg",
                    "folder.jpg", "artwork.jpg"}) {
        std::string p = join(dir, n);
        if (isFile(p)) return p;
    }
    return "";
}

static bool isMedia(const std::string& name) {
    auto e = ext(name);
    return e == ".mp4" || e == ".mp3" || e == ".mkv" || e == ".webm"
           || e == ".m4a" || e == ".ogg" || e == ".wav" || e == ".flac";
}

static std::vector<std::string> listDir(const std::string& path,
                                        bool dirsOnly = false) {
    std::vector<std::string> out;
    DIR* d = opendir(path.c_str());
    if (!d) return out;
    struct dirent* de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        if (dirsOnly && de->d_type != DT_DIR) continue;
        if (!dirsOnly && de->d_type != DT_REG) continue;
        out.emplace_back(de->d_name);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

static void findChartsRec(const std::string& dir, const std::string& want,
                          std::string& out, int depth = 0) {
    if (!out.empty() || depth > 4) return;
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        std::string p = join(dir, de->d_name);
        if (de->d_type == DT_DIR) {
            findChartsRec(p, want, out, depth + 1);
        } else if (de->d_type == DT_REG && ext(de->d_name) == ".upsong"
                   && stem(de->d_name) == want) {
            out = p;
        }
        if (!out.empty()) break;
    }
    closedir(d);
}

std::string findChart(const std::string& root, const std::string& dir,
                      const std::string& st) {
    // 1) same folder, same stem
    std::string p = join(dir, st + ".upsong");
    if (isFile(p)) return p;
    // 2) same folder, any .upsong (a one-song folder holds its own chart)
    for (auto& f : listDir(dir))
        if (ext(f) == ".upsong") return join(dir, f);
    // 3) recursive hunt under the root (charts/, chart-library/)
    std::string out;
    findChartsRec(root, st, out);
    return out;
}

/* 'Artist - Title' folder name → (artist, title). A name without the
   separator lands entirely on title so nothing is silently dropped. */
static std::pair<std::string, std::string> splitFolder(
    const std::string& name) {
    auto pos = name.find(" - ");
    if (pos == std::string::npos) return {"", name};
    return {name.substr(0, pos), name.substr(pos + 3)};
}

static bool pickMedia(Song& s, const std::string& dir,
                      const std::string& wantStem = "") {
    std::string media, inst;
    for (auto& f : listDir(dir)) {
        if (!isMedia(f)) continue;
        std::string st = stem(f);
        bool isInst = st.size() > 5
                      && st.compare(st.size() - 5, 5, ".inst") == 0;
        if (isInst) { if (inst.empty()) inst = join(dir, f); continue; }
        // fragments and temp files never count
        if (st.find(".part") != std::string::npos
            || st.find(".temp") != std::string::npos
            || ext(f) == ".ytdl") continue;
        if (!wantStem.empty() && st != wantStem) continue;
        if (media.empty()) media = join(dir, f);
    }
    s.mediaPath = media;
    s.instPath = inst;
    return !media.empty();
}

static std::vector<Song> scanManifest(const std::string& root,
                                      const std::string& mpath) {
    std::vector<Song> out;
    std::ifstream f(mpath, std::ios::binary);
    if (!f) return out;
    std::stringstream ss; ss << f.rdbuf();
    ujs::Val man;
    try { man = ujs::parse(ss.str()); } catch (...) { return out; }
    const ujs::Val& songs = man->get("songs");
    if (!songs->isArr()) return out;
    for (auto& rec : songs->arr) {
        std::string rel = rec->get("local_path")->asStr();
        if (rel.empty()) continue;
        Song s;
        s.artist = rec->get("artist")->asStr();
        s.title = rec->get("title")->asStr();
        s.sourceId = rec->get("source_id")->asStr();
        s.dir = join(root, rel.substr(0, rel.find_last_of('/'))
                     .empty() ? "" : rel.substr(0, rel.find_last_of('/')));
        std::string full = join(root, rel);
        if (!isFile(full)) {          // flat fallback: root/rel or dir scan
            std::string dir = full.substr(0, full.find_last_of('/'));
            if (!pickMedia(s, dir, stem(rel))) continue;
            s.dir = dir;
        } else {
            s.mediaPath = full;
            s.dir = full.substr(0, full.find_last_of('/'));
        }
        if (s.artist.empty() && s.title.empty())
            std::tie(s.artist, s.title) =
                splitFolder(s.dir.substr(s.dir.find_last_of('/') + 1));
        std::string inst = rec->get("inst_path")->asStr();
        if (!inst.empty() && isFile(join(root, inst)))
            s.instPath = join(root, inst);
        std::string ch = rec->get("chart")->asStr();
        if (!ch.empty() && isFile(join(root, ch)))
            s.chartPath = join(root, ch);
        if (s.chartPath.empty())
            s.chartPath = findChart(root, s.dir,
                                    stem(s.mediaPath.substr(
                                        s.mediaPath.find_last_of('/') + 1)));
        s.artPath = findArt(s.dir);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<Song> scanCatalog(const std::string& root) {
    std::vector<Song> out = scanManifest(root, join(root, "manifest.json"));
    if (!out.empty()) return out;

    // folder-scan fallback — every subdir holding a media file is a song
    for (auto& dir : listDir(root, true)) {
        std::string full = join(root, dir);
        Song s;
        if (!pickMedia(s, full)) continue;
        std::tie(s.artist, s.title) = splitFolder(dir);
        s.dir = full;
        s.artPath = findArt(full);
        s.chartPath = findChart(
            root, full,
            stem(s.mediaPath.substr(s.mediaPath.find_last_of('/') + 1)));
        out.push_back(std::move(s));
    }
    // flat root media files too (un-organized prepared roots)
    for (auto& f : listDir(root)) {
        if (!isMedia(f) || ext(f) == ".upsong") continue;
        std::string st = stem(f);
        if ((st.size() > 5 && st.compare(st.size() - 5, 5, ".inst") == 0)
            || st.find(".part") != std::string::npos) continue;
        Song s;
        s.dir = root;
        s.mediaPath = join(root, f);
        s.title = st;
        std::tie(s.artist, s.title) = splitFolder(st);
        if (s.title.empty()) s.title = st;
        s.chartPath = findChart(root, root, st);
        s.artPath = findArt(root);
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace uss
