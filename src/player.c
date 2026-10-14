/* player.c -- the player: movement, turning and the per-frame timers */
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
#include <TYPES.H>

/* Callback layout agrees with src/tasks.c and the callback stores at
 * 0x8003b7ac and 0x8003bac8. Field +0x18 is the task-list next pointer. */

short g_modeControl = -1;

static unsigned char s_demo[] = "D E M O";
/* where the demo caption blinks, and the g_frameCount bit that blinks it (Player_Tick). names: invented */
#define DEMO_TEXT_X 0x84
#define DEMO_TEXT_Y 0x2c
#define DEMO_TEXT_BLINK 0x20
extern soundChannel_t g_soundChannels[];
/* g_soundChannels: looping sounds the player keeps alive. names: invented */
#define SOUND_CHANNEL_FLAMETHROWER 0 /* Player_UpdateWeaponView; stopping it fades it out */
#define SOUND_CHANNEL_VISION 1       /* stopped when the lamp or night vision runs out (Player_Tick) */
#define SOUND_CHANNEL_COUNT 2
#define SOUND_CHANNEL_HOLD 15       /* frames a channel plays on after its last refresh (soundChannel_t.flags) */
#define SOUND_CHANNEL_FADE_STEP 300 /* volume a stop takes off the flamethrower channel */
/* Weapon view animation tables: entry 0 idle, 3 firing (Player_FirePulseGrenade).
 * names: runtime evidence, analysis/runtime/globals.json (selecting each weapon stores its table) */
static trackEntry_t g_pulseRifleSequence[] = { { g_pulseRifleAnim0Frames, g_pulseRifleAnim0Script },
                                               { g_pulseRifleAnim2Frames, g_pulseRifleAnim2Script },
                                               { g_pulseRifleAnim3Frames, g_pulseRifleAnim3Script },
                                               { g_pulseRifleAnim1Frames, g_pulseRifleAnim1Script } };
static trackEntry_t g_shotgunSequence[] = { { g_shotgunAnim0Frames, g_shotgunAnim0Script },
                                            { g_shotgunAnim1Frames, g_shotgunAnim1Script } };
static trackEntry_t g_pistolSequence[] = { { g_pistolAnim0Frames, g_pistolAnim0Script },
                                           { g_pistolAnim1Frames, g_pistolAnim1Script },
                                           { g_pistolAnim2Frames, g_pistolAnim2Script } };
static trackEntry_t g_flamethrowerSequence[] = { { g_flamethrowerAnim0Frames, g_flamethrowerAnim0Script },
                                                 { g_flamethrowerAnim1Frames, g_flamethrowerAnim1Script },
                                                 { g_flamethrowerAnim2Frames, g_flamethrowerAnim2Script } };
static trackEntry_t g_smartGunSequence[] = { { g_smartGunAnim0Frames, g_smartGunAnim0Script },
                                             { g_smartGunAnim1Frames, g_smartGunAnim1Script },
                                             { g_smartGunAnim2Frames, g_smartGunAnim2Script } };
extern void Player_ViewBob( void );
extern void Player_Move( void );
extern void Player_UpdateWeaponView( void );
extern void Player_TaskFall( TASK *task );
void Player_Controls( void );
void Player_FirePulseGrenade( void );
void Player_ThrowGrenade( void );
void Player_Fire( void );
void Player_DecreaseYaw( void );
void Player_IncreaseYaw( void );
void Player_TurnStop( void );
void Player_Accelerate( void );
void Player_AccelerateSlow( void );
void Player_DecreasePitch( void );
void Player_IncreasePitch( void );
void Player_RecentrePitch( void );
void Player_Decelerate( void );
void Player_PlayWeaponView( void );
void Player_StopSoundChannel( s16 index );
void Player_TaskDeath( TASK *task );
void Player_TaskWalk( TASK *task );

/*
==================
Player_ApproachByEight
==================
*/
static inline void Player_ApproachByEight( short *value, short target )
{
    short current = *value;
    if ( current < target ) {
        *value = current + PLAYER_PITCH_RECENTRE_STEP;
        if ( target < *value )
            *value = target;
    } else if ( target < current ) {
        *value = current - PLAYER_PITCH_RECENTRE_STEP;
        if ( *value < target )
            *value = target;
    }
}

/*
==================
TickTimer
==================
*/
static inline void TickTimer( s16 *timer )
{
    if ( *timer )
        ( *timer )--;
}

/*
==================
TickTimerExpire
==================
*/
static inline void TickTimerExpire( s16 *timer )
{
    if ( *timer ) {
        if ( --*timer == 0 ) {
            g_gameFlags |= GAME_FLAG_LAMP_FADE_OUT;
            Player_StopSoundChannel( SOUND_CHANNEL_VISION );
            Snd_Play( SOUND_POWER_DOWN, -1, SPU_VOLUME_MAX );
        }
    }
}

/*
==================
Player_RaiseToward
==================
*/
static inline void Player_RaiseToward( s16 *y, s16 target )
{
    if ( *y < target ) {
        *y += PLAYER_CLIMB_SPEED;
        if ( target < *y )
            *y = target;
    }
}

/*
==================
Player_SetModeControl
==================
*/
/* name: inferred from its body, which stores g_modeControl */
void Player_SetModeControl( short value )
{
    g_modeControl = value;
}

/*
==================
Player_GetModeControl
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
short Player_GetModeControl( void )
{
    return g_modeControl;
}

/*
==================
Player_StopSoundChannel
==================
*/
void Player_StopSoundChannel( s16 index )
{
    soundChannel_t *entry = &g_soundChannels[index];
    if ( entry->handle == -1 )
        return;
    if ( index == SOUND_CHANNEL_FLAMETHROWER ) {
        s16 value = entry->value - SOUND_CHANNEL_FADE_STEP;
        entry->value = value;
        if ( value > 0 ) {
            Snd_Adjust( entry->handle, value, 0 );
            return;
        }
    }
    Snd_Stop( entry->handle );
    entry->handle = -1;
    entry->flags = 0;
    entry->value = 0;
}

/*
==================
Player_PlaySoundChannel
==================
*/
void Player_PlaySoundChannel( short index, short x, short y, short value )
{
    soundChannel_t *entry = &g_soundChannels[index];
    if ( entry->handle == -1 )
        entry->handle = Snd_Play( x, y, value );
    entry->flags = SOUND_CHANNEL_HOLD;
    entry->value = value;
}

/*
==================
Player_DecreaseYaw
==================
*/
void Player_DecreaseYaw( void )
{
    player_t *player = &g_player;
    if ( player->turnSpeed < player->turnSpeedMax )
        player->turnSpeed = player->turnSpeed + PLAYER_TURN_ACCELERATION;
    if ( player->turnSpeedMax < player->turnSpeed )
        player->turnSpeed = player->turnSpeedMax;
    player->angles.vy = ( player->angles.vy - player->turnSpeed ) & ANGLE_MASK;
}

