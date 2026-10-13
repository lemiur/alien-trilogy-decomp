#include <sys/types.h>
#include <LIBGTE.H>
#include <LEVEL.H>
#include <MODEL.H>

/* name: runtime evidence, analysis/runtime/globals.json: brightness of the shoulder lamp
 * or night vision, ramped by GAME_FLAG_LAMP_FADE_IN / GAME_FLAG_LAMP_FADE_OUT */
extern short g_lamp_level;
/* Module statics (.sbss 0x8008d120..0x8008d120), in their retail order. */
/* name: inferred from lights_init, which stores the level's light count */
static short g_light_count;
/* name: inferred from use: light_raise raises the level of its LightRecords up to max_level */
extern LightRecord g_lights[];

/* Multi-statement macro (name invented): copies a colour's r, g and b. */
#define COPY_RGB(d, s) do { (d).r = (s).r; (d).g = (s).g; (d).b = (s).b; } while (0)

/* name: inferred from its body, which copies the level's light records and seeds a palette entry per light */
void lights_init(LightRecord *source, short count)
{
    LightRecord *light;
    CVECTOR *color;
    short i;

    g_light_count = count;
    light = g_lights;
    for (i = 0; i < count; i++, light++, source++) {
        light->colors[0].r = source->colors[0].r;
        light->colors[0].g = source->colors[0].g;
        light->colors[0].b = source->colors[0].b;
        light->colors[1].r = source->colors[1].r;
        light->colors[1].g = source->colors[1].g;
        light->colors[1].b = source->colors[1].b;
        light->colors[2].r = source->colors[2].r;
        light->colors[2].g = source->colors[2].g;
        light->colors[2].b = source->colors[2].b;
        light->colors[3].r = source->colors[3].r;
        light->colors[3].g = source->colors[3].g;
        light->colors[3].b = source->colors[3].b;
        light->mode = source->mode;
        light->flashing = source->flashing;
        light->timer = source->timer;
        light->flash_count = source->flash_count;
        light->flash_time = source->flash_time;
        light->rest_time = source->rest_time;
        light->level = source->level;
        light->max_level = source->max_level;
        color = g_palette[i].colors;
        if (light->mode == LIGHT_MODE_FULL_PALETTE) {
            COPY_RGB(*color, light->colors[1]);
            color++;
            COPY_RGB(*color, light->colors[0]);
            color++;
            COPY_RGB(*color, light->colors[2]);
            color++;
            COPY_RGB(*color, light->colors[3]);
        } else if (light->flashing == 1) {
            COPY_RGB(*color, light->colors[1]);
        } else {
            COPY_RGB(*color, light->colors[0]);
        }
    }
}


/* name: runtime evidence, analysis/runtime/functions.json */
void lights_load(void)
{
    g_lamp_level = 0;
    lights_init(g_level_map.lights, g_level_map.light_count);
}

/* name: inferred from its body, which raises a light's level by amount up to max_level */
void light_raise(int amount, int index)
{
    LightRecord *light = &g_lights[(u_char)index];
    light->level = (u_char)(light->level + (unsigned int)amount);
    if (light->level > light->max_level)
        light->level = light->max_level;
}
