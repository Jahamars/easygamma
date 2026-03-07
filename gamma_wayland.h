#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WaylandGamma WaylandGamma;

// Returns NULL if Wayland gamma protocol is not supported
WaylandGamma* wg_init(void);
void          wg_free(WaylandGamma* wg);

// output_index: 0 = first output, 1 = second, etc.
// red/green/blue: 0.0 - 1.0
int wg_set(WaylandGamma* wg, int output_index, double red, double green, double blue);

// Returns number of available outputs
int wg_output_count(WaylandGamma* wg);

// Returns output name (or NULL)
const char* wg_output_name(WaylandGamma* wg, int index);

#ifdef __cplusplus
}
#endif