/*
==================
Player_IncreaseYaw
==================
*/
void Player_IncreaseYaw( void )
{
    player_t *player = &g_player;
    if ( player->turnSpeed < player->turnSpeedMax )
        player->turnSpeed = player->turnSpeed + PLAYER_TURN_ACCELERATION;
    if ( player->turnSpeedMax < player->turnSpeed )
        player->turnSpeed = player->turnSpeedMax;
    player->angles.vy = ( player->angles.vy + player->turnSpeed ) & ANGLE_MASK;
}

/*
==================
Player_TurnStop
==================
*/
void Player_TurnStop( void )
{
    g_player.turnSpeed = 0;
}

/*
==================
Player_Accelerate
==================
*/
void Player_Accelerate( void )
{
    SVECTOR *movement = &g_player.movement;
    if ( g_player.bootsTimer == 0 && ( g_player.cell->type == CELL_TYPE_SLOW || g_player.cell->type == CELL_TYPE_LIQUID_DEEP ) ) {
        movement->vz += g_player.acceleration >> 1;
        if ( movement->vz > g_player.speedMax >> 1 )
            movement->vz = g_player.speedMax >> 1;
    } else {
        movement->vz = movement->vz + g_player.acceleration;
        if ( movement->vz > g_player.speedMax )
            movement->vz = g_player.speedMax;
    }
}

/*
==================
Player_AccelerateSlow
==================
*/
void Player_AccelerateSlow( void )
{
    SVECTOR *movement = &g_player.movement;
    if ( g_player.bootsTimer == 0 && ( g_player.cell->type == CELL_TYPE_SLOW || g_player.cell->type == CELL_TYPE_LIQUID_DEEP ) ) {
        movement->vz += g_player.acceleration >> 2;
        if ( movement->vz > g_player.speedMax >> 2 )
            movement->vz = g_player.speedMax >> 2;
    } else {
        movement->vz += g_player.acceleration >> 1;
        if ( movement->vz > g_player.speedMax >> 1 )
            movement->vz = g_player.speedMax >> 1;
    }
}

/*
==================
Player_DecreasePitch
==================
*/
void Player_DecreasePitch( void )
{
    short *pitch = &g_player.angles.vx;
    if ( g_player.pitchSpeed < g_player.pitchSpeedMax )
        g_player.pitchSpeed = g_player.pitchSpeed + PLAYER_PITCH_ACCELERATION;
    if ( g_player.pitchSpeedMax < g_player.pitchSpeed )
        g_player.pitchSpeed = g_player.pitchSpeedMax;
    *pitch = *pitch - g_player.pitchSpeed;
    if ( *pitch < g_player.pitchMin )
        *pitch = g_player.pitchMin;
}

/*
==================
Player_IncreasePitch
==================
*/
void Player_IncreasePitch( void )
{
    short *pitch = &g_player.angles.vx;
    if ( g_player.pitchSpeed < g_player.pitchSpeedMax )
        g_player.pitchSpeed = g_player.pitchSpeed + PLAYER_PITCH_ACCELERATION;
    if ( g_player.pitchSpeedMax < g_player.pitchSpeed )
        g_player.pitchSpeed = g_player.pitchSpeedMax;
    *pitch = *pitch + g_player.pitchSpeed;
    if ( *pitch > g_player.pitchMax )
        *pitch = g_player.pitchMax;
}

/*
==================
Player_RecentrePitch
==================
*/
void Player_RecentrePitch( void )
{
    short *pitch = &g_player.angles.vx;
    g_player.pitchSpeed = 0;
    Player_ApproachByEight( pitch, g_player.pitchRest );
}

/*
==================
Player_Decelerate
==================
*/
void Player_Decelerate( void )
{
    player_t *player = &g_player;
    s16       remaining = player->movement.vz;
    s16       step;
    if ( remaining > 0 ) {
        step = player->deceleration;
        if ( remaining >= step )
            player->movement.vz = remaining - step;
        else
            player->movement.vz = 0;
    }
}

/*
==================
Player_Fire

Fires the selected weapon: with ammunition left, it raises the noise, plays the
weapon's firing animation and sets game flag 0x400, then shoots; the flamethrower
keeps burning while its animation runs.
==================
*/
/* name: inferred from its body */
void Player_Fire( void )
{
    switch ( g_player.weapon ) {
    case WEAPON_PISTOL:
        if ( g_player.pistolAmmo != 0 ) {
            g_player.noiseTimer = PLAYER_NOISE_PISTOL;
            g_player.weaponAnimation = WEAPON_ANIM_FIRE;
            Player_PlayWeaponView();
            g_gameFlags |= GAME_FLAG_FLASH;
            Particle_FirePistol();
        }
        break;
    case WEAPON_SHOTGUN:
        if ( g_player.shotgunAmmo != 0 ) {
            g_player.noiseTimer = PLAYER_NOISE_SHOTGUN;
            g_player.weaponAnimation = WEAPON_ANIM_FIRE;
            Player_PlayWeaponView();
            g_gameFlags |= GAME_FLAG_FLASH;
            Particle_FireShotgun();
        }
        break;
    case WEAPON_PULSE_RIFLE:
        if ( g_player.pulseRifleAmmo != 0 ) {
            g_player.noiseTimer = PLAYER_NOISE_PULSE_RIFLE;
            g_player.weaponAnimation = WEAPON_ANIM_FIRE;
            Player_PlayWeaponView();
            g_gameFlags |= GAME_FLAG_FLASH;
            Particle_FirePulseRifle();
        }
        break;
    case WEAPON_FLAMETHROWER:
        if ( g_player.flamethrowerFuel != 0 ) {
            if ( g_player.weaponAnimation == WEAPON_ANIM_IDLE ) {
                g_player.noiseTimer = PLAYER_NOISE_FLAMETHROWER;
                g_player.weaponAnimation = WEAPON_ANIM_FIRE;
                Player_PlayWeaponView();
                g_gameFlags |= GAME_FLAG_FLASH;
            }
            Particle_FireFlamethrower();
        }
        break;
    case WEAPON_SMART_GUN:
        if ( g_player.smartGunAmmo != 0 ) {
            g_player.noiseTimer = PLAYER_NOISE_SMART_GUN;
            g_player.weaponAnimation = WEAPON_ANIM_FIRE;
            Player_PlayWeaponView();
            g_gameFlags |= GAME_FLAG_FLASH;
            Particle_FireSmartGun();
        }
        break;
    }
}

