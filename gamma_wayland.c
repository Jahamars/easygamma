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
    struct wl_output*           wl_output;
    struct zwlr_gamma_control_v1* gamma_control;
    uint32_t                    gamma_size;
    int                         failed;
    char                        name[64];
} Output;

struct WaylandGamma {
    struct wl_display*                  display;
    struct wl_registry*                 registry;
    struct zwlr_gamma_control_manager_v1* manager;
    Output                              outputs[MAX_OUTPUTS];
    int                                 n_outputs;
};

static void gamma_control_gamma_size(void* data, struct zwlr_gamma_control_v1* ctrl, uint32_t size) {
    (void)ctrl;
    Output* o = data;
    o->gamma_size = size;
}

static void gamma_control_failed(void* data, struct zwlr_gamma_control_v1* ctrl) {
    (void)ctrl;
    Output* o = data;
    o->failed = 1;
}

static const struct zwlr_gamma_control_v1_listener gamma_control_listener = {
    .gamma_size = gamma_control_gamma_size,
    .failed     = gamma_control_failed,
};

static void output_handle_geometry(void* data, struct wl_output* wl_output,
    int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
    int32_t subpixel, const char* make, const char* model, int32_t transform) {
    (void)data; (void)wl_output; (void)x; (void)y;
    (void)physical_width; (void)physical_height; (void)subpixel;
    (void)make; (void)model; (void)transform;
}
static void output_handle_mode(void* data, struct wl_output* wl_output,
    uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    (void)data; (void)wl_output; (void)flags;
    (void)width; (void)height; (void)refresh;
}
static void output_handle_done(void* data, struct wl_output* wl_output) {
    (void)data; (void)wl_output;
}
static void output_handle_scale(void* data, struct wl_output* wl_output, int32_t factor) {
    (void)data; (void)wl_output; (void)factor;
}
static void output_handle_name(void* data, struct wl_output* wl_output, const char* name) {
    (void)wl_output;
    Output* o = data;
    strncpy(o->name, name, sizeof(o->name) - 1);
}
static void output_handle_description(void* data, struct wl_output* wl_output, const char* desc) {
    (void)data; (void)wl_output; (void)desc;
}

static const struct wl_output_listener output_listener = {
    .geometry    = output_handle_geometry,
    .mode        = output_handle_mode,
    .done        = output_handle_done,
    .scale       = output_handle_scale,
    .name        = output_handle_name,
    .description = output_handle_description,
};

static void registry_global(void* data, struct wl_registry* registry,
    uint32_t name, const char* interface, uint32_t version) {
    WaylandGamma* wg = data;

    if (strcmp(interface, zwlr_gamma_control_manager_v1_interface.name) == 0) {
        wg->manager = wl_registry_bind(registry, name,
            &zwlr_gamma_control_manager_v1_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0 &&
               wg->n_outputs < MAX_OUTPUTS) {
        Output* o = &wg->outputs[wg->n_outputs++];
        memset(o, 0, sizeof(*o));
        snprintf(o->name, sizeof(o->name), "output-%d", wg->n_outputs);
        o->wl_output = wl_registry_bind(registry, name, &wl_output_interface,
            version >= 4 ? 4 : version);
        wl_output_add_listener(o->wl_output, &output_listener, o);
    }
}

static void registry_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove,
};

WaylandGamma* wg_init(void) {
    WaylandGamma* wg = calloc(1, sizeof(WaylandGamma));
    if (!wg) return NULL;

    wg->display = wl_display_connect(NULL);
    if (!wg->display) { free(wg); return NULL; }

    wg->registry = wl_display_get_registry(wg->display);
    wl_registry_add_listener(wg->registry, &registry_listener, wg);
    wl_display_roundtrip(wg->display);
    wl_display_roundtrip(wg->display);

    if (!wg->manager) {
        wl_registry_destroy(wg->registry);
        wl_display_disconnect(wg->display);
        free(wg);
        return NULL;
    }

    // Init gamma controls for each output
    for (int i = 0; i < wg->n_outputs; i++) {
        Output* o = &wg->outputs[i];
        o->gamma_control = zwlr_gamma_control_manager_v1_get_gamma_control(
            wg->manager, o->wl_output);
        zwlr_gamma_control_v1_add_listener(o->gamma_control,
            &gamma_control_listener, o);
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
    if (wg->manager) zwlr_gamma_control_manager_v1_destroy(wg->manager);
    wl_registry_destroy(wg->registry);
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

// Fill a gamma ramp applying brightness + per-channel multiplier
static void fill_ramp(uint16_t* ramp, uint32_t size, double value) {
    for (uint32_t i = 0; i < size; i++) {
        double v = (double)i / (double)(size - 1);
        v *= value;
        if (v > 1.0) v = 1.0;
        if (v < 0.0) v = 0.0;
        ramp[i] = (uint16_t)(v * 65535.0 + 0.5);
    }
}

int wg_set(WaylandGamma* wg, int output_index, double red, double green, double blue) {
    if (!wg || output_index < 0 || output_index >= wg->n_outputs) return -1;
    Output* o = &wg->outputs[output_index];
    if (o->failed || o->gamma_size == 0) return -1;

    uint32_t size    = o->gamma_size;
    size_t   ramp_sz = size * sizeof(uint16_t);
    size_t   total   = ramp_sz * 3;

    // Create anonymous shared memory
    int fd = memfd_create("easygamma", MFD_CLOEXEC);
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

    wl_display_flush(wg->display);
    return 0;
}
