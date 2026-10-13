#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <LEVEL.H>
#include <CAMERA.H>
#include <TASK.H>
#include <AUDIO.H>
#include <GAME.H>
#include <VIEW.H>
#include <TEXT.H>
#include <HUD.H>
#include <SPRITEUV.H>
#include <SPRITE.H>
#include <ANIMS.H>
#include <TEXTURE.H>
#include <ITEMS.H>
#include <LIBETC.H>
#include <MODEL.H>
#include <HEIGHT.H>
#include <PARTICLE.H>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed short s16;

/* Callback layout agrees with src/tasks.c and the callback stores at
 * 0x8003b7ac and 0x8003bac8. Field +0x18 is the task-list next pointer. */

typedef struct EffectEntry {
    int handle;
    s16 flags;
    s16 value;
} EffectEntry;

short g_mode_control = -1;

static unsigned char s_demo[] = "D E M O";
/* where the demo caption blinks, and the g_frame_count bit that blinks it (player_tick). names: invented */
#define DEMO_TEXT_X 0x84
#define DEMO_TEXT_Y 0x2c
#define DEMO_TEXT_BLINK 0x20
extern EffectEntry g_sound_channels[];
/* g_sound_channels: looping sounds the player keeps alive. names: invented */
#define SOUND_CHANNEL_FLAMETHROWER 0 /* weapon_view_update; stopping it fades it out */
#define SOUND_CHANNEL_VISION 1 /* stopped when the lamp or night vision runs out (player_tick) */
#define SOUND_CHANNEL_COUNT 2
#define SOUND_CHANNEL_HOLD 15 /* frames a channel plays on after its last refresh (EffectEntry.flags) */
#define SOUND_CHANNEL_FADE_STEP 300 /* volume a stop takes off the flamethrower channel */
/* Weapon view animation tables: entry 0 idle, 3 firing (player_fire_pulse_grenade).
 * names: runtime evidence, analysis/runtime/globals.json (selecting each weapon stores its table) */
static TrackEntry g_pulse_rifle_sequence[] = {
    { g_pulse_rifle_anim0_frames, g_pulse_rifle_anim0_script }, { g_pulse_rifle_anim2_frames, g_pulse_rifle_anim2_script }, { g_pulse_rifle_anim3_frames, g_pulse_rifle_anim3_script }, { g_pulse_rifle_anim1_frames, g_pulse_rifle_anim1_script }
};
static TrackEntry g_shotgun_sequence[] = {
    { g_shotgun_anim0_frames, g_shotgun_anim0_script }, { g_shotgun_anim1_frames, g_shotgun_anim1_script }
};
static TrackEntry g_pistol_sequence[] = {
    { g_pistol_anim0_frames, g_pistol_anim0_script }, { g_pistol_anim1_frames, g_pistol_anim1_script }, { g_pistol_anim2_frames, g_pistol_anim2_script }
};
static TrackEntry g_flamethrower_sequence[] = {
    { g_flamethrower_anim0_frames, g_flamethrower_anim0_script }, { g_flamethrower_anim1_frames, g_flamethrower_anim1_script }, { g_flamethrower_anim2_frames, g_flamethrower_anim2_script }
};
static TrackEntry g_smart_gun_sequence[] = {
    { g_smart_gun_anim0_frames, g_smart_gun_anim0_script }, { g_smart_gun_anim1_frames, g_smart_gun_anim1_script }, { g_smart_gun_anim2_frames, g_smart_gun_anim2_script }
};
extern void player_view_bob(void);
extern void player_move(void);
extern void weapon_view_update(void);
extern void player_task_fall(TASK *task);
void player_controls(void);
void player_fire_pulse_grenade(void);
void player_throw_grenade(void);
void player_fire(void);
void player_yaw_decrease(void);
void player_yaw_increase(void);
void player_turn_stop(void);
void player_accelerate(void);
void player_accelerate_slow(void);
void player_pitch_decrease(void);
void player_pitch_increase(void);
void player_pitch_recentre(void);
void player_decelerate(void);
void weapon_view_play(void);
void sound_channel_stop(s16 index);
void player_task_death(TASK *task);
void player_task_walk(TASK *task);

static inline void approach_by_eight(short *value, short target)
{
    short current = *value;
    if (current < target) {
        *value = current + PLAYER_PITCH_RECENTRE_STEP;
        if (target < *value) *value = target;
    } else if (target < current) {
        *value = current - PLAYER_PITCH_RECENTRE_STEP;
        if (*value < target) *value = target;
    }
}

static inline void tick(s16 *timer)
{
    if (*timer)
        (*timer)--;
}

static inline void tick_expire(s16 *timer)
{
    if (*timer) {
        if (--*timer == 0) {
            g_game_flags |= GAME_FLAG_LAMP_FADE_OUT;
            sound_channel_stop(SOUND_CHANNEL_VISION);
            sound_play(SOUND_POWER_DOWN, -1, SPU_VOLUME_MAX);
        }
    }
}

static inline void raise_toward(s16 *y, s16 target)
{
    if (*y < target) {
        *y += PLAYER_CLIMB_SPEED;
        if (target < *y)
            *y = target;
    }
}

/* name: inferred from its body, which stores g_mode_control */
void mode_control_set(short value) { g_mode_control = value; }

/* name: static evidence, analysis/runtime/functions.json */
short mode_control_get(void) { return g_mode_control; }

void sound_channel_stop(s16 index)
{
    EffectEntry *entry = &g_sound_channels[index];
    if (entry->handle == -1) return;
    if (index == SOUND_CHANNEL_FLAMETHROWER) {
        s16 value = entry->value - SOUND_CHANNEL_FADE_STEP;
        entry->value = value;
        if (value > 0) {
            sound_adjust(entry->handle, value, 0);
            return;
        }
    }
    sound_stop(entry->handle);
    entry->handle = -1;
    entry->flags = 0;
    entry->value = 0;
}

void sound_channel_play(short index, short x, short y, short value)
{
    EffectEntry *entry = &g_sound_channels[index];
    if (entry->handle == -1)
        entry->handle = sound_play(x, y, value);
    entry->flags = SOUND_CHANNEL_HOLD;
    entry->value = value;
}

void player_yaw_decrease(void) {
    Player *player = &g_player;
    if (player->turn_speed < player->turn_speed_max) player->turn_speed = player->turn_speed + PLAYER_TURN_ACCELERATION;
    if (player->turn_speed_max < player->turn_speed) player->turn_speed = player->turn_speed_max;
    player->angles.vy = (player->angles.vy - player->turn_speed) & ANGLE_MASK;
}