/*
==================
Player_FirePulseGrenade
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Player_FirePulseGrenade( void )
{
    if ( g_player.pulseRifleGrenades != 0 ) {
        g_player.noiseTimer = PLAYER_NOISE_GRENADE;
        if ( !CHEAT_IS_ACTIVE( CHEAT_FLAG_UNLIMITED_AMMO ) )
            g_player.pulseRifleGrenades--;
        g_player.weaponAnimation = WEAPON_ANIM_GRENADE;
        Player_PlayWeaponView();
        Particle_LaunchPulseGrenade();
    }
}

/*
==================
Player_ThrowGrenade
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Player_ThrowGrenade( void )
{
    if ( g_player.chargeGrenades != 0 ) {
        g_player.noiseTimer = PLAYER_NOISE_GRENADE;
        if ( !CHEAT_IS_ACTIVE( CHEAT_FLAG_UNLIMITED_AMMO ) )
            g_player.chargeGrenades--;
        Particle_ThrowGrenade();
        Snd_Play( SOUND_GRENADE_THROW, -1, SPU_VOLUME_MAX / 2 );
    }
}

/*
==================
Player_ClipXToCell
==================
*/
u32 Player_ClipXToCell( u32 fallback, u32 row, u32 x, u32 special )
{
    u32        adjusted;
    mapCell_t *cell;
    if ( ( special << HALFWORD_SHIFT ) != 0 )
        adjusted = ( x & ~MAP_CELL_MASK ) + ( MAP_CELL_SIZE + PLAYER_RADIUS );
    else
        adjusted = ( x & ~MAP_CELL_MASK ) - ( PLAYER_RADIUS + 1 );
    cell = g_levelMap.cells + g_levelMap.width * ( row >> MAP_CELL_SHIFT ) + ( adjusted >> MAP_CELL_SHIFT );
    if ( ( cell->light & CELL_BLOCKED ) != 0 || cell->marker != 0 )
        return fallback;
    return adjusted;
}

/*
==================
Player_MoveX

Steps x by move->vx at z and at z -/+ 0xc8 (the player's width): a cell that is
blocked, too low under its ceiling or a step too high stops x at the cell edge
(Player_ClipXToCell); each probe is pushed off the cell's diagonal walls (DiagonalClipX).
move->vy is the player's height.
==================
*/
/* name: inferred from its body */
u32 Player_MoveX( u32 x, u32 z, SVECTOR *move )
{
    short      head;
    short      negative;
    u32        probe;
    u32        target;
    u32        floor;
    mapCell_t *cell;

    head = move->vy + PLAYER_HEIGHT;
    negative = (u_short)move->vx >> HALFWORD_SIGN_SHIFT;
    target = x + move->vx;
    if ( negative )
        probe = target - PLAYER_RADIUS;
    else
        probe = target + PLAYER_RADIUS;

    floor = FloorHeight( probe, z );
    cell = MAP_CELL( probe, z );
    if ( ( ( cell->light & CELL_BLOCKED ) || cell->marker != 0 ) && !CHEAT_IS_ACTIVE( CHEAT_FLAG_NO_CLIP ) )
        return Player_ClipXToCell( x, z, probe, negative );
    if ( cell->type < CELL_TYPE_WALL || ( cell->type >= CELL_TYPE_DIAGONAL_FIRST && cell->type < CELL_TYPE_SLOPE ) ) {
        if ( (short)CeilingHeight( probe, z ) < head )
            return Player_ClipXToCell( x, z, probe, negative );
        if ( CHEAT_IS_ACTIVE( CHEAT_FLAG_FLOOR_SNAP ) )
            move->vy = floor;
        if ( move->vy < (short)floor && (short)floor - move->vy > PLAYER_STEP_HEIGHT )
            return Player_ClipXToCell( x, z, probe, negative );
    }
    probe = DiagonalClipX( probe, z );

    floor = FloorHeight( probe, z - PLAYER_RADIUS );
    cell = MAP_CELL( probe, z - PLAYER_RADIUS );
    if ( ( ( cell->light & CELL_BLOCKED ) || cell->marker != 0 ) && !CHEAT_IS_ACTIVE( CHEAT_FLAG_NO_CLIP ) )
        return Player_ClipXToCell( x, z, probe, negative );
    if ( cell->type < CELL_TYPE_WALL || ( cell->type >= CELL_TYPE_DIAGONAL_FIRST && cell->type < CELL_TYPE_SLOPE ) ) {
        if ( (short)CeilingHeight( probe, z - PLAYER_RADIUS ) < head )
            return Player_ClipXToCell( x, z, probe, negative );
        if ( CHEAT_IS_ACTIVE( CHEAT_FLAG_FLOOR_SNAP ) )
            move->vy = floor;
        if ( move->vy < (short)floor && (short)floor - move->vy > PLAYER_STEP_HEIGHT )
            return Player_ClipXToCell( x, z, probe, negative );
    }
    probe = DiagonalClipX( probe, z - PLAYER_RADIUS );

    floor = FloorHeight( probe, z + PLAYER_RADIUS );
    cell = MAP_CELL( probe, z + PLAYER_RADIUS );
    if ( ( ( cell->light & CELL_BLOCKED ) || cell->marker != 0 ) && !CHEAT_IS_ACTIVE( CHEAT_FLAG_NO_CLIP ) )
        return Player_ClipXToCell( x, z, probe, negative );
    if ( cell->type < CELL_TYPE_WALL || ( cell->type >= CELL_TYPE_DIAGONAL_FIRST && cell->type < CELL_TYPE_SLOPE ) ) {
        if ( (short)CeilingHeight( probe, z + PLAYER_RADIUS ) < head )
            return Player_ClipXToCell( x, z, probe, negative );
        if ( CHEAT_IS_ACTIVE( CHEAT_FLAG_FLOOR_SNAP ) )
            move->vy = floor;
        if ( move->vy < (short)floor && (short)floor - move->vy > PLAYER_STEP_HEIGHT )
            return Player_ClipXToCell( x, z, probe, negative );
    }
    probe = DiagonalClipX( probe, z + PLAYER_RADIUS );

    if ( negative ) {
        if ( probe < x + move->vx )
            probe = x + move->vx;
    } else {
        if ( x + move->vx < probe )
            probe = x + move->vx;
    }
    floor = (short)FloorHeight( probe, z );
    if ( (short)floor < move->vy && move->vy - (short)floor > PLAYER_STEP_HEIGHT ) {
        if ( negative ) {
            x = ( probe & ~MAP_CELL_MASK ) + ( MAP_CELL_SIZE - PLAYER_RADIUS );
            if ( x < probe )
                probe = x;
        } else {
            x = ( probe & ~MAP_CELL_MASK ) + ( PLAYER_RADIUS + 1 );
            if ( probe < x )
                probe = x;
        }
    }
    return probe;
}

/*
==================
Player_ClipZToCell
==================
*/
u32 Player_ClipZToCell( u32 column, u32 fallback, u32 z, u32 special )
{
    u32        adjusted;
    mapCell_t *cell;
    if ( ( special << HALFWORD_SHIFT ) != 0 )
        adjusted = ( z & ~MAP_CELL_MASK ) + ( MAP_CELL_SIZE + PLAYER_RADIUS );
    else
        adjusted = ( z & ~MAP_CELL_MASK ) - ( PLAYER_RADIUS + 1 );
    cell = g_levelMap.cells + g_levelMap.width * ( adjusted >> MAP_CELL_SHIFT ) + ( column >> MAP_CELL_SHIFT );
    if ( ( cell->light & CELL_BLOCKED ) != 0 || cell->marker != 0 )
        return fallback;
    return adjusted;
}

