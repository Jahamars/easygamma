#include <gtkmm.h>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;


enum class Backend { X11, WAYLAND };

static Backend detect_backend() {
    const char* wd = getenv("WAYLAND_DISPLAY");
    return (wd && wd[0] != '\0') ? Backend::WAYLAND : Backend::X11;
}


struct GammaConfig {
    double red          = 1.0;
    double green        = 1.0;
    double blue         = 1.0;
    int    monitor_index = 0;
};

static std::string config_path() {
    const char* xdg  = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    std::string base = (xdg && xdg[0] != '\0')
                           ? std::string(xdg)
                           : (home ? std::string(home) + "/.config" : "/tmp");
    return base + "/easygamma/settings.conf";
}

static void save_config(const GammaConfig& c) {
    const std::string path = config_path();
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    if (ec) return;

    std::ofstream f(path);
    if (!f) return;

    f << std::fixed << std::setprecision(4)
      << c.red           << '\n'
      << c.green         << '\n'
      << c.blue          << '\n'
      << c.monitor_index << '\n';
}

static GammaConfig load_config() {
    GammaConfig c;
    std::ifstream f(config_path());
    if (!f) return c;

    double r = 1.0, g = 1.0, b = 1.0;
    int    idx = 0;

    f >> r >> g >> b >> idx;

    c.red          = std::clamp(r,   0.1, 1.0);
    c.green        = std::clamp(g,   0.1, 1.0);
    c.blue         = std::clamp(b,   0.1, 1.0);
    c.monitor_index = (idx >= 0) ? idx : 0;
    return c;
}


static std::vector<std::string> x11_get_monitors() {
    std::vector<std::string> out;
    FILE* p = popen("xrandr 2>/dev/null | awk '/ connected/{print $1}'", "r");
    if (!p) return out;

    char buf[128];
    while (fgets(buf, sizeof(buf), p)) {
        std::string s(buf);
        auto last = s.find_last_not_of(" \t\n\r");
        if (last != std::string::npos) {
            s.erase(last + 1);
            if (!s.empty())
                out.push_back(std::move(s));
        }
    }
    pclose(p);
    return out;
}

static void x11_set_gamma(const std::string& monitor, const GammaConfig& c) {
    if (monitor.empty()) return;
    std::ostringstream cmd;
    cmd << std::fixed << std::setprecision(4)
        << "xrandr --output '" << monitor << "'"
        << " --gamma " << c.red << ':' << c.green << ':' << c.blue;
    std::system(cmd.str().c_str());
}


#ifdef USE_WAYLAND
extern "C" {
#include "gamma_wayland.h"
}
static WaylandGamma* wg = nullptr;
#endif

static void wayland_set_gamma(int output_index, const GammaConfig& c) {
#ifdef USE_WAYLAND
    if (wg)
        wg_set(wg, output_index, c.red, c.green, c.blue);
#else
    (void)output_index;
    (void)c;
#endif
}


struct Preset { const char* name; GammaConfig cfg; };

static const Preset PRESETS[] = {
    { "Default", { 1.00, 1.00, 1.00, 0 } },
    { "Night",   { 1.00, 0.75, 0.55, 0 } },
    { "Warm",    { 1.00, 0.90, 0.75, 0 } },
    { "Cool",    { 0.85, 0.90, 1.00, 0 } },
    { "Dim",     { 0.65, 0.65, 0.65, 0 } },
};


class EasyGammaApp : public Gtk::Window {
public:
    EasyGammaApp();
    ~EasyGammaApp() override;

private:
    void apply_current();
    void on_master_changed();
    void on_channel_changed();
    void on_preset(GammaConfig cfg);
    void on_startup_restore();

    Backend                  backend_;
    std::vector<std::string> x11_monitors_;
    bool                     syncing_ = false;

    Gtk::Box          root_       { Gtk::ORIENTATION_VERTICAL,   8 };
    Gtk::Box          preset_row_ { Gtk::ORIENTATION_HORIZONTAL, 4 };
    Gtk::ComboBoxText monitor_combo_;
    Gtk::Scale        master_     { Gtk::ORIENTATION_HORIZONTAL };
    Gtk::Scale        red_        { Gtk::ORIENTATION_HORIZONTAL };
    Gtk::Scale        green_      { Gtk::ORIENTATION_HORIZONTAL };
    Gtk::Scale        blue_       { Gtk::ORIENTATION_HORIZONTAL };
    Gtk::Label        status_;
};


static Gtk::Box* make_slider_row(const char* label, Gtk::Scale& s) {
    auto* row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
    auto* lbl = Gtk::manage(new Gtk::Label(label));
    lbl->set_width_chars(7);
    lbl->set_xalign(0.0f);

    s.set_range(0.1, 1.0);
    s.set_value(1.0);
    s.set_digits(2);
    s.set_hexpand(true);

    row->pack_start(*lbl, Gtk::PACK_SHRINK);
    row->pack_start(s,    Gtk::PACK_EXPAND_WIDGET);
    return row;
}