void player_yaw_increase(void) {
    Player *player = &g_player;
    if (player->turn_speed < player->turn_speed_max) player->turn_speed = player->turn_speed + PLAYER_TURN_ACCELERATION;
    if (player->turn_speed_max < player->turn_speed) player->turn_speed = player->turn_speed_max;
    player->angles.vy = (player->angles.vy + player->turn_speed) & ANGLE_MASK;
}

void player_turn_stop(void) { g_player.turn_speed = 0; }

void player_accelerate(void)
{
    SVECTOR *movement = &g_player.movement;
    if (g_player.boots_timer == 0 && (g_player.cell->type == CELL_TYPE_SLOW || g_player.cell->type == CELL_TYPE_LIQUID_DEEP)) {
        movement->vz += g_player.acceleration >> 1;
        if (movement->vz > g_player.speed_max >> 1) movement->vz = g_player.speed_max >> 1;
    } else {
        movement->vz = movement->vz + g_player.acceleration;
        if (movement->vz > g_player.speed_max) movement->vz = g_player.speed_max;
    }
}

void player_accelerate_slow(void)
{
    SVECTOR *movement = &g_player.movement;
    if (g_player.boots_timer == 0 && (g_player.cell->type == CELL_TYPE_SLOW || g_player.cell->type == CELL_TYPE_LIQUID_DEEP)) {
        movement->vz += g_player.acceleration >> 2;
        if (movement->vz > g_player.speed_max >> 2) movement->vz = g_player.speed_max >> 2;
    } else {
        movement->vz += g_player.acceleration >> 1;
        if (movement->vz > g_player.speed_max >> 1) movement->vz = g_player.speed_max >> 1;
    }
}

void player_pitch_decrease(void)
{
    short *pitch = &g_player.angles.vx;
    if (g_player.pitch_speed < g_player.pitch_speed_max) g_player.pitch_speed = g_player.pitch_speed + PLAYER_PITCH_ACCELERATION;
    if (g_player.pitch_speed_max < g_player.pitch_speed) g_player.pitch_speed = g_player.pitch_speed_max;
    *pitch = *pitch - g_player.pitch_speed;
    if (*pitch < g_player.pitch_min) *pitch = g_player.pitch_min;
}

void player_pitch_increase(void)
{
    short *pitch = &g_player.angles.vx;
    if (g_player.pitch_speed < g_player.pitch_speed_max) g_player.pitch_speed = g_player.pitch_speed + PLAYER_PITCH_ACCELERATION;
    if (g_player.pitch_speed_max < g_player.pitch_speed) g_player.pitch_speed = g_player.pitch_speed_max;
    *pitch = *pitch + g_player.pitch_speed;
    if (*pitch > g_player.pitch_max) *pitch = g_player.pitch_max;
}

void player_pitch_recentre(void)
{
    short *pitch = &g_player.angles.vx;
    g_player.pitch_speed = 0;
    approach_by_eight(pitch, g_player.pitch_rest);
}

void player_decelerate(void)
{
    Player *player = &g_player;
    s16 remaining = player->movement.vz;
    s16 step;
    if (remaining > 0) {
        step = player->deceleration;
        if (remaining >= step)
            player->movement.vz = remaining - step;
        else
            player->movement.vz = 0;
    }
}

/* name: inferred from its body */
/* Fires the selected weapon: with ammunition left, it raises the noise, plays the
 * weapon's firing animation and sets game flag 0x400, then shoots; the flamethrower
 * keeps burning while its animation runs. */