/*
==================
Player_MoveZ

Steps z by move->vz at x and at x -/+ 0xc8 (the player's width): a cell that is
blocked, too low under its ceiling or a step too high stops z at the cell edge
(Player_ClipZToCell); each probe is pushed off the cell's diagonal walls (DiagonalClipZ).
move->vy is the player's height.
==================
*/
/* name: inferred from its body */
u32 Player_MoveZ( u32 x, u32 z, SVECTOR *move )
{
    short      head;
    short      negative;
    u32        probe;
    u32        target;
    u32        floor;
    mapCell_t *cell;

    head = move->vy + PLAYER_HEIGHT;
    negative = (u_short)move->vz >> HALFWORD_SIGN_SHIFT;
    target = z + move->vz;
    if ( negative )
        probe = target - PLAYER_RADIUS;
    else
        probe = target + PLAYER_RADIUS;

    floor = FloorHeight( x, probe );
    cell = MAP_CELL( x, probe );
    if ( ( ( cell->light & CELL_BLOCKED ) || cell->marker != 0 ) && !CHEAT_IS_ACTIVE( CHEAT_FLAG_NO_CLIP ) )
        return Player_ClipZToCell( x, z, probe, negative );
    if ( cell->type < CELL_TYPE_WALL || ( cell->type >= CELL_TYPE_DIAGONAL_FIRST && cell->type < CELL_TYPE_SLOPE ) ) {
        if ( (short)CeilingHeight( x, probe ) < head )
            return Player_ClipZToCell( x, z, probe, negative );
        if ( CHEAT_IS_ACTIVE( CHEAT_FLAG_FLOOR_SNAP ) )
            move->vy = floor;
        if ( move->vy < (short)floor && (short)floor - move->vy > PLAYER_STEP_HEIGHT )
            return Player_ClipZToCell( x, z, probe, negative );
    }
    probe = DiagonalClipZ( x, probe );

    floor = FloorHeight( x - PLAYER_RADIUS, probe );
    cell = MAP_CELL( x - PLAYER_RADIUS, probe );
    if ( ( ( cell->light & CELL_BLOCKED ) || cell->marker != 0 ) && !CHEAT_IS_ACTIVE( CHEAT_FLAG_NO_CLIP ) )
        return Player_ClipZToCell( x, z, probe, negative );
    if ( cell->type < CELL_TYPE_WALL || ( cell->type >= CELL_TYPE_DIAGONAL_FIRST && cell->type < CELL_TYPE_SLOPE ) ) {
        if ( (short)CeilingHeight( x - PLAYER_RADIUS, probe ) < head )
            return Player_ClipZToCell( x, z, probe, negative );
        if ( CHEAT_IS_ACTIVE( CHEAT_FLAG_FLOOR_SNAP ) )
            move->vy = floor;
        if ( move->vy < (short)floor && (short)floor - move->vy > PLAYER_STEP_HEIGHT )
            return Player_ClipZToCell( x, z, probe, negative );
    }
    probe = DiagonalClipZ( x - PLAYER_RADIUS, probe );

    floor = FloorHeight( x + PLAYER_RADIUS, probe );
    cell = MAP_CELL( x + PLAYER_RADIUS, probe );
    if ( ( ( cell->light & CELL_BLOCKED ) || cell->marker != 0 ) && !CHEAT_IS_ACTIVE( CHEAT_FLAG_NO_CLIP ) )
        return Player_ClipZToCell( x, z, probe, negative );
    if ( cell->type < CELL_TYPE_WALL || ( cell->type >= CELL_TYPE_DIAGONAL_FIRST && cell->type < CELL_TYPE_SLOPE ) ) {
        if ( (short)CeilingHeight( x + PLAYER_RADIUS, probe ) < head )
            return Player_ClipZToCell( x, z, probe, negative );
        if ( CHEAT_IS_ACTIVE( CHEAT_FLAG_FLOOR_SNAP ) )
            move->vy = floor;
        if ( move->vy < (short)floor && (short)floor - move->vy > PLAYER_STEP_HEIGHT )
            return Player_ClipZToCell( x, z, probe, negative );
    }
    probe = DiagonalClipZ( x + PLAYER_RADIUS, probe );

    if ( negative ) {
        if ( probe < z + move->vz )
            probe = z + move->vz;
    } else {
        if ( z + move->vz < probe )
            probe = z + move->vz;
    }
    floor = (short)FloorHeight( x, probe );
    if ( (short)floor < move->vy && move->vy - (short)floor > PLAYER_STEP_HEIGHT ) {
        if ( negative ) {
            z = ( probe & ~MAP_CELL_MASK ) + ( MAP_CELL_SIZE - PLAYER_RADIUS );
            if ( z < probe )
                probe = z;
        } else {
            z = ( probe & ~MAP_CELL_MASK ) + ( PLAYER_RADIUS + 1 );
            if ( probe < z )
                probe = z;
        }
    }
    return probe;
}

/*
==================
Player_DamageHealth
==================
*/
void Player_DamageHealth( int amount, int mode )
{
    s16 callback = -1;

    if ( g_player.damageTimer == 0 && g_player.invulnerableTimer == 0 ) {
        Menu_SetDeathType( (s16)mode );
        if ( !CHEAT_IS_ACTIVE( CHEAT_FLAG_INDESTRUCTIBLE ) ) {
            g_player.health = g_player.health - amount;
            callback = SOUND_PLAYER_HURT;
            if ( g_player.health <= 0 ) {
                g_player.health = 0;
                g_gameFlags |= GAME_FLAG_PLAYER_DEAD;
            }
        }
        if ( callback != -1 )
            Snd_Play( callback, -1, SPU_VOLUME_MAX );
        g_player.damageTimer = PLAYER_DAMAGE_TIME;
    }
}

/*
==================
Player_Damage
==================
*/
void Player_Damage( int amount, int mode )
{
    s16 callback = -1;
    s16 rememberedMode = mode;
    if ( g_player.damageTimer == 0 && g_player.invulnerableTimer == 0 ) {
        if ( g_player.armour > 0 && (s16)mode == DEATH_SHOT )
            callback = SOUND_SHOT_IMPACT;
        Menu_SetDeathType( rememberedMode );
        if ( !CHEAT_IS_ACTIVE( CHEAT_FLAG_INDESTRUCTIBLE ) ) {
            if ( g_player.armour != 0 ) {
                g_player.armour -= amount;
                if ( g_player.armour <= 0 )
                    g_player.armour = 0;
            } else {
                g_player.health -= amount;
                callback = SOUND_PLAYER_HURT;
                if ( g_player.health <= 0 ) {
                    g_player.health = 0;
                    g_gameFlags |= GAME_FLAG_PLAYER_DEAD;
                }
            }
        }
        g_player.damageTimer = PLAYER_DAMAGE_TIME;
    }
    if ( callback != -1 )
        Snd_Play( callback, -1, SPU_VOLUME_MAX );
}