EasyGammaApp::EasyGammaApp() {
    backend_ = detect_backend();

    // Populate monitor combo
    if (backend_ == Backend::WAYLAND) {
#ifdef USE_WAYLAND
        wg = wg_init();
        if (wg) {
            int n = wg_output_count(wg);
            for (int i = 0; i < n; ++i) {
                const char* nm = wg_output_name(wg, i);
                monitor_combo_.append(nm ? nm : ("output-" + std::to_string(i)));
            }
        }
        if (monitor_combo_.get_model()->children().empty())
            monitor_combo_.append("(wayland output)");
        monitor_combo_.set_active(0);
#endif
    } else {
        x11_monitors_ = x11_get_monitors();
        for (const auto& m : x11_monitors_)
            monitor_combo_.append(m);
        if (!x11_monitors_.empty())
            monitor_combo_.set_active(0);
    }

    set_title(backend_ == Backend::WAYLAND ? "EasyGamma — Wayland"
                                           : "EasyGamma — X11");
    set_default_size(440, 300);
    set_border_width(12);
    set_resizable(false);

    // Preset buttons
    for (const auto& p : PRESETS) {
        auto* btn = Gtk::manage(new Gtk::Button(p.name));
        GammaConfig cfg = p.cfg;
        btn->signal_clicked().connect([this, cfg] { on_preset(cfg); });
        preset_row_.pack_start(*btn, Gtk::PACK_EXPAND_WIDGET);
    }

    // Build layout — make_slider_row calls set_value(1.0) on the Scale
    // members. Signals are NOT connected yet, so no spurious apply/save fires.
    status_.set_xalign(0.0f);

    root_.pack_start(preset_row_, Gtk::PACK_SHRINK);
    root_.pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL)),
                     Gtk::PACK_SHRINK);
    root_.pack_start(monitor_combo_, Gtk::PACK_SHRINK);
    root_.pack_start(*make_slider_row("Master", master_), Gtk::PACK_SHRINK);
    root_.pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL)),
                     Gtk::PACK_SHRINK);
    root_.pack_start(*make_slider_row("Red",   red_),   Gtk::PACK_SHRINK);
    root_.pack_start(*make_slider_row("Green", green_), Gtk::PACK_SHRINK);
    root_.pack_start(*make_slider_row("Blue",  blue_),  Gtk::PACK_SHRINK);
    root_.pack_start(status_, Gtk::PACK_SHRINK);

    add(root_);
    show_all_children();

    // Connect signals only after layout is fully built
    monitor_combo_.signal_changed().connect(
        [this] { apply_current(); });
    master_.signal_value_changed().connect(
        sigc::mem_fun(*this, &EasyGammaApp::on_master_changed));
    red_.signal_value_changed().connect(
        sigc::mem_fun(*this, &EasyGammaApp::on_channel_changed));
    green_.signal_value_changed().connect(
        sigc::mem_fun(*this, &EasyGammaApp::on_channel_changed));
    blue_.signal_value_changed().connect(
        sigc::mem_fun(*this, &EasyGammaApp::on_channel_changed));

    // Defer config restore to first idle tick — ensures the GTK main loop is
    // running before we call apply_current (Wayland needs wl_display_roundtrip).
    Glib::signal_idle().connect([this]() -> bool {
        on_startup_restore();
        return false;
    });
}

EasyGammaApp::~EasyGammaApp() {
#ifdef USE_WAYLAND
    wg_free(wg);
    wg = nullptr;
#endif
}


void EasyGammaApp::on_startup_restore() {
    GammaConfig cfg = load_config();

    // Restore monitor selection. Use syncing_ to suppress apply_current during
    // this intermediate state (sliders still at default 1.0).
    int n = static_cast<int>(monitor_combo_.get_model()->children().size());
    if (n > 0) {
        syncing_ = true;
        monitor_combo_.set_active(std::clamp(cfg.monitor_index, 0, n - 1));
        syncing_ = false;
    }

    // Sets sliders and calls apply_current() exactly once.
    on_preset(cfg);
}


void EasyGammaApp::apply_current() {
    if (syncing_) return;

    GammaConfig c;
    c.red          = red_.get_value();
    c.green        = green_.get_value();
    c.blue         = blue_.get_value();
    c.monitor_index = std::max(0, monitor_combo_.get_active_row_number());

    if (backend_ == Backend::WAYLAND) {
        wayland_set_gamma(c.monitor_index, c);
    } else {
        const std::string mon = monitor_combo_.get_active_text();
        if (mon.empty()) {
            status_.set_text("No monitor detected");
            return;
        }
        x11_set_gamma(mon, c);
    }

    save_config(c);

    std::ostringstream s;
    s << std::fixed << std::setprecision(2)
      << "R:" << c.red << "  G:" << c.green << "  B:" << c.blue;
    status_.set_text(s.str());
}


void EasyGammaApp::on_master_changed() {
    if (syncing_) return;
    const double v = master_.get_value();
    syncing_ = true;
    red_.set_value(v);
    green_.set_value(v);
    blue_.set_value(v);
    syncing_ = false;
    apply_current();
}

void EasyGammaApp::on_channel_changed() {
    if (syncing_) return;
    apply_current();
}

void EasyGammaApp::on_preset(GammaConfig cfg) {
    syncing_ = true;
    red_.set_value(cfg.red);
    green_.set_value(cfg.green);
    blue_.set_value(cfg.blue);
    master_.set_value((cfg.red + cfg.green + cfg.blue) / 3.0);
    syncing_ = false;
    apply_current();
}


int main(int argc, char* argv[]) {
    auto app = Gtk::Application::create(argc, argv, "org.easygamma.app");
    EasyGammaApp window;
    return app->run(window);
}
