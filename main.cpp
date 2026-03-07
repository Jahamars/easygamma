#include <gtkmm.h>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

enum class Backend { X11, WAYLAND };

static Backend detect_backend() {
    return getenv("WAYLAND_DISPLAY") ? Backend::WAYLAND : Backend::X11;
}

struct GammaConfig { double red, green, blue; };

static std::string config_path() {
    const char* xdg = getenv("XDG_CONFIG_HOME");
    std::string base = xdg ? xdg : (std::string(getenv("HOME")) + "/.config");
    return base + "/easygamma/settings.conf";
}

static void save_config(const GammaConfig& c) {
    auto path = config_path();
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    f << std::fixed << std::setprecision(2) << c.red << "\n" << c.green << "\n" << c.blue << "\n";
}

static GammaConfig load_config() {
    GammaConfig c{1.0, 1.0, 1.0};
    std::ifstream f(config_path());
    if (f) f >> c.red >> c.green >> c.blue;
    return c;
}

// ---- X11 backend -------------------------------------------------------

static std::vector<std::string> x11_get_monitors() {
    std::vector<std::string> out;
    FILE* p = popen("xrandr 2>/dev/null | grep ' connected' | awk '{print $1}'", "r");
    if (!p) return out;
    char buf[128];
    while (fgets(buf, sizeof(buf), p)) {
        std::string s(buf);
        s.erase(s.find_last_not_of(" \n\r\t") + 1);
        if (!s.empty()) out.push_back(s);
    }
    pclose(p);
    return out;
}

static void x11_set_gamma(const std::string& monitor, const GammaConfig& c) {
    std::ostringstream cmd;
    cmd << std::fixed << std::setprecision(2)
        << "xrandr --output " << monitor
        << " --gamma " << c.red << ":" << c.green << ":" << c.blue;
    std::system(cmd.str().c_str());
}

// ---- Wayland backend ---------------------------------------------------
// Compiled only when USE_WAYLAND is defined (set by CMake)

#ifdef USE_WAYLAND
extern "C" {
#include "gamma_wayland.h"
}
static WaylandGamma* wg = nullptr;
#endif

static void wayland_set_gamma(int output_index, const GammaConfig& c) {
#ifdef USE_WAYLAND
    if (wg) wg_set(wg, output_index, c.red, c.green, c.blue);
#else
    (void)output_index; (void)c;
#endif
}

// ---- UI ----------------------------------------------------------------

struct Preset { const char* name; GammaConfig cfg; };
static const Preset PRESETS[] = {
    { "Default", { 1.00, 1.00, 1.00 } },
    { "Night",   { 1.00, 0.75, 0.55 } },
    { "Warm",    { 1.00, 0.90, 0.75 } },
    { "Cool",    { 0.85, 0.90, 1.00 } },
    { "Dim",     { 0.65, 0.65, 0.65 } },
};

class EasyGammaApp : public Gtk::Window {
public:
    EasyGammaApp();
    ~EasyGammaApp();

private:
    void apply_current();
    void on_master_changed();
    void on_channel_changed();
    void on_preset(GammaConfig cfg);

    Backend backend;
    std::vector<std::string> x11_monitors;
    bool syncing = false;

