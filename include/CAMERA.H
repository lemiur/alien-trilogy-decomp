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
#define PLAYER_TALLY_COUNT 37 /* player_t.tallies; Player_Reset clears them all. name: invented */
typedef struct player_s {
    SVECTOR position;
    SVECTOR angles; /* invented name: pitch (vx), yaw (vy) and roll (vz) of the view; Player_TaskStart walks one SVECTOR pointer from position onto it; pad identity-TODO */
    SVECTOR rotation;
    SVECTOR movement;      /* invented name: rotated by rotation at 0x8003ac70 */
    sprite_t weaponSprite; /* invented name: the weapon view; Player_PlayWeaponView steps it with StepSprite, Player_SelectWeapon picks its clut and clears its flags */
    trackEntry_t *weaponSequence; /* invented name: animation table of the selected weapon (Player_SelectWeapon), 0 for none */
    short weaponAnimation; /* invented name: sequence entry the weapon view plays; 3 when firing (Player_FirePulseGrenade), 0 on selection (Player_SelectWeapon) */
    short weapon; /* invented name: current weapon 0..4, stored by Player_SelectWeapon, the argument of Weapon_SelectSlot in the pause menu */
    struct mapCell_s *cell; /* invented name: cell under the player; its type (5, 9) slows movement at 0x80039b4c */
    int flags; /* invented name: the player's flag word, PLAYER_* in <ITEMS.H>: control state (strafe, run), weapons and auto mapper owned, status (hugger attached, alternate footstep) and the pending weapon change */
    short health; /* invented name: reduced by damage (Player_DamageHealth), set to 100 by Player_Reset, death flag at 0 */
    short armour;     /* invented name: absorbs damage before health (Player_Damage) */
    short bootsTimer; /* name: runtime evidence, analysis/runtime/globals.json: protective boots; counted down by Player_Tick, Player_Accelerate/_slow skip the slow-cell penalty (types 5, 9) while nonzero */
    short pistolAmmo; /* invented name: decremented per pistol shot unless CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO) (Particle_FirePistol); the pause menu counts it plus 15 per clip */
    short pistolClips; /* invented name: the pause menu shows pistolAmmo + 15 x pistolClips; the password stores it */
    short shotgunAmmo; /* invented name: decremented per shotgun shot unless CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO) (Particle_FireShotgun) */
    short pulseRifleAmmo; /* invented name: decremented per pulse rifle shot unless CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO) (Particle_FirePulseRifle) */
    short pulseRifleClips;    /* invented name: the pause menu shows pulseRifleAmmo + 100 x pulseRifleClips */
    short pulseRifleGrenades; /* name: runtime evidence, analysis/runtime/globals.json: spent by Player_FirePulseGrenade; HUD shows it only with the pulse rifle; also enables the pause menu's pulse rifle option */
    short flamethrowerFuel; /* invented name: spent by Particle_FireFlamethrower, the pause menu's flamethrower count */
    short smartGunAmmo;     /* invented name: the pause menu's smart gun count with smartGunClips */
    short smartGunClips;    /* invented name: the pause menu shows smartGunAmmo + 100 x smartGunClips */
    short chargeGrenades; /* name: runtime evidence, analysis/runtime/globals.json: thrown with the grenade button by Player_ThrowGrenade */
    short batteries; /* invented name: spent by entries of type 0x16/0x1a, "Battery required" when 0 (Object_ActivateLevelObject) */
    short lampCharges;        /* invented name: decremented when the pause menu lights the shoulder lamp */
    short lampTimer;          /* invented name: set to 1800 when the lamp is lit (Pause_Select) */
    short nightVisionCharges; /* invented name: decremented when the pause menu starts night vision */
    short nightVisionTimer;   /* invented name: set to 2700 when night vision starts (Pause_Select) */
    short noiseTimer; /* invented name: set to 0x200 when a door opens (Door_TaskClosed), tested by Actor_HearsPlayer */
    short flash; /* invented name: screen flash; 0x20/0x40 when the player fires (Particle_LaunchPulseGrenade, Particle_FireShotgun), 0x80 when hit (Player_HitFrom) */
    short flashHeading; /* invented name: direction of the flash: yaw + 0x800 when firing, the hit direction when hit */
    short identityTags; /* invented name: identity tags picked up (Weapon_PlayerGivePickup PICKUP_IDENTITY_TAG), cleared by Player_Reset, copied to tallies[TALLY_IDENTITY_TAGS] by Menu_ComputeLevelResults */
    short invulnerableTimer; /* invented name: blocks damage while nonzero (Player_Damage), ticked by Player_Tick */
    /*
     * invented name: level tallies, one array (Menu_ComputeLevelResults sums [1..14] and indexes it by a
     * per-level class table, parallel to levelMap_t.kindCounts). [0..32] kills by actor kind;
     * [33] switches completed once (Door_Power, Door_PowerFinal); [34] lifts activated once (Lift_TaskWait);
     * [35] copy of identityTags and [36] the kill total, both stored by Menu_ComputeLevelResults.
     * Constant-index accesses compile to the same absolute address as named members.
     */
    u_short tallies[PLAYER_TALLY_COUNT];
    short   damageTimer;   /* invented name: set to 30 after damage, blocks damage while nonzero, tints the HUD */
    short   turnSpeedMax;  /* invented name: caps turnSpeed (Player_DecreaseYaw) */
    short   turnSpeed;     /* invented name: grows by 4 per frame, added to yaw (Player_DecreaseYaw) */
    short   pitchSpeedMax; /* invented name: caps pitchSpeed (Player_DecreasePitch) */
    short   pitchSpeed;    /* invented name: grows by 8 per frame, added to pitch (Player_DecreasePitch) */
    short   pitchMin;      /* invented name: lower pitch clamp (Player_DecreasePitch) */
    short   pitchMax;      /* invented name: upper pitch clamp (Player_IncreasePitch) */
    short   pitchRest;     /* invented name: pitch returns to it by 8 per frame (Player_RecentrePitch) */
    short layoutIndex; /* invented name: layout record of the cell under the player; Player_Reset and the move code at 0x8003af18 refill the flow field (FillFlowField) from it when it changes */
    short speedMax;    /* invented name: caps movement.vz (Player_Accelerate) */
    short acceleration; /* invented name: added to movement.vz per frame (Player_Accelerate) */
    short deceleration; /* invented name: subtracted from movement.vz per frame (Player_Decelerate) */
    short viewHeight;   /* invented name: added to position.vy for the view at 0x8003993c */
    short shotOffsetX;  /* invented name: added to position.vx for shots */
    short bob;          /* invented name: view bob height, min(sin, cos) of bobPhase >> 6 (Player_ViewBob) */
    short unknown_102; /* identity-TODO: Player_TaskStart stores 1 (0x8003ce78); no runtime reader in title, options, new game, briefing, play with combat, pause, death and level end (analysis/runtime) */
    u_short bobPhase;  /* invented name: advanced by 1.5 x movement.vz; footstep sound when it wraps (Player_ViewBob) */
    u_char lastTrigger; /* invented name: trigger number (mapCell_t.trigger) of the cell the player stood in last (level_triggers.c), 0 for none */
    u_char weaponLower; /* invented name: how far the weapon view is lowered, stepped by Player_Controls while the weapon changes (WEAPON_ANIM_LOWER/RAISE) */
} player_t;