void player_fire(void)
{
    switch (g_player.weapon) {
    case WEAPON_PISTOL:
        if (g_player.pistol_ammo != 0) {
            g_player.noise_timer = PLAYER_NOISE_PISTOL;
            g_player.weapon_animation = WEAPON_ANIM_FIRE;
            weapon_view_play();
            g_game_flags |= GAME_FLAG_FLASH;
            pistol_fire();
        }
        break;
    case WEAPON_SHOTGUN:
        if (g_player.shotgun_ammo != 0) {
            g_player.noise_timer = PLAYER_NOISE_SHOTGUN;
            g_player.weapon_animation = WEAPON_ANIM_FIRE;
            weapon_view_play();
            g_game_flags |= GAME_FLAG_FLASH;
            shotgun_fire();
        }
        break;
    case WEAPON_PULSE_RIFLE:
        if (g_player.pulse_rifle_ammo != 0) {
            g_player.noise_timer = PLAYER_NOISE_PULSE_RIFLE;
            g_player.weapon_animation = WEAPON_ANIM_FIRE;
            weapon_view_play();
            g_game_flags |= GAME_FLAG_FLASH;
            pulse_rifle_fire();
        }
        break;
    case WEAPON_FLAMETHROWER:
        if (g_player.flamethrower_fuel != 0) {
            if (g_player.weapon_animation == WEAPON_ANIM_IDLE) {
                g_player.noise_timer = PLAYER_NOISE_FLAMETHROWER;
                g_player.weapon_animation = WEAPON_ANIM_FIRE;
                weapon_view_play();
                g_game_flags |= GAME_FLAG_FLASH;
            }
            flamethrower_fire();
        }
        break;
    case WEAPON_SMART_GUN:
        if (g_player.smart_gun_ammo != 0) {
            g_player.noise_timer = PLAYER_NOISE_SMART_GUN;
            g_player.weapon_animation = WEAPON_ANIM_FIRE;
            weapon_view_play();
            g_game_flags |= GAME_FLAG_FLASH;
            smart_gun_fire();
        }
        break;
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void player_fire_pulse_grenade(void) {
 if(g_player.pulse_rifle_grenades!=0) {
  g_player.noise_timer=PLAYER_NOISE_GRENADE;
  if(!CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO)) g_player.pulse_rifle_grenades--;
  g_player.weapon_animation=WEAPON_ANIM_GRENADE;
  weapon_view_play();pulse_grenade_launch();
 }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void player_throw_grenade(void)
{
    if (g_player.charge_grenades != 0) {
        g_player.noise_timer = PLAYER_NOISE_GRENADE;
        if (!CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO))
            g_player.charge_grenades--;
        grenade_throw();
        sound_play(SOUND_GRENADE_THROW, -1, SPU_VOLUME_MAX / 2);
    }
}

u32 clip_x_to_cell(u32 fallback, u32 row, u32 x, u32 special)
{
    u32 adjusted;
    MapCell *cell;
    if ((special << 16) != 0) adjusted = (x & ~MAP_CELL_MASK) + (MAP_CELL_SIZE + PLAYER_RADIUS);
    else adjusted = (x & ~MAP_CELL_MASK) - (PLAYER_RADIUS + 1);
    cell = g_level_map.cells + g_level_map.width * (row >> MAP_CELL_SHIFT) + (adjusted >> MAP_CELL_SHIFT);
    if ((cell->light & CELL_BLOCKED) != 0 || cell->marker != 0) return fallback;
    return adjusted;
}

/* Steps x by move->vx at z and at z -/+ 0xc8 (the player's width): a cell that is
 * blocked, too low under its ceiling or a step too high stops x at the cell edge
 * (clip_x_to_cell); each probe is pushed off the cell's diagonal walls (diagonal_clip_x).
 * move->vy is the player's height. */
/* name: inferred from its body */
u32 player_move_x(u32 x, u32 z, SVECTOR *move)
{
    short head;
    short negative;
    u32 probe;
    u32 target;
    u32 floor;
    MapCell *cell;

    head = move->vy + PLAYER_HEIGHT;
    negative = (u_short)move->vx >> 15;
    target = x + move->vx;
    if (negative)
        probe = target - PLAYER_RADIUS;
    else
        probe = target + PLAYER_RADIUS;

    floor = floor_height(probe, z);
    cell = MAP_CELL(probe, z);
    if (((cell->light & CELL_BLOCKED) || cell->marker != 0) && !CHEAT_IS_ACTIVE(CHEAT_FLAG_NO_CLIP))
        return clip_x_to_cell(x, z, probe, negative);
    if (cell->type < CELL_TYPE_WALL || (cell->type >= CELL_TYPE_DIAGONAL_FIRST && cell->type < CELL_TYPE_SLOPE)) {
        if ((short)ceiling_height(probe, z) < head)
            return clip_x_to_cell(x, z, probe, negative);
        if (CHEAT_IS_ACTIVE(CHEAT_FLAG_FLOOR_SNAP))
            move->vy = floor;
        if (move->vy < (short)floor && (short)floor - move->vy > PLAYER_STEP_HEIGHT)
            return clip_x_to_cell(x, z, probe, negative);
    }
    probe = diagonal_clip_x(probe, z);

    floor = floor_height(probe, z - PLAYER_RADIUS);
    cell = MAP_CELL(probe, z - PLAYER_RADIUS);
    if (((cell->light & CELL_BLOCKED) || cell->marker != 0) && !CHEAT_IS_ACTIVE(CHEAT_FLAG_NO_CLIP))
        return clip_x_to_cell(x, z, probe, negative);
    if (cell->type < CELL_TYPE_WALL || (cell->type >= CELL_TYPE_DIAGONAL_FIRST && cell->type < CELL_TYPE_SLOPE)) {
        if ((short)ceiling_height(probe, z - PLAYER_RADIUS) < head)
            return clip_x_to_cell(x, z, probe, negative);
        if (CHEAT_IS_ACTIVE(CHEAT_FLAG_FLOOR_SNAP))
            move->vy = floor;
        if (move->vy < (short)floor && (short)floor - move->vy > PLAYER_STEP_HEIGHT)
            return clip_x_to_cell(x, z, probe, negative);
    }
    probe = diagonal_clip_x(probe, z - PLAYER_RADIUS);

    floor = floor_height(probe, z + PLAYER_RADIUS);
    cell = MAP_CELL(probe, z + PLAYER_RADIUS);
    if (((cell->light & CELL_BLOCKED) || cell->marker != 0) && !CHEAT_IS_ACTIVE(CHEAT_FLAG_NO_CLIP))
        return clip_x_to_cell(x, z, probe, negative);
    if (cell->type < CELL_TYPE_WALL || (cell->type >= CELL_TYPE_DIAGONAL_FIRST && cell->type < CELL_TYPE_SLOPE)) {
        if ((short)ceiling_height(probe, z + PLAYER_RADIUS) < head)
            return clip_x_to_cell(x, z, probe, negative);
        if (CHEAT_IS_ACTIVE(CHEAT_FLAG_FLOOR_SNAP))
            move->vy = floor;
        if (move->vy < (short)floor && (short)floor - move->vy > PLAYER_STEP_HEIGHT)
            return clip_x_to_cell(x, z, probe, negative);
    }
    probe = diagonal_clip_x(probe, z + PLAYER_RADIUS);

    if (negative) {
        if (probe < x + move->vx)
            probe = x + move->vx;
    } else {
        if (x + move->vx < probe)
            probe = x + move->vx;
    }
    floor = (short)floor_height(probe, z);
    if ((short)floor < move->vy && move->vy - (short)floor > PLAYER_STEP_HEIGHT) {
        if (negative) {
            x = (probe & ~MAP_CELL_MASK) + (MAP_CELL_SIZE - PLAYER_RADIUS);
            if (x < probe)
                probe = x;
        } else {
            x = (probe & ~MAP_CELL_MASK) + (PLAYER_RADIUS + 1);
            if (probe < x)
                probe = x;
        }
    }
    return probe;
}

u32 clip_z_to_cell(u32 column, u32 fallback, u32 z, u32 special)
{
    u32 adjusted;
    MapCell *cell;
    if ((special << 16) != 0) adjusted = (z & ~MAP_CELL_MASK) + (MAP_CELL_SIZE + PLAYER_RADIUS);
    else adjusted = (z & ~MAP_CELL_MASK) - (PLAYER_RADIUS + 1);
    cell = g_level_map.cells + g_level_map.width * (adjusted >> MAP_CELL_SHIFT) + (column >> MAP_CELL_SHIFT);
    if ((cell->light & CELL_BLOCKED) != 0 || cell->marker != 0) return fallback;
    return adjusted;
}

/* Steps z by move->vz at x and at x -/+ 0xc8 (the player's width): a cell that is
 * blocked, too low under its ceiling or a step too high stops z at the cell edge
 * (clip_z_to_cell); each probe is pushed off the cell's diagonal walls (diagonal_clip_z).
 * move->vy is the player's height. */
/* name: inferred from its body */
u32 player_move_z(u32 x, u32 z, SVECTOR *move)
{
    short head;
    short negative;
    u32 probe;
    u32 target;
    u32 floor;
    MapCell *cell;

    head = move->vy + PLAYER_HEIGHT;
    negative = (u_short)move->vz >> 15;
    target = z + move->vz;
    if (negative)
        probe = target - PLAYER_RADIUS;
    else
        probe = target + PLAYER_RADIUS;

    floor = floor_height(x, probe);
    cell = MAP_CELL(x, probe);
    if (((cell->light & CELL_BLOCKED) || cell->marker != 0) && !CHEAT_IS_ACTIVE(CHEAT_FLAG_NO_CLIP))
        return clip_z_to_cell(x, z, probe, negative);
    if (cell->type < CELL_TYPE_WALL || (cell->type >= CELL_TYPE_DIAGONAL_FIRST && cell->type < CELL_TYPE_SLOPE)) {
        if ((short)ceiling_height(x, probe) < head)
            return clip_z_to_cell(x, z, probe, negative);
        if (CHEAT_IS_ACTIVE(CHEAT_FLAG_FLOOR_SNAP))
            move->vy = floor;
        if (move->vy < (short)floor && (short)floor - move->vy > PLAYER_STEP_HEIGHT)
            return clip_z_to_cell(x, z, probe, negative);
    }
    probe = diagonal_clip_z(x, probe);

    floor = floor_height(x - PLAYER_RADIUS, probe);
    cell = MAP_CELL(x - PLAYER_RADIUS, probe);
    if (((cell->light & CELL_BLOCKED) || cell->marker != 0) && !CHEAT_IS_ACTIVE(CHEAT_FLAG_NO_CLIP))
        return clip_z_to_cell(x, z, probe, negative);
    if (cell->type < CELL_TYPE_WALL || (cell->type >= CELL_TYPE_DIAGONAL_FIRST && cell->type < CELL_TYPE_SLOPE)) {
        if ((short)ceiling_height(x - PLAYER_RADIUS, probe) < head)
            return clip_z_to_cell(x, z, probe, negative);
        if (CHEAT_IS_ACTIVE(CHEAT_FLAG_FLOOR_SNAP))
            move->vy = floor;
        if (move->vy < (short)floor && (short)floor - move->vy > PLAYER_STEP_HEIGHT)
            return clip_z_to_cell(x, z, probe, negative);
    }
    probe = diagonal_clip_z(x - PLAYER_RADIUS, probe);

    floor = floor_height(x + PLAYER_RADIUS, probe);
    cell = MAP_CELL(x + PLAYER_RADIUS, probe);
    if (((cell->light & CELL_BLOCKED) || cell->marker != 0) && !CHEAT_IS_ACTIVE(CHEAT_FLAG_NO_CLIP))
        return clip_z_to_cell(x, z, probe, negative);
    if (cell->type < CELL_TYPE_WALL || (cell->type >= CELL_TYPE_DIAGONAL_FIRST && cell->type < CELL_TYPE_SLOPE)) {
        if ((short)ceiling_height(x + PLAYER_RADIUS, probe) < head)
            return clip_z_to_cell(x, z, probe, negative);
        if (CHEAT_IS_ACTIVE(CHEAT_FLAG_FLOOR_SNAP))
            move->vy = floor;
        if (move->vy < (short)floor && (short)floor - move->vy > PLAYER_STEP_HEIGHT)
            return clip_z_to_cell(x, z, probe, negative);
    }
    probe = diagonal_clip_z(x + PLAYER_RADIUS, probe);

    if (negative) {
        if (probe < z + move->vz)
            probe = z + move->vz;
    } else {
        if (z + move->vz < probe)
            probe = z + move->vz;
    }
    floor = (short)floor_height(x, probe);
    if ((short)floor < move->vy && move->vy - (short)floor > PLAYER_STEP_HEIGHT) {
        if (negative) {
            z = (probe & ~MAP_CELL_MASK) + (MAP_CELL_SIZE - PLAYER_RADIUS);
            if (z < probe)
                probe = z;
        } else {
            z = (probe & ~MAP_CELL_MASK) + (PLAYER_RADIUS + 1);
            if (probe < z)
                probe = z;
        }
    }
    return probe;
}

void player_damage_health(int amount, int mode)
{
    s16 callback = -1;

    if (g_player.damage_timer == 0 && g_player.invulnerable_timer == 0) {
        set_death_type((s16)mode);
        if (!CHEAT_IS_ACTIVE(CHEAT_FLAG_INDESTRUCTIBLE)) {
            g_player.health = g_player.health - amount;
            callback = SOUND_PLAYER_HURT;
            if (g_player.health <= 0) {
                g_player.health = 0;
                g_game_flags |= GAME_FLAG_PLAYER_DEAD;
            }
        }
        if (callback != -1)
            sound_play(callback, -1, SPU_VOLUME_MAX);
        g_player.damage_timer = PLAYER_DAMAGE_TIME;
    }
}

void player_damage(int amount,int mode) {
 s16 callback=-1;
 s16 remembered_mode=mode;
 if(g_player.damage_timer==0 && g_player.invulnerable_timer==0) {
  if(g_player.armour>0 && (s16)mode==DEATH_SHOT) callback=SOUND_SHOT_IMPACT;
  set_death_type(remembered_mode);
  if(!CHEAT_IS_ACTIVE(CHEAT_FLAG_INDESTRUCTIBLE)) {
   if(g_player.armour!=0) {
    g_player.armour-=amount;
    if(g_player.armour<=0) g_player.armour=0;
   } else {
    g_player.health-=amount;
    callback=SOUND_PLAYER_HURT;
    if(g_player.health<=0) {g_player.health=0;g_game_flags|=GAME_FLAG_PLAYER_DEAD;}
   }
  }
  g_player.damage_timer=PLAYER_DAMAGE_TIME;
 }
 if(callback!=-1) sound_play(callback,-1,SPU_VOLUME_MAX);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void player_hit(int first, int second)
{
    player_damage((short)first, (short)second);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void player_hit_from(int x, int state, int y)
{
    short before = g_player.health;

    player_hit((short)x, (short)y);
    if (g_player.health != before) {
        g_player.flash = PLAYER_FLASH_HIT;
        g_player.flash_heading = state;
    }
}

void player_tick(void)
{
    s16 *first = &g_player.invulnerable_timer;
    s16 *last;
    tick(first);
    tick(&g_player.damage_timer);
    tick(&g_player.noise_timer);
    tick(&g_player.boots_timer);
    tick_expire(&g_player.lamp_timer);
    last = &g_player.night_vision_timer;
    tick_expire(last);
    hud_draw_health();
    hud_draw_ammo();
    if ((g_game_flags & GAME_FLAG_DEMO) && !(g_game_flags & GAME_FLAG_FADE) && (g_frame_count & DEMO_TEXT_BLINK))
        hud_draw_text(s_demo, DEMO_TEXT_X, DEMO_TEXT_Y, TEXT_WHITE);
}

/* name: inferred from its body */
/* Steps the weapon view each frame: winds down the flamethrower's sound channel,
 * steps the weapon sprite (playing its sounds and returning it to the idle entry when
 * its sequence ends), places it under the view bob and draws it lit by the player's
 * cell, uploading a new frame first. */
void weapon_view_update(void)
{
    Sprite *sprite;
    SpriteUV *uv;
    PaletteEntry *palette;

    if (g_sound_channels[SOUND_CHANNEL_FLAMETHROWER].handle != -1) {
        if (g_player.weapon == WEAPON_FLAMETHROWER && g_sound_channels[SOUND_CHANNEL_FLAMETHROWER].flags > 0)
            g_sound_channels[SOUND_CHANNEL_FLAMETHROWER].flags--;
        else
            sound_channel_stop(SOUND_CHANNEL_FLAMETHROWER);
    }
    if (g_player.weapon_sequence == 0)
        return;
    sprite = &g_player.weapon_sprite;
    sprite_step(sprite);
    uv = &sprite->uv;
    if (g_player.weapon_sprite.flags & SPRITE_FLAG_SOUND) {
        if (g_player.weapon_sprite.sound == SOUND_FLAMETHROWER)
            sound_channel_play(SOUND_CHANNEL_FLAMETHROWER, SOUND_FLAMETHROWER, -1, SPU_VOLUME_MAX * 3 / 4);
        else
            sound_play(g_player.weapon_sprite.sound, -1, SPU_VOLUME_MAX * 3 / 4);
    }
    if (sprite->flags & SPRITE_FLAG_ENDED) {
        sprite->flags &= ~SPRITE_FLAG_ENDED;
        g_player.weapon_animation = WEAPON_ANIM_IDLE;
        weapon_view_play();
    }
    sprite->screen.vx = WEAPON_VIEW_X;
    sprite->screen.vy = g_player.weapon_lower + WEAPON_VIEW_Y - (g_player.bob >> WEAPON_VIEW_BOB_SHIFT);
    if (g_game_flags & GAME_FLAG_FLASH)
        palette = g_palette;
    else
        palette = &g_palette[g_player.cell->palette];
    if (sprite->slot->pixels != sprite->frame->pixels) {
        if (sprite_frame_upload(sprite, 1) == 0)
            return;
        uv->u3 = uv->u1 = sprite->frame->w - 1;
        uv->v3 = uv->v2 = sprite->slot->y + sprite->frame->h - 1;
        sprite->slot->pixels = sprite->frame->pixels;
        screen_sprite_draw_semi(sprite, palette->colors);
    } else
        screen_sprite_draw_semi(sprite, palette->colors);
}

void player_task_climb(TASK *task)
{
    SVECTOR *position;
    s16 previous;
    s16 target;
    if (g_game_flags & GAME_FLAG_PLAYER_DEAD) {
        task->callback = player_task_death;
        SET_VECTOR_TO_ZERO(g_player.movement);
        player_task_death(task);
        return;
    }
    player_controls();
    target = task->state[0];
    position = &g_player.position;
    raise_toward(&g_player.position.vy, target);
    if (position->vy == target)
        task->callback = player_task_walk;
    previous = position->vy;
    player_move();
    if (position->vy < previous) {
        task->state[0] = position->vy;
        position->vy = previous;
        task->callback = player_task_fall;
    } else if (previous < position->vy) {
        task->state[0] = position->vy;
        position->vy = previous;
        task->callback = player_task_climb;
    }
    player_view_bob();
    player_tick();
    weapon_view_update();
    g_next_task = task->Next;
}

void player_task_death(TASK *task)
{
    g_player.view_height -= PLAYER_DEATH_SINK_STEP;
    if (g_player.view_height < PLAYER_DEAD_VIEW_HEIGHT) {
        g_player.view_height = PLAYER_DEAD_VIEW_HEIGHT;
        g_game_flags |= GAME_FLAG_DEATH_DONE;
    }
    player_pitch_recentre();
    player_turn_stop();
    player_view_bob();
    g_next_task = task->Next;
}

/* name: invented; turns the player with left/right (each ignored while the other was
 * held last frame) */
#define PLAYER_TURN() \
    if ((g_pad_held & PADLleft) && !(g_pad_previous & PADLright)) \
        player_yaw_decrease(); \
    else if ((g_pad_held & PADLright) && !(g_pad_previous & PADLleft)) \
        player_yaw_increase(); \
    else \
        player_turn_stop()

/* name: inferred from its body */
/* The controls for one frame: start pauses; a weapon change lowers and raises the
 * weapon view; run, strafe, look and the automatic look on slopes; turning and
 * walking; firing and grenades by weapon, and reloading or changing weapon when the
 * current one is empty. */
void player_controls(void)
{
    short yaw;

    if (g_game_flags & GAME_FLAG_RESET)
        return;
    yaw = g_player.angles.vy & ANGLE_MASK;
    if ((g_pad_pressed & PADstart) && g_player.weapon_animation == WEAPON_ANIM_IDLE) {
        g_game_flags |= GAME_FLAG_PAUSE;
        return;
    }
    if (g_player.weapon_animation == WEAPON_ANIM_LOWER) {
        g_player.weapon_lower += WEAPON_VIEW_LOWER_STEP;
        if (g_player.weapon_lower > WEAPON_VIEW_LOWERED) {
            g_player.weapon_lower = WEAPON_VIEW_LOWERED;
            if (g_player.flags & PLAYER_CHANGE_DEFAULT) {
                weapon_reset_default();
                DISABLE_FLAG(PLAYER_CHANGE_DEFAULT);
            } else if (g_player.flags & PLAYER_CHANGE_DISCARD) {
                weapon_discard_select_next();
                DISABLE_FLAG(PLAYER_CHANGE_DISCARD);
            } else
                weapon_select_next();
            g_player.weapon_animation = WEAPON_ANIM_RAISE;
        }
    } else if (g_player.weapon_animation == WEAPON_ANIM_RAISE) {
        g_player.weapon_lower -= WEAPON_VIEW_LOWER_STEP;
        if (g_player.weapon_lower == 0)
            g_player.weapon_animation = WEAPON_ANIM_IDLE;
    } else if (!(g_button_look & g_pad_held) && (g_button_weapon & g_pad_pressed)
               && g_player.weapon_animation == WEAPON_ANIM_IDLE && weapon_slot_other_owned())
        g_player.weapon_animation = WEAPON_ANIM_LOWER;
    if (g_button_run & g_pad_held) {
        g_player.flags |= PLAYER_RUN;
        g_player.speed_max = PLAYER_RUN_SPEED;
        g_player.acceleration = PLAYER_RUN_ACCELERATION;
        g_player.deceleration = PLAYER_RUN_DECELERATION;
        g_player.turn_speed_max = PLAYER_RUN_TURN_SPEED;
    } else {
        DISABLE_FLAG(PLAYER_RUN);
        g_player.speed_max = PLAYER_WALK_SPEED;
        g_player.acceleration = PLAYER_WALK_ACCELERATION;
        g_player.deceleration = PLAYER_WALK_DECELERATION;
        g_player.turn_speed_max = PLAYER_WALK_TURN_SPEED;
    }
    if ((g_button_strafe_left & g_pad_held) || (g_button_strafe_right & g_pad_held))
        g_player.flags |= PLAYER_STRAFE;
    else
        DISABLE_FLAG(PLAYER_STRAFE);
    if (g_button_look & g_pad_held) {
        if (g_button_use & g_pad_held)
            player_pitch_decrease();
        else if (g_button_weapon & g_pad_held)
            player_pitch_increase();
    } else if (g_player.cell->type < CELL_TYPE_SLOPE)
        player_pitch_recentre();
    else {
        switch (g_player.cell->type) {
        case CELL_TYPE_SLOPE:
        case CELL_TYPE_SLOPE + CELL_SLOPE_BAND:
        case CELL_TYPE_SLOPE + 2 * CELL_SLOPE_BAND:
            if (yaw <= ANGLE_90 / 2)
                player_pitch_decrease();
            else if (yaw >= ANGLE_90 * 3 / 2 && yaw <= ANGLE_90 * 5 / 2)
                player_pitch_increase();
            else
                player_pitch_recentre();
            break;
        case CELL_TYPE_SLOPE + 1:
        case CELL_TYPE_SLOPE + 1 + CELL_SLOPE_BAND:
        case CELL_TYPE_SLOPE + 1 + 2 * CELL_SLOPE_BAND:
            if (yaw >= ANGLE_90 * 5 / 2 && yaw <= ANGLE_90 * 7 / 2)
                player_pitch_increase();
            else if (yaw >= ANGLE_90 / 2 && yaw <= ANGLE_90 * 3 / 2)
                player_pitch_decrease();
            else
                player_pitch_recentre();
            break;
        case CELL_TYPE_SLOPE + 2:
        case CELL_TYPE_SLOPE + 2 + CELL_SLOPE_BAND:
        case CELL_TYPE_SLOPE + 2 + 2 * CELL_SLOPE_BAND:
            if (yaw >= ANGLE_90 * 5 / 2 && yaw <= ANGLE_90 * 7 / 2)
                player_pitch_decrease();
            else if (yaw >= ANGLE_90 / 2 && yaw <= ANGLE_90 * 3 / 2)
                player_pitch_increase();
            else
                player_pitch_recentre();
            break;
        case CELL_TYPE_SLOPE + 3:
        case CELL_TYPE_SLOPE + 3 + CELL_SLOPE_BAND:
        case CELL_TYPE_SLOPE + 3 + 2 * CELL_SLOPE_BAND:
            if (yaw <= ANGLE_90 / 2)
                player_pitch_increase();
            else if (yaw >= ANGLE_90 * 3 / 2 && yaw <= ANGLE_90 * 5 / 2)
                player_pitch_decrease();
            else
                player_pitch_recentre();
            break;
        default:
            player_pitch_recentre();
            break;
        }
    }
    if (g_player.flags & PLAYER_STRAFE) {
        PLAYER_TURN();
        if ((g_pad_held & PADLup) && (g_button_strafe_left & g_pad_held)) {
            player_accelerate_slow();
            g_player.rotation.vy = (g_player.angles.vy - ANGLE_90 / 2) & ANGLE_MASK;
        } else if ((g_pad_held & PADLup) && (g_button_strafe_right & g_pad_held)) {
            player_accelerate_slow();
            g_player.rotation.vy = (g_player.angles.vy + ANGLE_90 / 2) & ANGLE_MASK;
        } else if ((g_pad_held & PADLdown) && (g_button_strafe_left & g_pad_held)) {
            player_accelerate_slow();
            g_player.rotation.vy = (g_player.angles.vy + ANGLE_90 * 5 / 2) & ANGLE_MASK;
        } else if ((g_pad_held & PADLdown) && (g_button_strafe_right & g_pad_held)) {
            player_accelerate_slow();
            g_player.rotation.vy = (g_player.angles.vy + ANGLE_90 * 3 / 2) & ANGLE_MASK;
        } else if (g_button_strafe_left & g_pad_held) {
            player_accelerate_slow();
            g_player.rotation.vy = (g_player.angles.vy - ANGLE_90) & ANGLE_MASK;
        } else if (g_button_strafe_right & g_pad_held) {
            player_accelerate_slow();
            g_player.rotation.vy = (g_player.angles.vy + ANGLE_90) & ANGLE_MASK;
        } else
            player_decelerate();
    } else {
        PLAYER_TURN();
        if (g_pad_held & PADLup) {
            player_accelerate();
            g_player.rotation.vy = g_player.angles.vy;
        } else if (g_pad_held & PADLdown) {
            player_accelerate_slow();
            g_player.rotation.vy = (g_player.angles.vy + ANGLE_180) & ANGLE_MASK;
        } else
            player_decelerate();
    }
    switch (g_player.weapon) {
    case WEAPON_PISTOL:
    case WEAPON_SHOTGUN:
        if (g_player.weapon_animation != WEAPON_ANIM_IDLE)
            return;
        if (g_button_fire & g_pad_pressed)
            player_fire();
        else if (g_button_grenade & g_pad_pressed)
            player_throw_grenade();
        break;
    case WEAPON_FLAMETHROWER:
        if (g_player.weapon_animation == WEAPON_ANIM_LOWER || g_player.weapon_animation == WEAPON_ANIM_RAISE)
            break;
        if (g_button_fire & g_pad_held)
            player_fire();
        else if (g_button_grenade & g_pad_pressed)
            player_throw_grenade();
        break;
    case WEAPON_SMART_GUN:
        if (g_player.weapon_animation != WEAPON_ANIM_IDLE)
            return;
        if (g_button_fire & g_pad_held)
            player_fire();
        else if (g_button_grenade & g_pad_pressed)
            player_throw_grenade();
        break;
    case WEAPON_PULSE_RIFLE:
        if (g_player.weapon_animation != WEAPON_ANIM_IDLE)
            return;
        if (g_button_fire & g_pad_held)
            player_fire();
        else if (g_button_grenade & g_pad_pressed)
            player_fire_pulse_grenade();
        break;
    }
    if (g_player.weapon_animation != WEAPON_ANIM_IDLE)
        return;
    switch (g_player.weapon) {
    case WEAPON_PISTOL:
        if (g_player.pistol_ammo != 0)
            return;
        if (g_player.pistol_clips != 0) {
            g_player.weapon_animation = WEAPON_ANIM_RELOAD;
            g_player.pistol_ammo = PISTOL_CLIP_ROUNDS;
            g_player.pistol_clips--;
            weapon_view_play();
        } else if (!weapon_slot_other_owned()) {
            g_player.weapon_animation = WEAPON_ANIM_RELOAD;
            g_player.pistol_ammo = 1;
            weapon_view_play();
        } else {
            g_player.weapon_animation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DISCARD;
        }
        break;
    case WEAPON_PULSE_RIFLE:
        if (g_player.pulse_rifle_ammo != 0)
            return;
        if (g_player.pulse_rifle_clips != 0) {
            g_player.weapon_animation = WEAPON_ANIM_RELOAD;
            g_player.pulse_rifle_ammo = PULSE_RIFLE_CLIP_ROUNDS;
            g_player.pulse_rifle_clips--;
            weapon_view_play();
        } else if (g_player.pulse_rifle_grenades == 0) {
            g_player.weapon_animation = WEAPON_ANIM_LOWER;
            if (weapon_slot_other_owned())
                g_player.flags |= PLAYER_CHANGE_DISCARD;
            else if (g_player.pistol_ammo == 0) {
                g_player.pistol_ammo = 1;
                g_player.flags |= PLAYER_CHANGE_DEFAULT;
            }
        }
        break;
    case WEAPON_SMART_GUN:
        if (g_player.smart_gun_ammo != 0)
            return;
        if (g_player.smart_gun_clips != 0) {
            g_player.weapon_animation = WEAPON_ANIM_RELOAD;
            g_player.smart_gun_ammo = SMART_GUN_CLIP_ROUNDS;
            g_player.smart_gun_clips--;
            weapon_view_play();
        } else if (weapon_slot_other_owned()) {
            g_player.weapon_animation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DISCARD;
        } else {
            if (g_player.pistol_ammo == 0)
                g_player.pistol_ammo = 1;
            g_player.weapon_animation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DEFAULT;
        }
        break;
    case WEAPON_SHOTGUN:
        if (g_player.shotgun_ammo != 0)
            return;
        if (weapon_slot_other_owned()) {
            g_player.weapon_animation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DISCARD;
        } else {
            if (g_player.pistol_ammo == 0)
                g_player.pistol_ammo = 1;
            g_player.weapon_animation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DEFAULT;
        }
        break;
    case WEAPON_FLAMETHROWER:
        if (g_player.flamethrower_fuel != 0)
            return;
        if (weapon_slot_other_owned()) {
            g_player.weapon_animation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DISCARD;
        } else {
            if (g_player.pistol_ammo == 0)
                g_player.pistol_ammo = 1;
            g_player.weapon_animation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DEFAULT;
        }
        break;
    }
}

void player_task_walk(TASK *task)
{
    s16 *y;
    s16 previous;
    if (g_game_flags & GAME_FLAG_PLAYER_DEAD) {
        task->callback = player_task_death;
        SET_VECTOR_TO_ZERO(g_player.movement);
        player_task_death(task);
        return;
    }
    player_controls();
    y = &g_player.position.vy;
    previous = *y;
    player_move();
    if (*y < previous) {
        task->state[0] = *y;
        *y = previous;
        task->state[1] = previous;
        task->callback = player_task_fall;
    } else if (previous < *y) {
        task->state[0] = *y;
        *y = previous;
        task->callback = player_task_climb;
    }
    player_view_bob();
    player_tick();
    weapon_view_update();
    g_next_task = task->Next;
}

/* The sequence entry the weapon view plays. name: invented */
static inline TrackEntry *weapon_entry(Player *player)
{
    return &player->weapon_sequence[player->weapon_animation];
}

/* Restarts the weapon sprite on its current sequence entry. name: inferred from its body */
void weapon_view_play(void)
{
    TrackEntry *entry;
    TexturePage *slot;
    u_short *stream;
    u_short duration;

    if (g_player.weapon_sequence != 0) {
        entry = weapon_entry(&g_player);
        g_player.weapon_sprite.frames = entry->frames;
        g_player.weapon_sprite.frame = 0;
        stream = entry->stream;
        g_player.weapon_sprite.remaining = 1;
        duration = *stream;
        slot = g_player.weapon_sprite.slot;
        stream += 2;
        g_player.weapon_sprite.loop = (short *)stream;
        g_player.weapon_sprite.script = (short *)(stream - 1);
        g_player.weapon_sprite.repeat = 0;
        g_player.weapon_sprite.flags = 0;
        g_player.weapon_sprite.uv.u0 = 0;
        g_player.weapon_sprite.duration = duration;
        g_player.weapon_sprite.uv.v0 = slot->y;
        g_player.weapon_sprite.uv.clut = g_player.weapon_sprite.clut->clut;
        g_player.weapon_sprite.uv.v1 = slot->y;
        g_player.weapon_sprite.uv.tpage = slot->tpage;
        g_player.weapon_sprite.uv.u2 = 0;
        slot->pixels = 0;
        sprite_step(&g_player.weapon_sprite);
    }
}

/* name: inferred from its body, which stores Player.weapon and sets up the weapon sprite */
void weapon_select(int mode) {
    g_player.weapon_animation = WEAPON_ANIM_IDLE;
    g_player.weapon_sprite.flags = 0;
    g_player.weapon = mode;
    switch ((s16)mode) {
    case WEAPON_PISTOL: g_player.weapon_sequence = g_pistol_sequence; g_player.weapon_sprite.clut = g_pistol_clut; break;
    case WEAPON_SHOTGUN: g_player.weapon_sequence = g_shotgun_sequence; g_player.weapon_sprite.clut = g_shotgun_clut; break;
    case WEAPON_PULSE_RIFLE: g_player.weapon_sequence = g_pulse_rifle_sequence; g_player.weapon_sprite.clut = g_pulse_rifle_clut; break;
    case WEAPON_FLAMETHROWER: g_player.weapon_sequence = g_flamethrower_sequence; g_player.weapon_sprite.clut = g_flamethrower_clut; break;
    case WEAPON_SMART_GUN: g_player.weapon_sequence = g_smart_gun_sequence; g_player.weapon_sprite.clut = g_smart_gun_clut; break;
    default: g_player.weapon_sequence = 0; break;
    }
    weapon_view_play();
}

/* name: inferred from its body, which gives the weapon sprite the first VRAM slot and marks it used and locked */
TexturePage *weapon_sprite_bind_slot(void)
{
    Player *player = &g_player;
    TexturePage *slot = g_page_sets[PAGE_SET_SPRITES].pages;
    TexturePage *result;
    player->weapon_sprite.slot = slot;
    slot->users = 1;
    result = player->weapon_sprite.slot;
    result->reserved = 1;
    return result;
}

/* The player's first frame on a level: places the player on the level's start cell,
 * facing its start direction, resets the view, timers and tallies, selects the
 * current weapon and continues as player_task_walk. */
void player_task_start(TASK *task)
{
    short i;
    u_int x;
    u_int z;
    SVECTOR *v;
    short index;

    for (i = 0; i < SOUND_CHANNEL_COUNT; i++) {
        g_sound_channels[i].handle = -1;
        g_sound_channels[i].flags = 0;
        g_sound_channels[i].value = 0;
    }
    v = &g_player.position;
    v->vx = (g_level_map.start_x << MAP_CELL_SHIFT) - g_level_map.offset_x;
    v->vz = (g_level_map.start_z << MAP_CELL_SHIFT) - g_level_map.offset_z;
    v->vy = floor_height(v->vx + g_level_map.offset_x, v->vz + g_level_map.offset_z);
    x = v->vx + g_level_map.offset_x;
    z = v->vz + g_level_map.offset_z;
    v = &g_player.angles;
    g_player.cell = MAP_CELL(x, z);
    g_player.cell->marker = CELL_MARKER_PLAYER;
    index = g_player.cell->layout_index;
    g_player.layout_index = index;
    flow_field_fill(&g_level_map.layout_records[index], index);
    v->vx = PLAYER_PITCH_REST;
    v->vz = ANGLE_180;
    switch (g_level_map.start_facing) {
    case FACING_NEG_Z:
        v->vy = ANGLE_180;
        break;
    case FACING_POS_X_NEG_Z:
        v->vy = ANGLE_90 * 5 / 2;
        break;
    case FACING_POS_X:
        v->vy = -ANGLE_90;
        break;
    case FACING_POS_X_POS_Z:
        v->vy = -(ANGLE_90 / 2);
        break;
    case FACING_POS_Z:
        v->vy = 0;
        break;
    case FACING_NEG_X_POS_Z:
        v->vy = ANGLE_90 / 2;
        break;
    case FACING_NEG_X:
        v->vy = ANGLE_90;
        break;
    case FACING_NEG_X_NEG_Z:
        v->vy = ANGLE_90 * 3 / 2;
        break;
    }
    v = &g_player.rotation;
    v->vx = 0;
    v->vy = 0;
    v->vz = ANGLE_180;
    v = &g_player.movement;
    SET_VECTOR_TO_ZERO(*v);
    g_player.batteries = 0;
    g_player.lamp_charges = 0;
    g_player.lamp_timer = 0;
    g_player.night_vision_charges = 0;
    g_player.night_vision_timer = 0;
    g_player.boots_timer = 0;
    g_player.identity_tags = 0;
    g_player.invulnerable_timer = 0;
    DISABLE_FLAG(PLAYER_HAS_AUTO_MAPPER | PLAYER_HUGGER_ATTACHED | PLAYER_FOOTSTEP_ALT);
    for (i = 0; i < PLAYER_TALLY_COUNT; i++)
        g_player.tallies[i] = 0;
    g_player.noise_timer = 0;
    g_player.damage_timer = 0;
    g_player.flash = 0;
    g_player.flash_heading = 0;
    g_player.pitch_rest = PLAYER_PITCH_REST;
    g_player.pitch_max = PLAYER_PITCH_LIMIT;
    g_player.pitch_min = -PLAYER_PITCH_LIMIT;
    g_player.pitch_speed_max = PLAYER_PITCH_SPEED_MAX;
    g_player.pitch_speed = 0;
    g_player.speed_max = PLAYER_WALK_SPEED;
    g_player.acceleration = PLAYER_WALK_ACCELERATION;
    g_player.deceleration = PLAYER_WALK_DECELERATION;
    g_player.turn_speed_max = PLAYER_WALK_TURN_SPEED;
    g_player.turn_speed = 0;
    g_player.view_height = PLAYER_HEIGHT;
    g_player.shot_offset_x = 0;
    g_player.bob = 0;
    g_player.unknown_102 = 1;
    g_player.bob_phase = 2;
    g_player.last_trigger = 0;
    g_player.weapon_lower = 0;
    player_view_bob();
    weapon_select(g_player.weapon);
    weapon_slot_sync();
    task->callback = player_task_walk;
    g_next_task = task->Next;
}

/* name: inferred from its body, which resets the player's state for a level (health 100) */
void player_reset(void) {
    s16 i;
    weapon_slots_reset();
    g_player.flags = 0;
    g_player.health = PLAYER_HEALTH_MAX;
    g_player.armour = 0;
    g_player.charge_grenades = 0;
    g_player.pistol_ammo = 0;
    g_player.pistol_clips = 0;
    g_player.shotgun_ammo = 0;
    g_player.pulse_rifle_ammo = 0;
    g_player.pulse_rifle_clips = 0;
    g_player.pulse_rifle_grenades = 0;
    g_player.flamethrower_fuel = 0;
    g_player.smart_gun_ammo = 0;
    g_player.smart_gun_clips = 0;
    g_player.weapon = WEAPON_PISTOL;
    for (i = 0; i < SOUND_CHANNEL_COUNT; ++i) {
        g_sound_channels[i].handle = -1;
        g_sound_channels[i].flags = 0;
        g_sound_channels[i].value = 0;
    }
    player_give_pickup(PICKUP_PISTOL, PISTOL_CLIP_ROUNDS, PLAYER_START_PISTOL_CLIPS);
}
