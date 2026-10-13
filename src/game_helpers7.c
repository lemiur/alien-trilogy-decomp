#include <sys/types.h>
#include <LIBGTE.H>
#include <LEVEL.H>
#include <CAMERA.H>
#include <SPRITEUV.H>
#include <SPRITE.H>
#include <HEAP.H>
#include <GAME.H>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;


/* name: runtime evidence, analysis/runtime/functions.json */
void unpack_buffers_alloc(void)
{
    int mode;

    g_unpack_buffer = heap_alloc(UNPACK_BUFFER_SIZE);
    g_unpack_buffer_end = heap_alloc(UNPACK_BUFFER_SIZE);
    mode = g_level;
    if (mode == LEVEL_162_QUEENS_LAIR || mode == LEVEL_263_QUEENS_LAIR || mode == LEVEL_391_QUEENS_LAIR)
        g_boss_unpack_buffer = heap_alloc(BOSS_UNPACK_BUFFER_SIZE);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void sprite_run_script(Sprite *sprite)
{
    u16 command;
    short done = 0;

    sprite->flags &= ~SPRITE_FLAG_SOUND;
    while ((*sprite->script & SPRITE_SCRIPT_COMMAND) && done == 0) {
        command = *sprite->script;
        switch (command & SPRITE_COMMAND_MASK) {
        case SPRITE_COMMAND_SOUND:
            sprite->sound = command & SPRITE_COMMAND_OPERAND;
            sprite->flags |= SPRITE_FLAG_SOUND;
            sprite->script++;
            break;
        case SPRITE_COMMAND_LOOP:
            if (sprite->repeat != 0) {
                if (--sprite->repeat == 0) {
                    sprite->flags |= SPRITE_FLAG_ENDED;
                    done = -1;
                } else {
                    sprite->script = sprite->loop;
                }
            } else {
                sprite->repeat = command & SPRITE_COMMAND_OPERAND;
                sprite->script = sprite->loop;
            }
            break;
        case SPRITE_COMMAND_MARK:
            sprite->flags |= SPRITE_FLAG_PACKED;
            sprite->script++;
            break;
        case SPRITE_COMMAND_UNMARK:
            sprite->flags &= ~SPRITE_FLAG_PACKED;
            sprite->script++;
            break;
        case SPRITE_COMMAND_SKIP_5:
            sprite->script++;
            break;
        case SPRITE_COMMAND_END:
            sprite->flags |= SPRITE_FLAG_ENDED;
            done = -1;
            break;
        case SPRITE_COMMAND_CUE:
            sprite->flags |= SPRITE_FLAG_CUE;
            sprite->script++;
            break;
        case SPRITE_COMMAND_HALT:
            sprite->flags |= SPRITE_FLAG_HALTED;
            done = -1;
            break;
        case SPRITE_COMMAND_SKIP_7:
            sprite->script++;
            break;
        default:
            sprite->script++;
            break;
        }
    }
}

/* name: inferred from its body, which advances a sprite's animation script by one tick */
void sprite_step(Sprite *sprite)
{
    u16 remaining = sprite->remaining - 1;
    sprite->remaining = remaining;
    if (((u32)remaining << 16) == 0) {
        sprite->remaining = sprite->duration;
        sprite->script++;
        sprite_run_script(sprite);
        if ((sprite->flags & (SPRITE_FLAG_ENDED | SPRITE_FLAG_HALTED)) == 0)
            sprite->frame = sprite->frames + *sprite->script;
    }
}

