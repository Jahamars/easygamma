#define _GNU_SOURCE
#include "gamma_wayland.h"
#include "wlr-gamma-control-unstable-v1-client-protocol.h"

#include <wayland-client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#define MAX_OUTPUTS 16

typedef struct {
    struct wl_output*             wl_output;
    struct zwlr_gamma_control_v1* gamma_control;
    uint32_t                      gamma_size;
    int                           failed;
    char                          name[64];
} Output;

struct WaylandGamma {
    struct wl_display*                    display;
    struct wl_registry*                   registry;
    struct zwlr_gamma_control_manager_v1* manager;
    Output                                outputs[MAX_OUTPUTS];
    int                                   n_outputs;
};


static void on_gamma_size(void* data, struct zwlr_gamma_control_v1* ctrl,
                          uint32_t size)
{
    (void)ctrl;
    ((Output*)data)->gamma_size = size;
}

static void on_gamma_failed(void* data, struct zwlr_gamma_control_v1* ctrl)
{
    (void)ctrl;
    Output* o = (Output*)data;
    o->failed = 1;
    fprintf(stderr, "easygamma: gamma control failed for output '%s'\n",
            o->name);
}

static const struct zwlr_gamma_control_v1_listener gamma_listener = {
    .gamma_size = on_gamma_size,
    .failed     = on_gamma_failed,
};


static void out_geometry(void* d, struct wl_output* o,
    int32_t x, int32_t y, int32_t pw, int32_t ph,
    int32_t sp, const char* mk, const char* mo, int32_t tr)
{ (void)d;(void)o;(void)x;(void)y;(void)pw;(void)ph;(void)sp;(void)mk;(void)mo;(void)tr; }

static void out_mode(void* d, struct wl_output* o,
    uint32_t f, int32_t w, int32_t h, int32_t r)
{ (void)d;(void)o;(void)f;(void)w;(void)h;(void)r; }

static void out_done(void* d, struct wl_output* o)
{ (void)d;(void)o; }

static void out_scale(void* d, struct wl_output* o, int32_t f)
{ (void)d;(void)o;(void)f; }

static void out_name(void* data, struct wl_output* o, const char* name)
{
    (void)o;
    Output* out = (Output*)data;
    strncpy(out->name, name, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
}

static void out_description(void* d, struct wl_output* o, const char* desc)
{ (void)d;(void)o;(void)desc; }

static const struct wl_output_listener output_listener = {
    .geometry    = out_geometry,
    .mode        = out_mode,
    .done        = out_done,
    .scale       = out_scale,
    .name        = out_name,
    .description = out_description,
};


static void registry_global(void* data, struct wl_registry* reg,
    uint32_t name, const char* iface, uint32_t version)
{
    WaylandGamma* wg = (WaylandGamma*)data;

    if (strcmp(iface, zwlr_gamma_control_manager_v1_interface.name) == 0) {
        wg->manager = wl_registry_bind(reg, name,
            &zwlr_gamma_control_manager_v1_interface, 1);

    } else if (strcmp(iface, wl_output_interface.name) == 0 &&
               wg->n_outputs < MAX_OUTPUTS)
    {
        Output* o = &wg->outputs[wg->n_outputs++];
        memset(o, 0, sizeof(*o));
        snprintf(o->name, sizeof(o->name), "output-%d", wg->n_outputs);

        uint32_t ver = (version >= 4) ? 4 : version;
        o->wl_output = wl_registry_bind(reg, name, &wl_output_interface, ver);
        wl_output_add_listener(o->wl_output, &output_listener, o);
    }
}

static void registry_global_remove(void* d, struct wl_registry* r, uint32_t n)
{ (void)d;(void)r;(void)n; }

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove,
};


WaylandGamma* wg_init(void) {
    WaylandGamma* wg = calloc(1, sizeof(WaylandGamma));
    if (!wg) return NULL;

    wg->display = wl_display_connect(NULL);
    if (!wg->display) {
        fprintf(stderr, "easygamma: cannot connect to Wayland display\n");
        free(wg);
        return NULL;
    }

    wg->registry = wl_display_get_registry(wg->display);
    wl_registry_add_listener(wg->registry, &registry_listener, wg);

    wl_display_roundtrip(wg->display); 
    wl_display_roundtrip(wg->display); 

    if (!wg->manager) {
        fprintf(stderr,
            "easygamma: zwlr_gamma_control_manager_v1 not available\n"
            "  Is your compositor wlroots-based? (Sway / Hyprland / river)\n");
        wl_registry_destroy(wg->registry);
        wl_display_disconnect(wg->display);
        free(wg);
        return NULL;
    }

    for (int i = 0; i < wg->n_outputs; i++) {
        Output* o = &wg->outputs[i];
        o->gamma_control = zwlr_gamma_control_manager_v1_get_gamma_control(
            wg->manager, o->wl_output);
        zwlr_gamma_control_v1_add_listener(o->gamma_control, &gamma_listener, o);
    }
    wl_display_roundtrip(wg->display); 

    return wg;
}

void wg_free(WaylandGamma* wg) {
    if (!wg) return;
    for (int i = 0; i < wg->n_outputs; i++) {
        if (wg->outputs[i].gamma_control)
            zwlr_gamma_control_v1_destroy(wg->outputs[i].gamma_control);
        if (wg->outputs[i].wl_output)
            wl_output_destroy(wg->outputs[i].wl_output);
    }
    if (wg->manager)  zwlr_gamma_control_manager_v1_destroy(wg->manager);
    if (wg->registry) wl_registry_destroy(wg->registry);
    wl_display_disconnect(wg->display);
    free(wg);
}

int wg_output_count(WaylandGamma* wg) {
    return wg ? wg->n_outputs : 0;
}

const char* wg_output_name(WaylandGamma* wg, int index) {
    if (!wg || index < 0 || index >= wg->n_outputs) return NULL;
    return wg->outputs[index].name;
}


static void fill_ramp(uint16_t* ramp, uint32_t size, double value) {
    if (value < 0.0) value = 0.0;
    if (value > 1.0) value = 1.0;
    for (uint32_t i = 0; i < size; i++)
        ramp[i] = (uint16_t)(((double)i / (double)(size - 1)) * value * 65535.0 + 0.5);
}

int wg_set(WaylandGamma* wg, int output_index,
           double red, double green, double blue)
{
    if (!wg || output_index < 0 || output_index >= wg->n_outputs)
        return -1;

    Output* o = &wg->outputs[output_index];

    if (o->failed)
        return -1;

    if (o->gamma_size == 0) {
        wl_display_roundtrip(wg->display);
        if (o->gamma_size == 0) {
            fprintf(stderr, "easygamma: gamma_size still 0 for '%s'\n", o->name);
            return -1;
        }
    }

    const uint32_t size  = o->gamma_size;
    const size_t   total = size * 3 * sizeof(uint16_t); 

    int fd = memfd_create("easygamma_ramp", MFD_CLOEXEC);
    if (fd < 0) return -1;

    if (ftruncate(fd, (off_t)total) < 0) { close(fd); return -1; }

    uint16_t* data = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return -1; }

    fill_ramp(data,            size, red);
    fill_ramp(data + size,     size, green);
    fill_ramp(data + size * 2, size, blue);

    munmap(data, total);

    zwlr_gamma_control_v1_set_gamma(o->gamma_control, fd);
    close(fd);

    wl_display_roundtrip(wg->display);
    return 0;
}