    Gtk::Box root{Gtk::ORIENTATION_VERTICAL, 8};
    Gtk::Box preset_row{Gtk::ORIENTATION_HORIZONTAL, 4};
    Gtk::ComboBoxText monitor_combo;
    Gtk::Scale master{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Scale red{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Scale green{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Scale blue{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Label status;
};

static Gtk::Box* slider_row(const char* name, Gtk::Scale& s) {
    auto* row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
    auto* lbl = Gtk::manage(new Gtk::Label(name));
    lbl->set_width_chars(7);
    lbl->set_xalign(0.0f);
    s.set_range(0.1, 1.0);
    s.set_value(1.0);
    s.set_digits(2);
    s.set_hexpand(true);
    row->pack_start(*lbl, Gtk::PACK_SHRINK);
    row->pack_start(s, Gtk::PACK_EXPAND_WIDGET);
    return row;
}

EasyGammaApp::EasyGammaApp() {
    backend = detect_backend();

    if (backend == Backend::WAYLAND) {
#ifdef USE_WAYLAND
        wg = wg_init();
        if (wg) {
            int n = wg_output_count(wg);
            for (int i = 0; i < n; i++)
                monitor_combo.append(wg_output_name(wg, i));
        }
        if (monitor_combo.get_model()->children().empty())
            monitor_combo.append("(wayland output)");
        monitor_combo.set_active(0);
#endif
    } else {
        x11_monitors = x11_get_monitors();
        for (const auto& m : x11_monitors) monitor_combo.append(m);
        if (!x11_monitors.empty()) monitor_combo.set_active(0);
    }

    set_title(std::string("EasyGamma — ") +
              (backend == Backend::WAYLAND ? "Wayland" : "X11"));
    set_default_size(440, 300);
    set_border_width(12);
    set_resizable(false);

    for (const auto& p : PRESETS) {
        auto* btn = Gtk::manage(new Gtk::Button(p.name));
        GammaConfig cfg = p.cfg;
        btn->signal_clicked().connect([this, cfg]{ on_preset(cfg); });
        preset_row.pack_start(*btn, Gtk::PACK_EXPAND_WIDGET);
    }

    monitor_combo.signal_changed().connect([this]{ apply_current(); });
    master.signal_value_changed().connect(sigc::mem_fun(*this, &EasyGammaApp::on_master_changed));
    red.signal_value_changed().connect(sigc::mem_fun(*this, &EasyGammaApp::on_channel_changed));
    green.signal_value_changed().connect(sigc::mem_fun(*this, &EasyGammaApp::on_channel_changed));
    blue.signal_value_changed().connect(sigc::mem_fun(*this, &EasyGammaApp::on_channel_changed));

    status.set_xalign(0.0f);

    root.pack_start(preset_row, Gtk::PACK_SHRINK);
    root.pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL)), Gtk::PACK_SHRINK);
    root.pack_start(monitor_combo, Gtk::PACK_SHRINK);
    root.pack_start(*slider_row("Master", master), Gtk::PACK_SHRINK);
    root.pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL)), Gtk::PACK_SHRINK);
    root.pack_start(*slider_row("Red",   red),   Gtk::PACK_SHRINK);
    root.pack_start(*slider_row("Green", green), Gtk::PACK_SHRINK);
    root.pack_start(*slider_row("Blue",  blue),  Gtk::PACK_SHRINK);
    root.pack_start(status, Gtk::PACK_SHRINK);

    add(root);
    show_all_children();

    on_preset(load_config());
}

EasyGammaApp::~EasyGammaApp() {
#ifdef USE_WAYLAND
    wg_free(wg);
    wg = nullptr;
#endif
}

void EasyGammaApp::apply_current() {
    GammaConfig c{ red.get_value(), green.get_value(), blue.get_value() };

    if (backend == Backend::WAYLAND) {
        int idx = monitor_combo.get_active_row_number();
        wayland_set_gamma(idx < 0 ? 0 : idx, c);
    } else {
        std::string mon = monitor_combo.get_active_text();
        if (mon.empty()) { status.set_text("No monitor detected"); return; }
        x11_set_gamma(mon, c);
    }

    save_config(c);

    std::ostringstream s;
    s << std::fixed << std::setprecision(2)
      << "R:" << c.red << "  G:" << c.green << "  B:" << c.blue;
    status.set_text(s.str());
}

void EasyGammaApp::on_master_changed() {
    if (syncing) return;
    double v = master.get_value();
    syncing = true;
    red.set_value(v); green.set_value(v); blue.set_value(v);
    syncing = false;
    apply_current();
}

void EasyGammaApp::on_channel_changed() {
    if (syncing) return;
    apply_current();
}

void EasyGammaApp::on_preset(GammaConfig cfg) {
    syncing = true;
    red.set_value(cfg.red); green.set_value(cfg.green); blue.set_value(cfg.blue);
    master.set_value((cfg.red + cfg.green + cfg.blue) / 3.0);
    syncing = false;
    apply_current();
}

int main(int argc, char* argv[]) {
    auto app = Gtk::Application::create(argc, argv, "org.easygamma.app");
    EasyGammaApp window;
    return app->run(window);
}
