# easygamma

simple GUI for display gamma and brightness control, works on **X11** and **Wayland** 

> **Note:** on wayland, only one app can control gamma at a time
> if gamma does not apply, check for a running `wlsunset`, `gammastep`, or similar

## install from AUR

```bash
yay -S easygamma-git
```

## build from source

### arch based

```bash
sudo pacman -S gtkmm3 cmake wayland wayland-protocols gcc libayatana-appindicator
git clone https://github.com/jahamars/EasyGamma
cd EasyGamma
mkdir build && cd build
cmake ..
make
sudo make install
```

### debian based

```bash
sudo apt install libgtkmm-3.0-dev cmake libwayland-dev wayland-protocols g++ libayatana-appindicator3-dev
git clone https://github.com/jahamars/EasyGamma
cd EasyGamma
mkdir build && cd build
cmake ..
make
sudo make install
```

## features

- presets: default, night, warm, cool, dim
- master slider to control all channels at once
- per-channel R/G/B sliders
- multi-monitor support
- settings saved to `~/.config/easygamma/settings.conf`

## requirements

Wayland gamma control requires a compositor that implements `wlr-gamma-control-unstable-v1`:
Sway, Hyprland, river, and other wlroots-based compositors.
