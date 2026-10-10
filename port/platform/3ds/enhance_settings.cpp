// visual-enhancements: see enhance_settings.h.
#include "enhance_settings.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace enhance {

Settings Settings::safe() { return Settings{}; }

Settings Settings::recommended() {
    Settings s;
    s.logo_hd = true;
    s.logo_method = 7;                       // Xbr (4xBR)
    // In-game "sharp" stays off: +8-9 ms per update in Azahar (core-1 work of
    // 12 extra draws/7 target switches), late updates in heavy courses.
    s.tex_filter = TexFilter::Auto;
    s.res3d = Res3d::Ds;                     // 3D AA costs 2x the 3D fill: opt-in only
    return s;
}

bool Settings::operator==(const Settings& o) const {
    return logo_hd == o.logo_hd && logo_method == o.logo_method && ui_hd == o.ui_hd &&
           top_layout == o.top_layout && screen_filter == o.screen_filter &&
           full_width_sky == o.full_width_sky && gameplay_filter == o.gameplay_filter &&
           res3d == o.res3d && tex_filter == o.tex_filter && show_fps == o.show_fps &&
           gpu_timing == o.gpu_timing && circle_pad_analog == o.circle_pad_analog &&
           camera_cstick == o.camera_cstick;
}

namespace {
const char* kMethods[] = {"nearest", "sharp", "bilinear", "epx", "mmpx", "superxbr", "mmpx+superxbr", "xbr"};
constexpr int kMethodCount = 8;

void trim(char*& b, char*& e) {
    while (b < e && std::isspace((unsigned char)*b)) ++b;
    while (e > b && std::isspace((unsigned char)e[-1])) --e;
}
bool ieq(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
    return !*a && !*b;
}
bool parse_bool(const char* v, bool& out) {
    if (ieq(v, "1") || ieq(v, "on") || ieq(v, "true") || ieq(v, "oui") || ieq(v, "yes")) { out = true; return true; }
    if (ieq(v, "0") || ieq(v, "off") || ieq(v, "false") || ieq(v, "non") || ieq(v, "no")) { out = false; return true; }
    return false;
}
template <class E>
bool parse_enum(const char* v, const char* const* names, int n, E& out) {
    for (int i = 0; i < n; ++i)
        if (ieq(v, names[i])) { out = E(i); return true; }
    return false;
}
const char* kLayouts[] = {"auto", "ds", "stretch"};
const char* kFilters[] = {"nearest", "sharp"};
const char* kRes[] = {"ds", "native", "2x"};
const char* kTex[] = {"nearest", "auto", "linear"};

// returns 1 applied, 0 unknown key, -1 invalid value
int apply(const char* section, const char* key, const char* val, Settings& s) {
    char full[96];
    std::snprintf(full, sizeof full, "%s.%s", section, key);
    bool b; uint8_t m;
    if (ieq(full, "2d.logo_hd")) return parse_bool(val, b) ? (s.logo_hd = b, 1) : -1;
    if (ieq(full, "2d.logo_method")) return parse_enum(val, kMethods, kMethodCount, m) ? (s.logo_method = m, 1) : -1;
    if (ieq(full, "2d.ui_hd")) return parse_bool(val, b) ? (s.ui_hd = b, 1) : -1;
    if (ieq(full, "2d.top_layout")) return parse_enum(val, kLayouts, 3, s.top_layout) ? 1 : -1;
    if (ieq(full, "2d.screen_filter")) return parse_enum(val, kFilters, 2, s.screen_filter) ? 1 : -1;
    if (ieq(full, "2d.full_width_sky")) return parse_bool(val, b) ? (s.full_width_sky = b, 1) : -1;
    if (ieq(full, "2d.gameplay_filter")) return parse_enum(val, kFilters, 2, s.gameplay_filter) ? 1 : -1;
    if (ieq(full, "3d.resolution")) return parse_enum(val, kRes, 3, s.res3d) ? 1 : -1;
    if (ieq(full, "3d.texture_filter")) return parse_enum(val, kTex, 3, s.tex_filter) ? 1 : -1;
    if (ieq(full, "debug.show_fps")) return parse_bool(val, b) ? (s.show_fps = b, 1) : -1;
    if (ieq(full, "debug.gpu_timing")) return parse_bool(val, b) ? (s.gpu_timing = b, 1) : -1;
    if (ieq(full, "input.circle_pad")) {
        if (ieq(val, "analog") || ieq(val, "analogique")) { s.circle_pad_analog = true; return 1; }
        if (ieq(val, "ds") || ieq(val, "digital") || ieq(val, "numerique")) { s.circle_pad_analog = false; return 1; }
        return parse_bool(val, b) ? (s.circle_pad_analog = b, 1) : -1;
    }
    if (ieq(full, "input.camera_cstick")) {
        if (ieq(val, "off") || ieq(val, "0") || ieq(val, "non")) { s.camera_cstick = 0; return 1; }
        if (ieq(val, "ds") || ieq(val, "arrows") || ieq(val, "fleches") || ieq(val, "1")) { s.camera_cstick = 1; return 1; }
        if (ieq(val, "free") || ieq(val, "libre") || ieq(val, "2")) { s.camera_cstick = 2; return 1; }
        return -1;
    }
    return 0;
}
}  // namespace

