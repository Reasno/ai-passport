#pragma once
#include "lvgl.h"
#include <stddef.h>
#include <stdint.h>

/* Pocket Garden artwork. Plants are 48x48 palette run-length data: each byte holds a
 * palette index (bits 0-4, 0 = transparent) and a run length minus one (bits 5-7). */
#define GARDEN_ART_CROPS 6
#define GARDEN_ART_STAGES 4
#define GARDEN_ART_VARIANTS 3
#define GARDEN_ART_OVERLAYS 3
#define GARDEN_ART_SIZE 48
#define GARDEN_ICON_SIZE 16

typedef enum { GARDEN_ART_HEALTHY = 0, GARDEN_ART_DRY, GARDEN_ART_DEFICIENT } garden_art_variant_t;
typedef enum { GARDEN_OVERLAY_WEED = 0, GARDEN_OVERLAY_PEST, GARDEN_OVERLAY_MOON } garden_overlay_t;
typedef enum {
    GARDEN_ICON_WATER = 0,
    GARDEN_ICON_FERTILIZER,
    GARDEN_ICON_HERBICIDE,
    GARDEN_ICON_INSECTICIDE,
    GARDEN_ICON_READY,
    GARDEN_ICON_WATERING_0,
    GARDEN_ICON_COUNT = GARDEN_ICON_WATERING_0 + 4,
} garden_icon_t;

typedef struct {
    const uint8_t *data;
    size_t size;
} garden_rle_t;

extern const uint16_t garden_palette[];
extern const garden_rle_t garden_sprites[GARDEN_ART_CROPS][GARDEN_ART_STAGES][GARDEN_ART_VARIANTS];
extern const garden_rle_t garden_overlays[GARDEN_ART_OVERLAYS];
extern const lv_image_dsc_t garden_icons[GARDEN_ICON_COUNT];
