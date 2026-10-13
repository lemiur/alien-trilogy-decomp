#ifndef CAMERA_H
#define CAMERA_H

#include <TRACK.H>
#include <SPRITEUV.H>
#include <SPRITE.H>

/*
 * Requires <sys/types.h> and <LIBGTE.H>.
 * The player at 0x80099fd4..0x8009a0db: view, weapon, inventory and level tallies
 * (type and object names: runtime evidence, analysis/runtime/globals.json; every
 * published GameShark code at 0x8009a0xx lands on a member of this one object).
 * Retail forms 0x80099fd4 and 0x80099fe4
 * from the 0x80099fec base (0x8003ac70) and 0x80099fd4/0x8009a0d0 from the 0x80099fdc
 * base (0x8003986c), so the whole span is one object. pitch, yaw and roll are copied
 * into the view rotation by the code at 0x800398ec.
 */
#define PLAYER_TALLY_COUNT 37 /* Player.tallies; player_reset clears them all. name: invented */
typedef struct Player {
    SVECTOR position;
    SVECTOR angles; /* invented name: pitch (vx), yaw (vy) and roll (vz) of the view; player_task_start walks one SVECTOR pointer from position onto it; pad identity-TODO */
    SVECTOR rotation;
    SVECTOR movement; /* invented name: rotated by rotation at 0x8003ac70 */
    Sprite weapon_sprite; /* invented name: the weapon view; weapon_view_play steps it with sprite_step, weapon_select picks its clut and clears its flags */
    TrackEntry *weapon_sequence; /* invented name: animation table of the selected weapon (weapon_select), 0 for none */
    short weapon_animation; /* invented name: sequence entry the weapon view plays; 3 when firing (player_fire_pulse_grenade), 0 on selection (weapon_select) */
    short weapon; /* invented name: current weapon 0..4, stored by weapon_select, the argument of weapon_select_slot in the pause menu */
    struct MapCell *cell; /* invented name: cell under the player; its type (5, 9) slows movement at 0x80039b4c */
    int flags; /* invented name: the player's flag word, PLAYER_* in <ITEMS.H>: control state (strafe, run), weapons and auto mapper owned, status (hugger attached, alternate footstep) and the pending weapon change */
    short health; /* invented name: reduced by damage (player_damage_health), set to 100 by player_reset, death flag at 0 */
    short armour; /* invented name: absorbs damage before health (player_damage) */
    short boots_timer; /* name: runtime evidence, analysis/runtime/globals.json: protective boots; counted down by player_tick, player_accelerate/_slow skip the slow-cell penalty (types 5, 9) while nonzero */
    short pistol_ammo; /* invented name: decremented per pistol shot unless CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO) (pistol_fire); the pause menu counts it plus 15 per clip */
    short pistol_clips; /* invented name: the pause menu shows pistol_ammo + 15 x pistol_clips; the password stores it */
    short shotgun_ammo; /* invented name: decremented per shotgun shot unless CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO) (shotgun_fire) */
    short pulse_rifle_ammo; /* invented name: decremented per pulse rifle shot unless CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO) (pulse_rifle_fire) */
    short pulse_rifle_clips; /* invented name: the pause menu shows pulse_rifle_ammo + 100 x pulse_rifle_clips */
    short pulse_rifle_grenades; /* name: runtime evidence, analysis/runtime/globals.json: spent by player_fire_pulse_grenade; HUD shows it only with the pulse rifle; also enables the pause menu's pulse rifle option */
    short flamethrower_fuel; /* invented name: spent by flamethrower_fire, the pause menu's flamethrower count */
    short smart_gun_ammo; /* invented name: the pause menu's smart gun count with smart_gun_clips */
    short smart_gun_clips; /* invented name: the pause menu shows smart_gun_ammo + 100 x smart_gun_clips */
    short charge_grenades; /* name: runtime evidence, analysis/runtime/globals.json: thrown with the grenade button by player_throw_grenade */
    short batteries; /* invented name: spent by entries of type 0x16/0x1a, "Battery required" when 0 (level_object_activate) */
    short lamp_charges; /* invented name: decremented when the pause menu lights the shoulder lamp */
    short lamp_timer; /* invented name: set to 1800 when the lamp is lit (pause_menu_select) */
    short night_vision_charges; /* invented name: decremented when the pause menu starts night vision */
    short night_vision_timer; /* invented name: set to 2700 when night vision starts (pause_menu_select) */
    short noise_timer; /* invented name: set to 0x200 when a door opens (door_task_closed), tested by actor_hears_player */
    short flash; /* invented name: screen flash; 0x20/0x40 when the player fires (pulse_grenade_launch, shotgun_fire), 0x80 when hit (player_hit_from) */
    short flash_heading; /* invented name: direction of the flash: yaw + 0x800 when firing, the hit direction when hit */
    short identity_tags; /* invented name: identity tags picked up (player_give_pickup PICKUP_IDENTITY_TAG), cleared by player_reset, copied to tallies[TALLY_IDENTITY_TAGS] by compute_level_results */
    short invulnerable_timer; /* invented name: blocks damage while nonzero (player_damage), ticked by player_tick */
    /*
     * invented name: level tallies, one array (compute_level_results sums [1..14] and indexes it by a
     * per-level class table, parallel to LevelMap.kind_counts). [0..32] kills by actor kind;
     * [33] switches completed once (door_power, door_power_final); [34] lifts activated once (lift_task_wait);
     * [35] copy of identity_tags and [36] the kill total, both stored by compute_level_results.
     * Constant-index accesses compile to the same absolute address as named members.
     */
    u_short tallies[PLAYER_TALLY_COUNT];
    short damage_timer; /* invented name: set to 30 after damage, blocks damage while nonzero, tints the HUD */
    short turn_speed_max; /* invented name: caps turn_speed (player_yaw_decrease) */
    short turn_speed; /* invented name: grows by 4 per frame, added to yaw (player_yaw_decrease) */
    short pitch_speed_max; /* invented name: caps pitch_speed (player_pitch_decrease) */
    short pitch_speed; /* invented name: grows by 8 per frame, added to pitch (player_pitch_decrease) */
    short pitch_min; /* invented name: lower pitch clamp (player_pitch_decrease) */
    short pitch_max; /* invented name: upper pitch clamp (player_pitch_increase) */
    short pitch_rest; /* invented name: pitch returns to it by 8 per frame (player_pitch_recentre) */
    short layout_index; /* invented name: layout record of the cell under the player; player_reset and the move code at 0x8003af18 refill the flow field (flow_field_fill) from it when it changes */
    short speed_max; /* invented name: caps movement.vz (player_accelerate) */
    short acceleration; /* invented name: added to movement.vz per frame (player_accelerate) */
    short deceleration; /* invented name: subtracted from movement.vz per frame (player_decelerate) */
    short view_height; /* invented name: added to position.vy for the view at 0x8003993c */
    short shot_offset_x; /* invented name: added to position.vx for shots */
    short bob; /* invented name: view bob height, min(sin, cos) of bob_phase >> 6 (player_view_bob) */
    short unknown_102; /* identity-TODO: player_task_start stores 1 (0x8003ce78); no runtime reader in title, options, new game, briefing, play with combat, pause, death and level end (analysis/runtime, round 5) */
    u_short bob_phase; /* invented name: advanced by 1.5 x movement.vz; footstep sound when it wraps (player_view_bob) */
    u_char last_trigger; /* invented name: trigger number (MapCell.trigger) of the cell the player stood in last (level_triggers.c), 0 for none */
    u_char weapon_lower; /* invented name: how far the weapon view is lowered, stepped by player_controls while the weapon changes (WEAPON_ANIM_LOWER/RAISE) */
} Player;