ParseReport parse_ini(const char* text, Settings& s) {
    ParseReport r;
    char section[32] = "";
    const char* p = text;
    while (*p) {
        const char* nl = std::strchr(p, '\n');
        const char* end = nl ? nl : p + std::strlen(p);
        char line[256];
        size_t n = size_t(end - p);
        if (n >= sizeof line) n = sizeof line - 1;
        std::memcpy(line, p, n); line[n] = 0;
        p = nl ? nl + 1 : end;
        ++r.lines;
        char* b = line; char* e = line + n;
        if (char* c = std::strpbrk(b, ";#")) e = c;   // comments
        trim(b, e);
        if (b == e) continue;
        *e = 0;
        if (*b == '[') {
            char* close = std::strchr(b, ']');
            if (!close) { ++r.invalid; continue; }
            *close = 0;
            std::snprintf(section, sizeof section, "%s", b + 1);
            continue;
        }
        char* eq = std::strchr(b, '=');
        if (!eq) { ++r.invalid; continue; }
        char* kb = b; char* ke = eq; trim(kb, ke); *ke = 0;
        char* vb = eq + 1; char* ve = e; trim(vb, ve); *ve = 0;
        const int res = apply(section, kb, vb, s);
        if (res > 0) ++r.applied; else if (res == 0) ++r.unknown; else ++r.invalid;
    }
    return r;
}

int write_ini(const Settings& s, char* out, int cap) {
    const int n = std::snprintf(out, size_t(cap),
        "; Super Mario 64 DS (3DS) - options d'affichage\n"
        "; Fichier lu au demarrage ; modifiable depuis le jeu (SELECT+START).\n"
        "[2d]\n"
        "logo_hd = %d            ; logo du titre en haute definition\n"
        "logo_method = %s        ; nearest|sharp|bilinear|epx|mmpx|superxbr|mmpx+superxbr|xbr\n"
        "ui_hd = %d              ; autres elements 2D en HD (experimental)\n"
        "top_layout = %s         ; auto|ds|stretch (redemarrage)\n"
        "screen_filter = %s      ; sharp|nearest, menus (redemarrage)\n"
        "full_width_sky = %d     ; ciel sur toute la largeur (redemarrage)\n"
        "gameplay_filter = %s    ; sharp|nearest, ecran du haut en jeu\n"
        "[3d]\n"
        "resolution = %s         ; ds|native|2x\n"
        "texture_filter = %s     ; nearest|auto|linear\n"
        "[debug]\n"
        "show_fps = %d\n"
        "gpu_timing = %d\n"
        "[input]\n"
        "circle_pad = %s      ; analog (vitesse selon l'inclinaison) | ds (croix numerique, Y pour courir)\n"
        "camera_cstick = %s   ; New 3DS: ds (= fleches camera de l'ecran tactile, bas = L, haut = X) | free (camera libre analogique) | off\n",
        int(s.logo_hd), s.logo_method < kMethodCount ? kMethods[s.logo_method] : "xbr", int(s.ui_hd),
        kLayouts[int(s.top_layout) % 3], kFilters[int(s.screen_filter) % 2], int(s.full_width_sky),
        kFilters[int(s.gameplay_filter) % 2], kRes[int(s.res3d) % 3], kTex[int(s.tex_filter) % 3],
        int(s.show_fps), int(s.gpu_timing), s.circle_pad_analog ? "analog" : "ds",
        s.camera_cstick == 0 ? "off" : s.camera_cstick == 2 ? "free" : "ds");
    return n;
}

bool load_file(const char* path, Settings& s, ParseReport* report) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char buf[4096];
    const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    buf[n] = 0;
    const ParseReport r = parse_ini(buf, s);
    if (report) *report = r;
    return true;
}