/*
==================
Player_Hit
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Player_Hit( int first, int second )
{
    Player_Damage( (short)first, (short)second );
}

/*
==================
Player_HitFrom
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Player_HitFrom( int x, int state, int y )
{
    short before = g_player.health;

    Player_Hit( (short)x, (short)y );
    if ( g_player.health != before ) {
        g_player.flash = PLAYER_FLASH_HIT;
        g_player.flashHeading = state;
    }
}

/*
==================
Player_Tick
==================
*/
void Player_Tick( void )
{
    s16 *first = &g_player.invulnerableTimer;
    s16 *last;
    TickTimer( first );
    TickTimer( &g_player.damageTimer );
    TickTimer( &g_player.noiseTimer );
    TickTimer( &g_player.bootsTimer );
    TickTimerExpire( &g_player.lampTimer );
    last = &g_player.nightVisionTimer;
    TickTimerExpire( last );
    Hud_DrawHealth();
    Hud_DrawAmmo();
    if ( ( g_gameFlags & GAME_FLAG_DEMO ) && !( g_gameFlags & GAME_FLAG_FADE ) && ( g_frameCount & DEMO_TEXT_BLINK ) )
        Pause_DrawHUDText( s_demo, DEMO_TEXT_X, DEMO_TEXT_Y, TEXT_WHITE );
}

/*
==================
Player_UpdateWeaponView

Steps the weapon view each frame: winds down the flamethrower's sound channel,
steps the weapon sprite (playing its sounds and returning it to the idle entry when
its sequence ends), places it under the view bob and draws it lit by the player's
cell, uploading a new frame first.
==================
*/
/* name: inferred from its body */
void Player_UpdateWeaponView( void )
{
    sprite_t       *sprite;
    spriteUV_t     *uv;
    paletteEntry_t *palette;

    if ( g_soundChannels[SOUND_CHANNEL_FLAMETHROWER].handle != -1 ) {
        if ( g_player.weapon == WEAPON_FLAMETHROWER && g_soundChannels[SOUND_CHANNEL_FLAMETHROWER].flags > 0 )
            g_soundChannels[SOUND_CHANNEL_FLAMETHROWER].flags--;
        else
            Player_StopSoundChannel( SOUND_CHANNEL_FLAMETHROWER );
    }
    if ( g_player.weaponSequence == 0 )
        return;
    sprite = &g_player.weaponSprite;
    StepSprite( sprite );
    uv = &sprite->uv;
    if ( g_player.weaponSprite.flags & SPRITE_FLAG_SOUND ) {
        if ( g_player.weaponSprite.sound == SOUND_FLAMETHROWER )
            Player_PlaySoundChannel( SOUND_CHANNEL_FLAMETHROWER, SOUND_FLAMETHROWER, -1, SOUND_NEAR_VOLUME );
        else
            Snd_Play( g_player.weaponSprite.sound, -1, SOUND_NEAR_VOLUME );
    }
    if ( sprite->flags & SPRITE_FLAG_ENDED ) {
        sprite->flags &= ~SPRITE_FLAG_ENDED;
        g_player.weaponAnimation = WEAPON_ANIM_IDLE;
        Player_PlayWeaponView();
    }
    sprite->screen.vx = WEAPON_VIEW_X;
    sprite->screen.vy = g_player.weaponLower + WEAPON_VIEW_Y - ( g_player.bob >> WEAPON_VIEW_BOB_SHIFT );
    if ( g_gameFlags & GAME_FLAG_FLASH )
        palette = g_palette;
    else
        palette = &g_palette[g_player.cell->palette];
    if ( sprite->slot->pixels != sprite->frame->pixels ) {
        if ( Tex_UploadSpriteFrame( sprite, 1 ) == 0 )
            return;
        uv->u3 = uv->u1 = sprite->frame->w - 1;
        uv->v3 = uv->v2 = sprite->slot->y + sprite->frame->h - 1;
        sprite->slot->pixels = sprite->frame->pixels;
        DrawScreenSpriteSemi( sprite, palette->colors );
    } else
        DrawScreenSpriteSemi( sprite, palette->colors );
}

/*
==================
Player_TaskClimb
==================
*/
void Player_TaskClimb( TASK *task )
{
    SVECTOR *position;
    s16      previous;
    s16      target;
    if ( g_gameFlags & GAME_FLAG_PLAYER_DEAD ) {
        task->callback = Player_TaskDeath;
        SET_VECTOR_TO_ZERO( g_player.movement );
        Player_TaskDeath( task );
        return;
    }
    Player_Controls();
    target = task->state[0];
    position = &g_player.position;
    Player_RaiseToward( &g_player.position.vy, target );
    if ( position->vy == target )
        task->callback = Player_TaskWalk;
    previous = position->vy;
    Player_Move();
    if ( position->vy < previous ) {
        task->state[0] = position->vy;
        position->vy = previous;
        task->callback = Player_TaskFall;
    } else if ( previous < position->vy ) {
        task->state[0] = position->vy;
        position->vy = previous;
        task->callback = Player_TaskClimb;
    }
    Player_ViewBob();
    Player_Tick();
    Player_UpdateWeaponView();
    g_nextTask = task->Next;
}

/*
==================
Player_TaskDeath
==================
*/
void Player_TaskDeath( TASK *task )
{
    g_player.viewHeight -= PLAYER_DEATH_SINK_STEP;
    if ( g_player.viewHeight < PLAYER_DEAD_VIEW_HEIGHT ) {
        g_player.viewHeight = PLAYER_DEAD_VIEW_HEIGHT;
        g_gameFlags |= GAME_FLAG_DEATH_DONE;
    }
    Player_RecentrePitch();
    Player_TurnStop();
    Player_ViewBob();
    g_nextTask = task->Next;
}

/* name: invented; turns the player with left/right (each ignored while the other was
 * held last frame) */
#define PLAYER_TURN()                                                      \
    if ( ( g_padHeld & PADLleft ) && !( g_padPrevious & PADLright ) )      \
        Player_DecreaseYaw();                                              \
    else if ( ( g_padHeld & PADLright ) && !( g_padPrevious & PADLleft ) ) \
        Player_IncreaseYaw();                                              \
    else                                                                   \
        Player_TurnStop()

