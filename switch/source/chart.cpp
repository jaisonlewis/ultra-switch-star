#include "chart.h"
#include "json.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace uss {

static double num(const std::unordered_map<std::string, std::string>& m,
                  const char* k, double d = 0) {
    auto it = m.find(k);
    if (it == m.end()) return d;
    std::string s = it->second;
    std::replace(s.begin(), s.end(), ',', '.');
    try { return std::stod(s); } catch (...) { return d; }
}

double Chart::bpm() const { return num(headers, "BPM"); }
double Chart::beatMs() const { return bpm() > 0 ? 15000.0 / bpm() : 0; }
double Chart::gapMs() const { return num(headers, "GAP"); }

std::vector<std::string> Chart::trackNames() const {
    std::vector<std::string> out;
    out.reserve(tracks.size());
    for (auto& kv : tracks) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::vector<const Note*>> Chart::lines(
    const std::string& track) const {
    std::vector<std::vector<const Note*>> out;
    auto tit = tracks.find(track);
    if (tit == tracks.end()) return out;
    std::vector<int> brks;
    auto bit = breaks.find(track);
    if (bit != breaks.end()) brks = bit->second;
    std::sort(brks.begin(), brks.end());
    brks.erase(std::unique(brks.begin(), brks.end()), brks.end());

    std::vector<const Note*> cur;
    size_t i = 0;
    for (const Note& n : tit->second) {
        while (i < brks.size() && brks[i] <= n.start) {
            if (!cur.empty() && cur.back()->start <= brks[i]) {
                out.push_back(cur);
                cur.clear();
            }
            ++i;
        }
        cur.push_back(&n);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool Chart::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    ujs::Val d;
    try { d = ujs::parse(ss.str()); }
    catch (const ujs::ParseError&) { return false; }

    if (d->get("format")->asStr() != "upsong"
        || d->get("version")->asInt() != 2)   // NATIVE_VERSION
        return false;

    for (auto& kv : d->get("headers")->obj) {
        std::string key = kv.first;
        for (auto& c : key) c = (char)std::toupper((unsigned char)c);
        headers[key] = kv.second->asStr();
    }
    for (auto& kv : d->get("tracks")->obj) {
        auto& notes = tracks[kv.first];
        for (auto& nv : kv.second->arr) {
            Note n;
            n.start = nv->get("s")->asInt();
            n.length = nv->get("l")->asInt();
            n.pitch = nv->get("p")->asInt();
            n.text = nv->get("t")->asStr();
            auto at = nv->get("at");
            n.alt = at->isStr() ? at->asStr() : "";
            n.wordBreak = nv->get("wb")->asInt() != 0;
            auto k = nv->get("k");
            n.kind = k->isStr() ? k->asStr() : "normal";
            notes.push_back(std::move(n));
        }
    }
    if (tracks.empty()) tracks["P1"] = {};
    for (auto& kv : d->get("breaks")->obj) {
        auto& bs = breaks[kv.first];
        for (auto& bv : kv.second->arr) bs.push_back(bv->asInt());
    }
    return true;
}

} // namespace uss