bool save_file(const char* path, const Settings& s) {
    char buf[2048];
    const int n = write_ini(s, buf, sizeof buf);
    if (n <= 0 || n >= int(sizeof buf)) return false;
    char tmp[256];
    std::snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE* f = std::fopen(tmp, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(buf, 1, size_t(n), f) == size_t(n);
    if (std::fclose(f) != 0 || !ok) { std::remove(tmp); return false; }
    std::remove(path);              // FAT/sdmc rename does not replace
    return std::rename(tmp, path) == 0;
}

namespace {
Live g_live;
// publish()/snapshot() are game-thread only (overlay + boot); other threads
// read live() atomics. No lock: a spinlock could deadlock across priorities.
Settings g_snapshot;
}
Live& live() { return g_live; }

void publish(const Settings& s) {
    g_snapshot = s;
    g_live.logo_hd.store(s.logo_hd, std::memory_order_relaxed);
    g_live.logo_method.store(s.logo_method, std::memory_order_relaxed);
    g_live.ui_hd.store(s.ui_hd, std::memory_order_relaxed);
    g_live.gameplay_filter.store(uint8_t(s.gameplay_filter), std::memory_order_relaxed);
    g_live.res3d.store(uint8_t(s.res3d), std::memory_order_relaxed);
    g_live.tex_filter.store(uint8_t(s.tex_filter), std::memory_order_relaxed);
    g_live.show_fps.store(s.show_fps, std::memory_order_relaxed);
    g_live.gpu_timing.store(s.gpu_timing, std::memory_order_relaxed);
    g_live.circle_pad_analog.store(s.circle_pad_analog, std::memory_order_relaxed);
    g_live.camera_cstick.store(s.camera_cstick, std::memory_order_relaxed);
    g_live.generation.fetch_add(1, std::memory_order_release);
}

Settings snapshot() {
    return g_snapshot;
}

Settings boot_apply(const char* path) {
    Settings s = Settings::safe();
    ParseReport r;
    const bool loaded = load_file(path, s, &r);
    // Dev/test override (env.txt, ignored by user builds): SM64DS_SETTINGS=
    // "2d.logo_hd=1;3d.texture_filter=auto" (section.key=value, ';'-separated).
    if (const char* extra = std::getenv("SM64DS_SETTINGS")) {
        char text[1024]; int n = 0;
        for (const char* p = extra; *p && n < int(sizeof text) - 40;) {
            const char* end = std::strchr(p, ';'); if (!end) end = p + std::strlen(p);
            const char* dot = static_cast<const char*>(std::memchr(p, '.', size_t(end - p)));
            if (dot) n += std::snprintf(text + n, sizeof text - size_t(n), "[%.*s]\n%.*s\n", int(dot - p), p, int(end - dot - 1), dot + 1);
            p = *end ? end + 1 : end;
        }
        text[n] = 0;
        const ParseReport e = parse_ini(text, s);
        std::printf("Settings override SM64DS_SETTINGS: applied=%d unknown=%d invalid=%d\n", e.applied, e.unknown, e.invalid);
    }
    publish(s);
    auto export_env = [](const char* name, const char* value) {
        if (!std::getenv(name)) setenv(name, value, 0);
    };
    if (s.top_layout != TopLayout::Auto) export_env("SM64DS_TOP_LAYOUT", s.top_layout == TopLayout::Ds ? "ds" : "stretch");
    if (s.screen_filter == ScreenFilter::Nearest) export_env("SM64DS_SCREEN_FILTER", "nearest");
    if (!s.full_width_sky) export_env("SM64DS_TOP_SKY", "0");
    std::printf("Settings %s: %s applied=%d unknown=%d invalid=%d logo_hd=%d method=%u res3d=%u tex=%u\n",
                path, loaded ? "loaded" : "absent (defaults)", r.applied, r.unknown, r.invalid,
                int(s.logo_hd), unsigned(s.logo_method), unsigned(s.res3d), unsigned(s.tex_filter));
    return s;
}

bool texture_wants_linear(uint32_t param, TexFilter policy) {
    if (policy == TexFilter::Nearest) return false;
    if (policy == TexFilter::Linear) return true;
    const unsigned format = (param >> 26) & 7u;
    if (format == 0) return false;                    // no texture
    if (format == 7) return true;                     // direct colour (alpha bit: cut-out only if used)
    if (format == 1 || format == 6) return false;     // A3I5 / A5I3 translucent
    if (param & (1u << 29)) return false;             // colour 0 transparent -> cut-out fringes
    return true;
}

}  // namespace enhance