/*
==================
Player_Controls

The controls for one frame: start pauses; a weapon change lowers and raises the
weapon view; run, strafe, look and the automatic look on slopes; turning and
walking; firing and grenades by weapon, and reloading or changing weapon when the
current one is empty.
==================
*/
/* name: inferred from its body */
void Player_Controls( void )
{
    short yaw;

    if ( g_gameFlags & GAME_FLAG_RESET )
        return;
    yaw = g_player.angles.vy & ANGLE_MASK;
    if ( ( g_padPressed & PADstart ) && g_player.weaponAnimation == WEAPON_ANIM_IDLE ) {
        g_gameFlags |= GAME_FLAG_PAUSE;
        return;
    }
    if ( g_player.weaponAnimation == WEAPON_ANIM_LOWER ) {
        g_player.weaponLower += WEAPON_VIEW_LOWER_STEP;
        if ( g_player.weaponLower > WEAPON_VIEW_LOWERED ) {
            g_player.weaponLower = WEAPON_VIEW_LOWERED;
            if ( g_player.flags & PLAYER_CHANGE_DEFAULT ) {
                Weapon_ResetDefault();
                DISABLE_FLAG( PLAYER_CHANGE_DEFAULT );
            } else if ( g_player.flags & PLAYER_CHANGE_DISCARD ) {
                Weapon_DiscardSelectNext();
                DISABLE_FLAG( PLAYER_CHANGE_DISCARD );
            } else
                Weapon_SelectNext();
            g_player.weaponAnimation = WEAPON_ANIM_RAISE;
        }
    } else if ( g_player.weaponAnimation == WEAPON_ANIM_RAISE ) {
        g_player.weaponLower -= WEAPON_VIEW_LOWER_STEP;
        if ( g_player.weaponLower == 0 )
            g_player.weaponAnimation = WEAPON_ANIM_IDLE;
    } else if ( !( g_buttonLook & g_padHeld ) && ( g_buttonWeapon & g_padPressed )
                && g_player.weaponAnimation == WEAPON_ANIM_IDLE && Weapon_SlotOtherOwned() )
        g_player.weaponAnimation = WEAPON_ANIM_LOWER;
    if ( g_buttonRun & g_padHeld ) {
        g_player.flags |= PLAYER_RUN;
        g_player.speedMax = PLAYER_RUN_SPEED;
        g_player.acceleration = PLAYER_RUN_ACCELERATION;
        g_player.deceleration = PLAYER_RUN_DECELERATION;
        g_player.turnSpeedMax = PLAYER_RUN_TURN_SPEED;
    } else {
        DISABLE_FLAG( PLAYER_RUN );
        g_player.speedMax = PLAYER_WALK_SPEED;
        g_player.acceleration = PLAYER_WALK_ACCELERATION;
        g_player.deceleration = PLAYER_WALK_DECELERATION;
        g_player.turnSpeedMax = PLAYER_WALK_TURN_SPEED;
    }
    if ( ( g_buttonStrafeLeft & g_padHeld ) || ( g_buttonStrafeRight & g_padHeld ) )
        g_player.flags |= PLAYER_STRAFE;
    else
        DISABLE_FLAG( PLAYER_STRAFE );
    if ( g_buttonLook & g_padHeld ) {
        if ( g_buttonUse & g_padHeld )
            Player_DecreasePitch();
        else if ( g_buttonWeapon & g_padHeld )
            Player_IncreasePitch();
    } else if ( g_player.cell->type < CELL_TYPE_SLOPE )
        Player_RecentrePitch();
    else {
        switch ( g_player.cell->type ) {
        case CELL_TYPE_SLOPE_AT( SLOPE_BAND_STEP, SLOPE_RISES_POS_Z ):
        case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP, SLOPE_RISES_POS_Z ):
        case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP_ALT, SLOPE_RISES_POS_Z ):
            if ( yaw <= ANGLE_45 )
                Player_DecreasePitch();
            else if ( yaw >= ANGLE_135 && yaw <= ANGLE_225 )
                Player_IncreasePitch();
            else
                Player_RecentrePitch();
            break;
        case CELL_TYPE_SLOPE_AT( SLOPE_BAND_STEP, SLOPE_RISES_NEG_X ):
        case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP, SLOPE_RISES_NEG_X ):
        case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP_ALT, SLOPE_RISES_NEG_X ):
            if ( yaw >= ANGLE_225 && yaw <= ANGLE_315 )
                Player_IncreasePitch();
            else if ( yaw >= ANGLE_45 && yaw <= ANGLE_135 )
                Player_DecreasePitch();
            else
                Player_RecentrePitch();
            break;
        case CELL_TYPE_SLOPE_AT( SLOPE_BAND_STEP, SLOPE_RISES_POS_X ):
        case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP, SLOPE_RISES_POS_X ):
        case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP_ALT, SLOPE_RISES_POS_X ):
            if ( yaw >= ANGLE_225 && yaw <= ANGLE_315 )
                Player_DecreasePitch();
            else if ( yaw >= ANGLE_45 && yaw <= ANGLE_135 )
                Player_IncreasePitch();
            else
                Player_RecentrePitch();
            break;
        case CELL_TYPE_SLOPE_AT( SLOPE_BAND_STEP, SLOPE_RISES_NEG_Z ):
        case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP, SLOPE_RISES_NEG_Z ):
        case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP_ALT, SLOPE_RISES_NEG_Z ):
            if ( yaw <= ANGLE_45 )
                Player_IncreasePitch();
            else if ( yaw >= ANGLE_135 && yaw <= ANGLE_225 )
                Player_DecreasePitch();
            else
                Player_RecentrePitch();
            break;
        default:
            Player_RecentrePitch();
            break;
        }
    }
    if ( g_player.flags & PLAYER_STRAFE ) {
        PLAYER_TURN();
        if ( ( g_padHeld & PADLup ) && ( g_buttonStrafeLeft & g_padHeld ) ) {
            Player_AccelerateSlow();
            g_player.rotation.vy = ( g_player.angles.vy - ANGLE_45 ) & ANGLE_MASK;
        } else if ( ( g_padHeld & PADLup ) && ( g_buttonStrafeRight & g_padHeld ) ) {
            Player_AccelerateSlow();
            g_player.rotation.vy = ( g_player.angles.vy + ANGLE_45 ) & ANGLE_MASK;
        } else if ( ( g_padHeld & PADLdown ) && ( g_buttonStrafeLeft & g_padHeld ) ) {
            Player_AccelerateSlow();
            g_player.rotation.vy = ( g_player.angles.vy + ANGLE_225 ) & ANGLE_MASK;
        } else if ( ( g_padHeld & PADLdown ) && ( g_buttonStrafeRight & g_padHeld ) ) {
            Player_AccelerateSlow();
            g_player.rotation.vy = ( g_player.angles.vy + ANGLE_135 ) & ANGLE_MASK;
        } else if ( g_buttonStrafeLeft & g_padHeld ) {
            Player_AccelerateSlow();
            g_player.rotation.vy = ( g_player.angles.vy - ANGLE_90 ) & ANGLE_MASK;
        } else if ( g_buttonStrafeRight & g_padHeld ) {
            Player_AccelerateSlow();
            g_player.rotation.vy = ( g_player.angles.vy + ANGLE_90 ) & ANGLE_MASK;
        } else
            Player_Decelerate();
    } else {
        PLAYER_TURN();
        if ( g_padHeld & PADLup ) {
            Player_Accelerate();
            g_player.rotation.vy = g_player.angles.vy;
        } else if ( g_padHeld & PADLdown ) {
            Player_AccelerateSlow();
            g_player.rotation.vy = ( g_player.angles.vy + ANGLE_180 ) & ANGLE_MASK;
        } else
            Player_Decelerate();
    }
    switch ( g_player.weapon ) {
    case WEAPON_PISTOL:
    case WEAPON_SHOTGUN:
        if ( g_player.weaponAnimation != WEAPON_ANIM_IDLE )
            return;
        if ( g_buttonFire & g_padPressed )
            Player_Fire();
        else if ( g_buttonGrenade & g_padPressed )
            Player_ThrowGrenade();
        break;
    case WEAPON_FLAMETHROWER:
        if ( g_player.weaponAnimation == WEAPON_ANIM_LOWER || g_player.weaponAnimation == WEAPON_ANIM_RAISE )
            break;
        if ( g_buttonFire & g_padHeld )
            Player_Fire();
        else if ( g_buttonGrenade & g_padPressed )
            Player_ThrowGrenade();
        break;
    case WEAPON_SMART_GUN:
        if ( g_player.weaponAnimation != WEAPON_ANIM_IDLE )
            return;
        if ( g_buttonFire & g_padHeld )
            Player_Fire();
        else if ( g_buttonGrenade & g_padPressed )
            Player_ThrowGrenade();
        break;
    case WEAPON_PULSE_RIFLE:
        if ( g_player.weaponAnimation != WEAPON_ANIM_IDLE )
            return;
        if ( g_buttonFire & g_padHeld )
            Player_Fire();
        else if ( g_buttonGrenade & g_padPressed )
            Player_FirePulseGrenade();
        break;
    }
    if ( g_player.weaponAnimation != WEAPON_ANIM_IDLE )
        return;
    switch ( g_player.weapon ) {
    case WEAPON_PISTOL:
        if ( g_player.pistolAmmo != 0 )
            return;
        if ( g_player.pistolClips != 0 ) {
            g_player.weaponAnimation = WEAPON_ANIM_RELOAD;
            g_player.pistolAmmo = PISTOL_CLIP_ROUNDS;
            g_player.pistolClips--;
            Player_PlayWeaponView();
        } else if ( !Weapon_SlotOtherOwned() ) {
            g_player.weaponAnimation = WEAPON_ANIM_RELOAD;
            g_player.pistolAmmo = 1;
            Player_PlayWeaponView();
        } else {
            g_player.weaponAnimation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DISCARD;
        }
        break;
    case WEAPON_PULSE_RIFLE:
        if ( g_player.pulseRifleAmmo != 0 )
            return;
        if ( g_player.pulseRifleClips != 0 ) {
            g_player.weaponAnimation = WEAPON_ANIM_RELOAD;
            g_player.pulseRifleAmmo = PULSE_RIFLE_CLIP_ROUNDS;
            g_player.pulseRifleClips--;
            Player_PlayWeaponView();
        } else if ( g_player.pulseRifleGrenades == 0 ) {
            g_player.weaponAnimation = WEAPON_ANIM_LOWER;
            if ( Weapon_SlotOtherOwned() )
                g_player.flags |= PLAYER_CHANGE_DISCARD;
            else if ( g_player.pistolAmmo == 0 ) {
                g_player.pistolAmmo = 1;
                g_player.flags |= PLAYER_CHANGE_DEFAULT;
            }
        }
        break;
    case WEAPON_SMART_GUN:
        if ( g_player.smartGunAmmo != 0 )
            return;
        if ( g_player.smartGunClips != 0 ) {
            g_player.weaponAnimation = WEAPON_ANIM_RELOAD;
            g_player.smartGunAmmo = SMART_GUN_CLIP_ROUNDS;
            g_player.smartGunClips--;
            Player_PlayWeaponView();
        } else if ( Weapon_SlotOtherOwned() ) {
            g_player.weaponAnimation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DISCARD;
        } else {
            if ( g_player.pistolAmmo == 0 )
                g_player.pistolAmmo = 1;
            g_player.weaponAnimation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DEFAULT;
        }
        break;
    case WEAPON_SHOTGUN:
        if ( g_player.shotgunAmmo != 0 )
            return;
        if ( Weapon_SlotOtherOwned() ) {
            g_player.weaponAnimation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DISCARD;
        } else {
            if ( g_player.pistolAmmo == 0 )
                g_player.pistolAmmo = 1;
            g_player.weaponAnimation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DEFAULT;
        }
        break;
    case WEAPON_FLAMETHROWER:
        if ( g_player.flamethrowerFuel != 0 )
            return;
        if ( Weapon_SlotOtherOwned() ) {
            g_player.weaponAnimation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DISCARD;
        } else {
            if ( g_player.pistolAmmo == 0 )
                g_player.pistolAmmo = 1;
            g_player.weaponAnimation = WEAPON_ANIM_LOWER;
            g_player.flags |= PLAYER_CHANGE_DEFAULT;
        }
        break;
    }
}