/* name: runtime evidence, analysis/runtime/globals.json */
extern player_t g_player;

/* src/weapon_slots.c: gives the player a pickup */
extern void Weapon_PlayerGivePickup( short kind, short amount, short extra );

struct TASK;

/* src/player.c: the player's first frame on a level (name: inferred from its body) */
extern void Player_TaskStart( struct TASK *task );

/* src/player.c */
extern void Player_Hit( int amount, int mode );

/* src/player.c */
extern void Player_HitFrom( int amount, int angle, int mode );

/* src/player.c */
extern void Player_DamageHealth( int amount, int mode );
extern void Player_SetModeControl( short value );
extern void Player_Reset( void );
extern void Player_SelectWeapon( int mode );

/* One looping sound the player keeps alive (g_soundChannels, src/player.c): the voice handle (-1 when idle), the frames
 * it plays on after its last refresh, and its volume. names: invented */
typedef struct soundChannel_s {
    int   handle;
    short flags;
    short value;
} soundChannel_t;

/* Player geometry. names: invented */
#define PLAYER_RADIUS 0xc8 /* Player_MoveX/_z probe this far each side of the player */
#define PLAYER_HEIGHT \
    0x300 /* head clearance above position.vy (Player_MoveX), the starting viewHeight (Player_Reset) */
#define PLAYER_STEP_HEIGHT 0x100      /* highest step Player_MoveX walks up */
#define PLAYER_CLIMB_SPEED 0x100      /* height gained per frame while climbing (Player_TaskClimb) */
#define PLAYER_DEAD_VIEW_HEIGHT 0x180 /* Player_TaskDeath lowers viewHeight to it */

/* Movement tuning, walking and running (Player_Controls, Player_Reset). names: invented */
#define PLAYER_WALK_SPEED 0x80
#define PLAYER_WALK_ACCELERATION 0xc
#define PLAYER_WALK_DECELERATION 8
#define PLAYER_WALK_TURN_SPEED 0x40
#define PLAYER_RUN_SPEED 0xc0
#define PLAYER_RUN_ACCELERATION 0x10
#define PLAYER_RUN_DECELERATION 0xc
#define PLAYER_RUN_TURN_SPEED 0x50