/* name: runtime evidence, analysis/runtime/globals.json */
extern Player g_player;

/* src/weapon_slots.c: gives the player a pickup */
extern void player_give_pickup(short kind, short amount, short extra);

struct TASK;

/* src/player.c: the player's first frame on a level (name: inferred from its body) */
extern void player_task_start(struct TASK *task);

/* src/player.c */
extern void player_hit(int amount, int mode);

/* src/player.c */
extern void player_hit_from(int amount, int angle, int mode);

/* src/player.c */
extern void player_damage_health(int amount, int mode);
extern void mode_control_set(short value);
extern void player_reset(void);
extern void weapon_select(int mode);

/* Player geometry. names: invented */
#define PLAYER_RADIUS 0xc8 /* player_move_x/_z probe this far each side of the player */
#define PLAYER_HEIGHT 0x300 /* head clearance above position.vy (player_move_x), the starting view_height (player_reset) */
#define PLAYER_STEP_HEIGHT 0x100 /* highest step player_move_x walks up */
#define PLAYER_CLIMB_SPEED 0x100 /* height gained per frame while climbing (player_task_climb) */
#define PLAYER_DEAD_VIEW_HEIGHT 0x180 /* player_task_death lowers view_height to it */

/* Movement tuning, walking and running (player_controls, player_reset). names: invented */
#define PLAYER_WALK_SPEED 0x80
#define PLAYER_WALK_ACCELERATION 0xc
#define PLAYER_WALK_DECELERATION 8
#define PLAYER_WALK_TURN_SPEED 0x40
#define PLAYER_RUN_SPEED 0xc0
#define PLAYER_RUN_ACCELERATION 0x10
#define PLAYER_RUN_DECELERATION 0xc
#define PLAYER_RUN_TURN_SPEED 0x50