/*
==================
Player_TaskWalk
==================
*/
void Player_TaskWalk( TASK *task )
{
    s16 *y;
    s16  previous;
    if ( g_gameFlags & GAME_FLAG_PLAYER_DEAD ) {
        task->callback = Player_TaskDeath;
        SET_VECTOR_TO_ZERO( g_player.movement );
        Player_TaskDeath( task );
        return;
    }
    Player_Controls();
    y = &g_player.position.vy;
    previous = *y;
    Player_Move();
    if ( *y < previous ) {
        task->state[0] = *y;
        *y = previous;
        task->state[1] = previous;
        task->callback = Player_TaskFall;
    } else if ( previous < *y ) {
        task->state[0] = *y;
        *y = previous;
        task->callback = Player_TaskClimb;
    }
    Player_ViewBob();
    Player_Tick();
    Player_UpdateWeaponView();
    g_nextTask = task->Next;
}

/*
==================
Player_WeaponEntry

The sequence entry the weapon view plays.
==================
*/
/* name: invented */
static inline trackEntry_t *Player_WeaponEntry( player_t *player )
{
    return &player->weaponSequence[player->weaponAnimation];
}

/*
==================
Player_PlayWeaponView

Restarts the weapon sprite on its current sequence entry.
==================
*/
/* name: inferred from its body */
void Player_PlayWeaponView( void )
{
    trackEntry_t  *entry;
    texturePage_t *slot;
    u_short       *stream;
    u_short        duration;

    if ( g_player.weaponSequence != 0 ) {
        entry = Player_WeaponEntry( &g_player );
        g_player.weaponSprite.frames = entry->frames;
        g_player.weaponSprite.frame = 0;
        stream = entry->stream;
        g_player.weaponSprite.remaining = 1;
        duration = *stream;
        slot = g_player.weaponSprite.slot;
        stream += 2;
        g_player.weaponSprite.loop = (short *)stream;
        g_player.weaponSprite.script = (short *)( stream - 1 );
        g_player.weaponSprite.repeat = 0;
        g_player.weaponSprite.flags = 0;
        g_player.weaponSprite.uv.u0 = 0;
        g_player.weaponSprite.duration = duration;
        g_player.weaponSprite.uv.v0 = slot->y;
        g_player.weaponSprite.uv.clut = g_player.weaponSprite.clut->clut;
        g_player.weaponSprite.uv.v1 = slot->y;
        g_player.weaponSprite.uv.tpage = slot->tpage;
        g_player.weaponSprite.uv.u2 = 0;
        slot->pixels = 0;
        StepSprite( &g_player.weaponSprite );
    }
}

