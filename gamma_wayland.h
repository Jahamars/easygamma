#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WaylandGamma WaylandGamma;

WaylandGamma* wg_init(void);

void wg_free(WaylandGamma* wg);

int wg_output_count(WaylandGamma* wg);

const char* wg_output_name(WaylandGamma* wg, int index);

int wg_set(WaylandGamma* wg, int output_index,
           double red, double green, double blue);

#ifdef __cplusplus
}
#endif