/* Per-frame steps of turning and looking (player_yaw_*, player_pitch_*). names: invented */
#define PLAYER_TURN_ACCELERATION 4   /* turnSpeed growth up to turnSpeedMax */
#define PLAYER_PITCH_ACCELERATION 8  /* pitchSpeed growth up to pitchSpeedMax */
#define PLAYER_PITCH_RECENTRE_STEP 8 /* pitch change toward pitchRest (Player_RecentrePitch) */
#define PLAYER_DEATH_SINK_STEP 12    /* viewHeight drop per frame (Player_TaskDeath) */

/* Look up and down (Player_Reset). names: invented */
#define PLAYER_PITCH_REST 0x30  /* also the starting pitch (Player_TaskStart) */
#define PLAYER_PITCH_LIMIT 0x80 /* pitchMax, and -pitchMin */
#define PLAYER_PITCH_SPEED_MAX 0x10

/* player_t.noiseTimer per shot (Player_Fire, Player_FirePulseGrenade, Player_ThrowGrenade). names: invented */
#define PLAYER_NOISE_PISTOL 0x80
#define PLAYER_NOISE_SHOTGUN 0x200
#define PLAYER_NOISE_PULSE_RIFLE 0x140
#define PLAYER_NOISE_FLAMETHROWER 0x80
#define PLAYER_NOISE_SMART_GUN 0x200
#define PLAYER_NOISE_GRENADE 0x280

/* Task ticks a second: the game steps its tasks 30 times a second (Timer_Task counts
 * its seconds in this many ticks; Player timers count ticks). name: invented */
#define TICKS_PER_SECOND 30

/* player_t.health and the Player timers. names: invented */
#define PLAYER_HEALTH_MAX 100        /* Player_Reset; medikits stop here, a hypo pack raises health to it */
#define PLAYER_HEALTH_ADRENALINE 200 /* an adrenaline burst sets health to it, past the maximum */
#define PLAYER_HYPO_INVULNERABLE_TIME ( 30 * TICKS_PER_SECOND ) /* a hypo pack's invulnerableTimer */
#define PLAYER_DAMAGE_TIME TICKS_PER_SECOND                     /* damageTimer after a hit */
#define PLAYER_LAMP_TIME ( 60 * TICKS_PER_SECOND )              /* lampTimer when the pause menu lights the lamp */
#define PLAYER_NIGHT_VISION_TIME ( 90 * TICKS_PER_SECOND )      /* nightVisionTimer when night vision starts */

/* player_t.flash when the player is hit (Player_HitFrom). name: invented */
#define PLAYER_FLASH_HIT 0x80
/* player_t.flash when the player fires the shotgun (Particle_FireShotgun) and a pulse rifle grenade (Particle_LaunchPulseGrenade). names: invented */
#define PLAYER_FLASH_SHOTGUN 0x40
#define PLAYER_FLASH_GRENADE 0x20

/* The weapon view (Player_UpdateWeaponView, Player_Controls). names: invented */
#define WEAPON_VIEW_X 0xa0          /* screen x of the weapon sprite */
#define WEAPON_VIEW_Y 0xf0          /* screen y before the bob and the lowering offset (weaponLower) */
#define WEAPON_VIEW_LOWERED 0x80    /* lowering offset of a weapon fully lowered for a change */
#define WEAPON_VIEW_LOWER_STEP 0x10 /* lowering offset change per frame */
#define WEAPON_VIEW_BOB_SHIFT 4     /* the weapon sprite rises by bob >> WEAPON_VIEW_BOB_SHIFT */

/* player_t.weaponAnimation: the weapon sequence entry the view plays, or a weapon change in
 * progress (Player_Fire, Player_Controls, Player_UpdateWeaponView). names: invented */
#define WEAPON_ANIM_IDLE 0
#define WEAPON_ANIM_FIRE 1
#define WEAPON_ANIM_RELOAD 2  /* a clip goes in (Player_Controls) */
#define WEAPON_ANIM_GRENADE 3 /* pulse rifle grenade (Player_FirePulseGrenade) */
#define WEAPON_ANIM_LOWER 4   /* the view lowers by WEAPON_VIEW_LOWER_STEP, then the weapon changes */
#define WEAPON_ANIM_RAISE 5   /* the new weapon rises back */

/* Player_Reset gives the pistol with one clip loaded and PLAYER_START_PISTOL_CLIPS spare.
 * name: invented */
#define PLAYER_START_PISTOL_CLIPS 2

#endif