/*
==================
Player_SelectWeapon
==================
*/
/* name: inferred from its body, which stores player_t.weapon and sets up the weapon sprite */
void Player_SelectWeapon( int mode )
{
    g_player.weaponAnimation = WEAPON_ANIM_IDLE;
    g_player.weaponSprite.flags = 0;
    g_player.weapon = mode;
    switch ( (s16)mode ) {
    case WEAPON_PISTOL:
        g_player.weaponSequence = g_pistolSequence;
        g_player.weaponSprite.clut = g_pistolCLUT;
        break;
    case WEAPON_SHOTGUN:
        g_player.weaponSequence = g_shotgunSequence;
        g_player.weaponSprite.clut = g_shotgunCLUT;
        break;
    case WEAPON_PULSE_RIFLE:
        g_player.weaponSequence = g_pulseRifleSequence;
        g_player.weaponSprite.clut = g_pulseRifleCLUT;
        break;
    case WEAPON_FLAMETHROWER:
        g_player.weaponSequence = g_flamethrowerSequence;
        g_player.weaponSprite.clut = g_flamethrowerCLUT;
        break;
    case WEAPON_SMART_GUN:
        g_player.weaponSequence = g_smartGunSequence;
        g_player.weaponSprite.clut = g_smartGunCLUT;
        break;
    default:
        g_player.weaponSequence = 0;
        break;
    }
    Player_PlayWeaponView();
}

/*
==================
Player_BindWeaponSpriteSlot
==================
*/
/* name: inferred from its body, which gives the weapon sprite the first VRAM slot and marks it used and locked */
texturePage_t *Player_BindWeaponSpriteSlot( void )
{
    player_t      *player = &g_player;
    texturePage_t *slot = g_pageSets[PAGE_SET_SPRITES].pages;
    texturePage_t *result;
    player->weaponSprite.slot = slot;
    slot->users = 1;
    result = player->weaponSprite.slot;
    result->reserved = 1;
    return result;
}

/*
==================
Player_TaskStart

The player's first frame on a level: places the player on the level's start cell,
facing its start direction, resets the view, timers and tallies, selects the
current weapon and continues as Player_TaskWalk.
==================
*/
void Player_TaskStart( TASK *task )
{
    short    i;
    u_int    x;
    u_int    z;
    SVECTOR *v;
    short    index;

    for ( i = 0; i < SOUND_CHANNEL_COUNT; i++ ) {
        g_soundChannels[i].handle = -1;
        g_soundChannels[i].flags = 0;
        g_soundChannels[i].value = 0;
    }
    v = &g_player.position;
    v->vx = ( g_levelMap.startX << MAP_CELL_SHIFT ) - g_levelMap.offsetX;
    v->vz = ( g_levelMap.startZ << MAP_CELL_SHIFT ) - g_levelMap.offsetZ;
    v->vy = FloorHeight( v->vx + g_levelMap.offsetX, v->vz + g_levelMap.offsetZ );
    x = v->vx + g_levelMap.offsetX;
    z = v->vz + g_levelMap.offsetZ;
    v = &g_player.angles;
    g_player.cell = MAP_CELL( x, z );
    g_player.cell->marker = CELL_MARKER_PLAYER;
    index = g_player.cell->layoutIndex;
    g_player.layoutIndex = index;
    FillFlowField( &g_levelMap.layoutRecords[index], index );
    v->vx = PLAYER_PITCH_REST;
    v->vz = ANGLE_180;
    switch ( g_levelMap.startFacing ) {
    case FACING_NEG_Z:
        v->vy = ANGLE_180;
        break;
    case FACING_POS_X_NEG_Z:
        v->vy = ANGLE_225;
        break;
    case FACING_POS_X:
        v->vy = -ANGLE_90;
        break;
    case FACING_POS_X_POS_Z:
        v->vy = -ANGLE_45;
        break;
    case FACING_POS_Z:
        v->vy = 0;
        break;
    case FACING_NEG_X_POS_Z:
        v->vy = ANGLE_45;
        break;
    case FACING_NEG_X:
        v->vy = ANGLE_90;
        break;
    case FACING_NEG_X_NEG_Z:
        v->vy = ANGLE_135;
        break;
    }
    v = &g_player.rotation;
    v->vx = 0;
    v->vy = 0;
    v->vz = ANGLE_180;
    v = &g_player.movement;
    SET_VECTOR_TO_ZERO( *v );
    g_player.batteries = 0;
    g_player.lampCharges = 0;
    g_player.lampTimer = 0;
    g_player.nightVisionCharges = 0;
    g_player.nightVisionTimer = 0;
    g_player.bootsTimer = 0;
    g_player.identityTags = 0;
    g_player.invulnerableTimer = 0;
    DISABLE_FLAG( PLAYER_HAS_AUTO_MAPPER | PLAYER_HUGGER_ATTACHED | PLAYER_FOOTSTEP_ALT );
    for ( i = 0; i < PLAYER_TALLY_COUNT; i++ )
        g_player.tallies[i] = 0;
    g_player.noiseTimer = 0;
    g_player.damageTimer = 0;
    g_player.flash = 0;
    g_player.flashHeading = 0;
    g_player.pitchRest = PLAYER_PITCH_REST;
    g_player.pitchMax = PLAYER_PITCH_LIMIT;
    g_player.pitchMin = -PLAYER_PITCH_LIMIT;
    g_player.pitchSpeedMax = PLAYER_PITCH_SPEED_MAX;
    g_player.pitchSpeed = 0;
    g_player.speedMax = PLAYER_WALK_SPEED;
    g_player.acceleration = PLAYER_WALK_ACCELERATION;
    g_player.deceleration = PLAYER_WALK_DECELERATION;
    g_player.turnSpeedMax = PLAYER_WALK_TURN_SPEED;
    g_player.turnSpeed = 0;
    g_player.viewHeight = PLAYER_HEIGHT;
    g_player.shotOffsetX = 0;
    g_player.bob = 0;
    g_player.unknown_102 = 1;
    g_player.bobPhase = 2;
    g_player.lastTrigger = 0;
    g_player.weaponLower = 0;
    Player_ViewBob();
    Player_SelectWeapon( g_player.weapon );
    Weapon_SyncSlot();
    task->callback = Player_TaskWalk;
    g_nextTask = task->Next;
}

/*
==================
Player_Reset
==================
*/
/* name: inferred from its body, which resets the player's state for a level (health 100) */
void Player_Reset( void )
{
    s16 i;
    Weapon_ResetSlots();
    g_player.flags = 0;
    g_player.health = PLAYER_HEALTH_MAX;
    g_player.armour = 0;
    g_player.chargeGrenades = 0;
    g_player.pistolAmmo = 0;
    g_player.pistolClips = 0;
    g_player.shotgunAmmo = 0;
    g_player.pulseRifleAmmo = 0;
    g_player.pulseRifleClips = 0;
    g_player.pulseRifleGrenades = 0;
    g_player.flamethrowerFuel = 0;
    g_player.smartGunAmmo = 0;
    g_player.smartGunClips = 0;
    g_player.weapon = WEAPON_PISTOL;
    for ( i = 0; i < SOUND_CHANNEL_COUNT; ++i ) {
        g_soundChannels[i].handle = -1;
        g_soundChannels[i].flags = 0;
        g_soundChannels[i].value = 0;
    }
    Weapon_PlayerGivePickup( PICKUP_PISTOL, PISTOL_CLIP_ROUNDS, PLAYER_START_PISTOL_CLIPS );
}
