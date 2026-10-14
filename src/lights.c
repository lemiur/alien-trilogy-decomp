/* lights.c -- level lights: initialising, loading and raising them */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LEVEL.H>
#include <MODEL.H>

/* name: runtime evidence, analysis/runtime/globals.json: brightness of the shoulder lamp
 * or night vision, ramped by GAME_FLAG_LAMP_FADE_IN / GAME_FLAG_LAMP_FADE_OUT */
extern short g_lampLevel;
/* Module statics (.sbss 0x8008d120..0x8008d120), in their retail order. */
/* name: inferred from Light_Init, which stores the level's light count */
static short g_lightCount;
/* name: inferred from use: Light_Raise raises the level of its LightRecords up to maxLevel */
extern lightRecord_t g_lights[];

/* Multi-statement macro (name invented): copies a colour's r, g and b. */
#define COPY_RGB( d, s )   \
    do {                   \
        ( d ).r = ( s ).r; \
        ( d ).g = ( s ).g; \
        ( d ).b = ( s ).b; \
    } while ( 0 )

/*
==================
Light_Init
==================
*/
/* name: inferred from its body, which copies the level's light records and seeds a palette entry per light */
void Light_Init( lightRecord_t *source, short count )
{
    lightRecord_t *light;
    CVECTOR       *color;
    short          i;

    g_lightCount = count;
    light = g_lights;
    for ( i = 0; i < count; i++, light++, source++ ) {
        light->colors[LIGHT_COLOR_REST].r = source->colors[LIGHT_COLOR_REST].r;
        light->colors[LIGHT_COLOR_REST].g = source->colors[LIGHT_COLOR_REST].g;
        light->colors[LIGHT_COLOR_REST].b = source->colors[LIGHT_COLOR_REST].b;
        light->colors[LIGHT_COLOR_FLASH].r = source->colors[LIGHT_COLOR_FLASH].r;
        light->colors[LIGHT_COLOR_FLASH].g = source->colors[LIGHT_COLOR_FLASH].g;
        light->colors[LIGHT_COLOR_FLASH].b = source->colors[LIGHT_COLOR_FLASH].b;
        light->colors[LIGHT_COLOR_PALETTE_2].r = source->colors[LIGHT_COLOR_PALETTE_2].r;
        light->colors[LIGHT_COLOR_PALETTE_2].g = source->colors[LIGHT_COLOR_PALETTE_2].g;
        light->colors[LIGHT_COLOR_PALETTE_2].b = source->colors[LIGHT_COLOR_PALETTE_2].b;
        light->colors[LIGHT_COLOR_PALETTE_3].r = source->colors[LIGHT_COLOR_PALETTE_3].r;
        light->colors[LIGHT_COLOR_PALETTE_3].g = source->colors[LIGHT_COLOR_PALETTE_3].g;
        light->colors[LIGHT_COLOR_PALETTE_3].b = source->colors[LIGHT_COLOR_PALETTE_3].b;
        light->mode = source->mode;
        light->flashing = source->flashing;
        light->timer = source->timer;
        light->flashCount = source->flashCount;
        light->flashTime = source->flashTime;
        light->restTime = source->restTime;
        light->level = source->level;
        light->maxLevel = source->maxLevel;
        color = g_palette[i].colors;
        if ( light->mode == LIGHT_MODE_FULL_PALETTE ) {
            COPY_RGB( *color, light->colors[LIGHT_COLOR_FLASH] );
            color++;
            COPY_RGB( *color, light->colors[LIGHT_COLOR_REST] );
            color++;
            COPY_RGB( *color, light->colors[LIGHT_COLOR_PALETTE_2] );
            color++;
            COPY_RGB( *color, light->colors[LIGHT_COLOR_PALETTE_3] );
        } else if ( light->flashing == 1 ) {
            COPY_RGB( *color, light->colors[LIGHT_COLOR_FLASH] );
        } else {
            COPY_RGB( *color, light->colors[LIGHT_COLOR_REST] );
        }
    }
}


/*
==================
Light_Load
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Light_Load( void )
{
    g_lampLevel = 0;
    Light_Init( g_levelMap.lights, g_levelMap.lightCount );
}

/*
==================
Light_Raise
==================
*/
/* name: inferred from its body, which raises a light's level by amount up to maxLevel */
void Light_Raise( int amount, int index )
{
    lightRecord_t *light = &g_lights[(u_char)index];
    light->level = (u_char)( light->level + (unsigned int)amount );
    if ( light->level > light->maxLevel )
        light->level = light->maxLevel;
}