/* Per-frame steps of turning and looking (player_yaw_*, player_pitch_*). names: invented */
#define PLAYER_TURN_ACCELERATION 4 /* turn_speed growth up to turn_speed_max */
#define PLAYER_PITCH_ACCELERATION 8 /* pitch_speed growth up to pitch_speed_max */
#define PLAYER_PITCH_RECENTRE_STEP 8 /* pitch change toward pitch_rest (player_pitch_recentre) */
#define PLAYER_DEATH_SINK_STEP 12 /* view_height drop per frame (player_task_death) */

/* Look up and down (player_reset). names: invented */
#define PLAYER_PITCH_REST 0x30 /* also the starting pitch (player_task_start) */
#define PLAYER_PITCH_LIMIT 0x80 /* pitch_max, and -pitch_min */
#define PLAYER_PITCH_SPEED_MAX 0x10

/* Player.noise_timer per shot (player_fire, player_fire_pulse_grenade, player_throw_grenade). names: invented */
#define PLAYER_NOISE_PISTOL 0x80
#define PLAYER_NOISE_SHOTGUN 0x200
#define PLAYER_NOISE_PULSE_RIFLE 0x140
#define PLAYER_NOISE_FLAMETHROWER 0x80
#define PLAYER_NOISE_SMART_GUN 0x200
#define PLAYER_NOISE_GRENADE 0x280

/* Task ticks a second: the game steps its tasks 30 times a second (countdown_task counts
 * its seconds in this many ticks; Player timers count ticks). name: invented */
#define TICKS_PER_SECOND 30

/* Player.health and the Player timers. names: invented */
#define PLAYER_HEALTH_MAX 100 /* player_reset; medikits stop here, a hypo pack raises health to it */
#define PLAYER_HEALTH_ADRENALINE 200 /* an adrenaline burst sets health to it, past the maximum */
#define PLAYER_HYPO_INVULNERABLE_TIME (30 * TICKS_PER_SECOND) /* a hypo pack's invulnerable_timer */
#define PLAYER_DAMAGE_TIME TICKS_PER_SECOND /* damage_timer after a hit */
#define PLAYER_LAMP_TIME (60 * TICKS_PER_SECOND) /* lamp_timer when the pause menu lights the lamp */
#define PLAYER_NIGHT_VISION_TIME (90 * TICKS_PER_SECOND) /* night_vision_timer when night vision starts */

/* Player.flash when the player is hit (player_hit_from). name: invented */
#define PLAYER_FLASH_HIT 0x80
/* Player.flash when the player fires the shotgun (shotgun_fire) and a pulse rifle grenade (pulse_grenade_launch). names: invented */
#define PLAYER_FLASH_SHOTGUN 0x40
#define PLAYER_FLASH_GRENADE 0x20

/* The weapon view (weapon_view_update, player_controls). names: invented */
#define WEAPON_VIEW_X 0xa0 /* screen x of the weapon sprite */
#define WEAPON_VIEW_Y 0xf0 /* screen y before the bob and the lowering offset (weapon_lower) */
#define WEAPON_VIEW_LOWERED 0x80 /* lowering offset of a weapon fully lowered for a change */
#define WEAPON_VIEW_LOWER_STEP 0x10 /* lowering offset change per frame */
#define WEAPON_VIEW_BOB_SHIFT 4 /* the weapon sprite rises by bob >> WEAPON_VIEW_BOB_SHIFT */

/* Player.weapon_animation: the weapon sequence entry the view plays, or a weapon change in
 * progress (player_fire, player_controls, weapon_view_update). names: invented */
#define WEAPON_ANIM_IDLE 0
#define WEAPON_ANIM_FIRE 1
#define WEAPON_ANIM_RELOAD 2 /* a clip goes in (player_controls) */
#define WEAPON_ANIM_GRENADE 3 /* pulse rifle grenade (player_fire_pulse_grenade) */
#define WEAPON_ANIM_LOWER 4 /* the view lowers by WEAPON_VIEW_LOWER_STEP, then the weapon changes */
#define WEAPON_ANIM_RAISE 5 /* the new weapon rises back */

/* player_reset gives the pistol with one clip loaded and PLAYER_START_PISTOL_CLIPS spare.
 * name: invented */
#define PLAYER_START_PISTOL_CLIPS 2

#endif
