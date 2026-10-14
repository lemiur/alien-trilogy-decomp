/* map_objects.c -- map objects: the object pool, spawning, pickup models and dropped items */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <LEVEL.H>
#include <MODEL.H>
#include <CAMERA.H>
#include <VIEW.H>
#include <SPRITEUV.H>
#include <TEXTURE.H>
#include <TASK.H>
#include <AUDIO.H>
#include <HEIGHT.H>
#include <LIBCD.H>
#include <HEAPFILE.H>
#include <HEAP.H>
#include <TEXT.H>
#include <HUD.H>
#include <TRACK.H>
#include <SPRITE.H>
#include <ACTOR.H>
#include <GAME.H>
#include <ITEMS.H>

extern mapObject_t *g_mapObjectPool;
extern mapObject_t *g_mapObjectFree;
extern mapObject_t *g_mapObjects;
/* ScaleMatrix scale of each object kind (Object_Draw). name: invented */
static VECTOR g_mapObjectScales[] = {
    { 0x1000, 0x1000, 0x1000, 0 }, { 0x1000, 0x1000, 0x1000, 0 }, { 0x1000, 0x1000, 0x1000, 0 },
    { 0x1000, 0x1000, 0x1000, 0 }, { 0x1400, 0x1400, 0x1400, 0 }, { 0x400, 0x400, 0x400, 0 },
    { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 },
    { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 },
    { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 },
    { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0xc00, 0xc00, 0xc00, 0 },
    { 0x2c00, 0x2c00, 0x2c00, 0 }, { 0x2c00, 0x2c00, 0x2c00, 0 }, { 0x2800, 0x2800, 0x2800, 0 },
    { 0x2000, 0x2000, 0x2000, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 },
    { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 },
    { 0x1c00, 0x1c00, 0x1c00, 0 }
};
/* name: invented; Object_Spawn keeps the level's object records for Object_Trigger */
extern objectRecord_t *g_objectRecords;
/* name: invented; the number of g_objectRecords */
extern short g_objectRecordCount;
extern void Object_Move( mapObject_t *object );
/* name: inferred from use: the HUD message per pickup kind ("9mm automatic", ...) */
/* name: analysis/runtime/globals.json */
static char *g_pickupMessages[] = { "9mm automatic",
                                    "Shotgun",
                                    "Pulse rifle",
                                    "Flamethrower",
                                    "Smart Gun",
                                    "NOT USED",
                                    "Seismic charges",
                                    "Battery",
                                    "Night vision goggles",
                                    "9mm ammunition clip",
                                    "Shotgun cartridges",
                                    "Pulse rifle clip",
                                    "Grenades",
                                    "Flamethower fuel",
                                    "Smartgun ammunition",
                                    "Identity tag",
                                    "Auto mapper",
                                    "Hypo Pack",
                                    "Acid vest",
                                    "Body suit",
                                    "MediKit",
                                    "DermPatch",
                                    "Protective boots",
                                    "Adreneline burst",
                                    "NOT USED",
                                    "Shoulder lamp",
                                    "NOT USED",
                                    "NOT USED" };
/* name: invented; height of each object kind above the floor */
static u_short g_objectHeights[] = { 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                                     64, 64, 64, 64, 64, 64, 96, 64, 64, 64, 64, 64, 64, 64 };
extern void Object_Unlink( mapObject_t *node );
extern int Object_NearPlayer( mapObject_t *object );
extern void Object_Draw( mapObject_t *object );

/*
==================
Object_Free
==================
*/
/* name: inferred from its body, which pushes an object onto the free list */
void Object_Free( mapObject_t *node )
{
    node->next = g_mapObjectFree;
    g_mapObjectFree = node;
}

/*
==================
Object_Reset
==================
*/
/* name: inferred from its body, which threads the 128-object pool onto the free list */
void Object_Reset( void )
{
    mapObject_t *node;
    int          i;

    g_mapObjectFree = 0;
    g_mapObjects = 0;
    node = g_mapObjectPool;
    for ( i = 0; i < MAP_OBJECT_MAX; ++i ) {
        Object_Free( node );
        ++node;
    }
}

/*
==================
Object_FixupModelFaces

The face kind is the halfword over flags and selector until this splits it.
==================
*/
/* name: inferred from its body (Door_FixupModelFaces with palette entry 10): turns an
 * object model's vertex indices into pointers and each face kind into flags and a
 * dispatch selector */
void Object_FixupModelFaces( modelRow_t *face, SVECTOR *vertices )
{
    int   index;
    short last;

    do {
        index = face->file.vertices[QUAD_VERTEX_TOP_LEFT];
        face->row.vertices[QUAD_VERTEX_TOP_LEFT] = vertices + index;
        index = face->file.vertices[QUAD_VERTEX_TOP_RIGHT];
        face->row.vertices[QUAD_VERTEX_TOP_RIGHT] = vertices + index;
        index = face->file.vertices[QUAD_VERTEX_BOTTOM_RIGHT];
        face->row.vertices[QUAD_VERTEX_BOTTOM_RIGHT] = vertices + index;
        index = face->file.vertices[QUAD_VERTEX_BOTTOM_LEFT];
        face->row.vertices[QUAD_VERTEX_BOTTOM_LEFT] = vertices + index;
        last = face->file.kind & ROW_FLAG_LAST;
        switch ( (short)( face->file.kind & ~ROW_FLAG_LAST ) ) {
        case FACE_KIND_QUAD:
        case FACE_KIND_QUAD_OTHER:
            face->row.selector = ROW_SELECT_QUAD;
            break;
        case FACE_KIND_QUAD_MIRROR:
            face->row.selector = ROW_SELECT_QUAD_ALT;
            break;
        case FACE_KIND_TRIANGLE ... FACE_KIND_TRIANGLE_LAST:
            face->row.selector = ROW_SELECT_TRI;
            break;
        default:
            face->row.selector = ROW_SELECT_TRI;
            break;
        }
        face->row.flags = last ? ROW_FLAG_LAST | MODEL_PALETTE : MODEL_PALETTE;
    } while ( !( face++->row.flags & ROW_FLAG_LAST ) );
}

/*
==================
Object_ReadNumber
==================
*/
/* name: invented; reads one of the BND file's decimal fields */
static inline short Object_ReadNumber( u_char *text )
{
    return Text_ParseDecimal( text );
}

/*
==================
Object_LoadPickupModels
==================
*/
/* name: inferred from its body, which loads \GFX\PICKMOD.BND (g_fileTable[FILE_PICKMOD_BND]) into g_objectModels */
void Object_LoadPickupModels( void )
{
    short       i;
    short       count;
    short       index;
    u_int       size;
    u_char     *p;
    modelRow_t *rows;
    u_char      text[BND_COUNT_DIGITS + 1];

    g_mapObjectPool = Heap_Alloc( MAP_OBJECT_MAX * sizeof( mapObject_t ) );
    for ( i = 0; i < PICKUP_KIND_COUNT; i++ )
        g_objectModels[i] = 0;
    Sys_LoadFile( &g_fileTable[FILE_PICKMOD_BND] );
    p = g_fileTable[FILE_PICKMOD_BND].buffer;
    p += BND_HEADER_SIZE;
    text[0] = *p++;
    text[1] = *p++;
    text[2] = *p++;
    text[BND_COUNT_DIGITS - 1] = *p++;
    text[BND_COUNT_DIGITS] = 0;
    count = Object_ReadNumber( text );
    for ( i = 0; i < count; i++ ) {
        p++;
        text[0] = *p++;
        text[1] = *p++;
        text[2] = *p++;
        text[BND_INDEX_DIGITS] = 0;
        index = Object_ReadNumber( text );
        size = Text_ReadBigEndian32( p );
        p += BND_LENGTH_SIZE;
        if ( size & 1 )
            size++;
        rows = ( (modelBlock_t *)p )->rows;
        Object_FixupModelFaces( rows, (SVECTOR *)( rows + ( (modelBlock_t *)p )->rowCount ) );
        p += size;
        g_objectModels[index] = &rows->row;
    }
}

/*
==================
Object_Alloc
==================
*/
mapObject_t *Object_Alloc( void )
{
    mapObject_t *first;

    if ( ( first = g_mapObjectFree ) )
        g_mapObjectFree = first->next;

    return first;
}

/*
==================
Object_Link
==================
*/
void Object_Link( mapObject_t *node )
{
    mapObject_t *old = g_mapObjects;
    if ( old )
        old->prev = node;
    g_mapObjects = node;
    node->next = old;
    node->prev = 0;
}

/* Multi-statement macro (name invented): takes a pool object and places a record's object on
 * its floor cell. A macro, not an inline function: the inline spelling compiles Object_Spawn
 * 20 bytes longer than retail. */
#define MAP_OBJECT_SPAWN( record )                                                                                              \
    {                                                                                                                           \
        mapObject_t *object = Object_Alloc();                                                                                   \
        u_int        x, z; /* map position of the object, for MAP_CELL */                                                       \
        object->kind = ( record )->kind;                                                                                        \
        if ( object->kind == PICKUP_UNUSED_24 )                                                                                 \
            object->kind = PICKUP_DERM_PATCH;                                                                                   \
        object->amount = ( record )->amount;                                                                                    \
        object->clips = ( record )->clips;                                                                                      \
        object->position.vx = ( ( record )->x << MAP_CELL_SHIFT ) + MAP_CELL_HALF - g_levelMap.offsetX;                         \
        object->position.vz = ( ( record )->z << MAP_CELL_SHIFT ) + MAP_CELL_HALF - g_levelMap.offsetZ;                         \
        object->position.vy = FloorHeight( object->position.vx + g_levelMap.offsetX, object->position.vz + g_levelMap.offsetZ ) \
                              + g_objectHeights[object->kind];                                                                  \
        x = object->position.vx + g_levelMap.offsetX;                                                                           \
        z = object->position.vz + g_levelMap.offsetZ;                                                                           \
        object->cell = MAP_CELL( x, z );                                                                                        \
        object->rotation.vx = 0;                                                                                                \
        object->rotation.vy = 0;                                                                                                \
        object->rotation.vz = 0;                                                                                                \
        object->velocity.vx = 0;                                                                                                \
        object->velocity.vy = 0;                                                                                                \
        object->velocity.vz = 0;                                                                                                \
        object->model = g_objectModels[object->kind];                                                                           \
        Object_Link( object );                                                                                                  \
    }

/*
==================
Object_Spawn
==================
*/
/* name: inferred from its body, which keeps the level's object records and spawns the ones needing no trigger */
void Object_Spawn( objectRecord_t *records, short count )
{
    g_objectRecords = records;
    g_objectRecordCount = count;
    while ( count-- > 0U ) {
        records->triggers = 0;
        if ( records->kind == PICKUP_IDENTITY_TAG )
            g_levelMap.kindCounts[TALLY_IDENTITY_TAGS]++;
        if ( records->required == 0 )
            MAP_OBJECT_SPAWN( records );
        records++;
    }
}

/*
==================
Object_Init
==================
*/
/* name: inferred from its body, which resets the map object pool and spawns the level's objects */
void Object_Init( void )
{
    Object_Reset();
    if ( g_levelMap.objectRecordCount )
        Object_Spawn( g_levelMap.objectRecords, g_levelMap.objectRecordCount );
}

/*
==================
Object_Trigger
==================
*/
/* name: inferred from its body, which raises a record's trigger count and spawns its object when it is reached */
void Object_Trigger( int amount, u_char index )
{
    objectRecord_t *record = &g_objectRecords[index];
    record->triggers += amount;
    if ( record->triggers > record->required )
        record->triggers = record->required;
    if ( record->triggers == record->required )
        MAP_OBJECT_SPAWN( record );
}

/*
==================
Object_ActorDropItem

Object_ActorDropItem raises the trigger count of the actor's object record and throws its object out
of the actor
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Object_ActorDropItem( actor_t *actor )
{
    objectRecord_t *record;
    mapObject_t    *object;
    u_int           x, z;

    if ( g_objectRecordCount == 0 )
        return;
    record = &g_objectRecords[actor->attachedIndex];
    record->triggers += 1;
    if ( record->triggers > record->required )
        record->triggers = record->required;
    if ( record->triggers != record->required )
        return;
    object = Object_Alloc();
    object->kind = record->kind;
    if ( object->kind == PICKUP_UNUSED_24 )
        object->kind = PICKUP_DERM_PATCH;
    object->amount = record->amount;
    object->clips = record->clips;
    object->position.vx = actor->position.vx;
    object->position.vz = actor->position.vz;
    object->position.vy =
        FloorHeight( object->position.vx + g_levelMap.offsetX, object->position.vz + g_levelMap.offsetZ ) + THROW_DROP_HEIGHT;
    x = object->position.vx + g_levelMap.offsetX;
    z = object->position.vz + g_levelMap.offsetZ;
    object->cell = MAP_CELL( x, z );
    switch ( actor->direction ) {
    case FACING_NEG_Z:
        object->velocity.vx = 0;
        object->velocity.vz = -( ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN );
        break;
    case FACING_POS_X_NEG_Z:
        object->velocity.vx = ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN;
        object->velocity.vz = -( ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN );
        break;
    case FACING_POS_X:
        object->velocity.vx = ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN;
        object->velocity.vz = 0;
        break;
    case FACING_POS_X_POS_Z:
        object->velocity.vx = ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN;
        object->velocity.vz = ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN;
        break;
    case FACING_POS_Z:
        object->velocity.vx = 0;
        object->velocity.vz = ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN;
        break;
    case FACING_NEG_X_POS_Z:
        object->velocity.vx = -( ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN );
        object->velocity.vz = ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN;
        break;
    case FACING_NEG_X:
        object->velocity.vx = -( ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN );
        object->velocity.vz = 0;
        break;
    case FACING_NEG_X_NEG_Z:
        object->velocity.vx = -( ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN );
        object->velocity.vz = -( ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN );
        break;
    }
    /* (u_short): retail adds the jitter through the constant -THROW_JITTER_BIAS as a halfword */
    object->velocity.vx += ( (u_short)Sys_Random() & THROW_JITTER_MASK ) - THROW_JITTER_BIAS;
    object->velocity.vz += ( (u_short)Sys_Random() & THROW_JITTER_MASK ) - THROW_JITTER_BIAS;
    object->velocity.vy = THROW_LIFT_SPEED;
    object->bounceSpeed = THROW_BOUNCE_SPEED;
    SET_VECTOR_TO_ZERO( object->rotation );
    object->model = g_objectModels[object->kind];
    Object_Link( object );
}

/*
==================
Object_LevelObjectDropItem

Object_LevelObjectDropItem raises the trigger count of a destroyed level object's drop record and
throws its object out
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Object_LevelObjectDropItem( levelObject_t *source, u_char index )
{
    objectRecord_t *record;
    mapObject_t    *object;
    u_int           x, z;

    if ( g_objectRecordCount == 0 )
        return;
    record = &g_objectRecords[index];
    record->triggers += 1;
    if ( record->triggers > record->required )
        record->triggers = record->required;
    if ( record->triggers != record->required )
        return;
    object = Object_Alloc();
    object->kind = record->kind;
    if ( object->kind == PICKUP_UNUSED_24 )
        object->kind = PICKUP_DERM_PATCH;
    object->amount = record->amount;
    object->clips = record->clips;
    object->position.vx = source->position.vx;
    object->position.vz = source->position.vz;
    object->position.vy = FloorHeight( object->position.vx + g_levelMap.offsetX, object->position.vz + g_levelMap.offsetZ )
                          + g_objectHeights[object->kind];
    x = object->position.vx + g_levelMap.offsetX;
    z = object->position.vz + g_levelMap.offsetZ;
    object->cell = MAP_CELL( x, z );
    switch ( source->facing ) {
    case FACING_NEG_Z:
        object->velocity.vx = 0;
        object->velocity.vz = -( ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN );
        break;
    case FACING_POS_X_NEG_Z:
        object->velocity.vx = ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN;
        object->velocity.vz = -( ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN );
        break;
    case FACING_POS_X:
        object->velocity.vx = ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN;
        object->velocity.vz = 0;
        break;
    case FACING_POS_X_POS_Z:
        object->velocity.vx = ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN;
        object->velocity.vz = ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN;
        break;
    case FACING_POS_Z:
        object->velocity.vx = 0;
        object->velocity.vz = ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN;
        break;
    case FACING_NEG_X_POS_Z:
        object->velocity.vx = -( ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN );
        object->velocity.vz = ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN;
        break;
    case FACING_NEG_X:
        object->velocity.vx = -( ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN );
        object->velocity.vz = 0;
        break;
    case FACING_NEG_X_NEG_Z:
        object->velocity.vx = -( ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN );
        object->velocity.vz = -( ( Sys_Random() & THROW_SPEED_MASK ) + THROW_SPEED_MIN );
        break;
    }
    /* (u_short): retail adds the jitter through the constant -THROW_JITTER_BIAS as a halfword */
    object->velocity.vx += ( (u_short)Sys_Random() & THROW_JITTER_MASK ) - THROW_JITTER_BIAS;
    object->velocity.vz += ( (u_short)Sys_Random() & THROW_JITTER_MASK ) - THROW_JITTER_BIAS;
    object->velocity.vy = THROW_LIFT_SPEED;
    object->bounceSpeed = THROW_BOUNCE_SPEED;
    SET_VECTOR_TO_ZERO( object->rotation );
    object->model = g_objectModels[object->kind];
    Object_Link( object );
}

/*
==================
Object_Unlink
==================
*/
void Object_Unlink( mapObject_t *node )
{
    mapObject_t *next = node->next;
    mapObject_t *prev = node->prev;
    if ( next )
        next->prev = prev;
    if ( prev )
        prev->next = next;
    else
        g_mapObjects = next;
    node->prev = 0;
    node->next = 0;
}

/*
==================
Object_NearPlayer
==================
*/
int Object_NearPlayer( mapObject_t *object )
{
    int delta[VECTOR_AXES];
    delta[0] = object->position.vx - g_player.position.vx;
    delta[1] = object->position.vy - g_player.position.vy;
    delta[2] = object->position.vz - g_player.position.vz;
    if ( delta[0] < 0 )
        delta[0] = -delta[0];
    if ( delta[1] < 0 )
        delta[1] = -delta[1];
    if ( delta[2] < 0 )
        delta[2] = -delta[2];
    return delta[0] < PICKUP_REACH && delta[1] < PICKUP_REACH_Y && delta[2] < PICKUP_REACH;
}

/*
==================
Object_Draw
==================
*/
void Object_Draw( mapObject_t *object )
{
    if ( object->model == 0 )
        return;
    if ( ( object->cell->mask & g_visibleCellMask ) == 0 )
        return;
    g_modelUvs = g_pickupUvs;
    LoadAverageShort12( &object->position, &g_viewPosition, ONE, -ONE, &g_relativePosition );
    PushMatrix();
    ApplyMatrix( &g_viewMatrix, &g_relativePosition, &g_translation );
    if ( (u_long)( g_translation.vz - 1 ) < MODEL_CULL_DISTANCE - 1 && g_translation.vx < MODEL_CULL_DISTANCE ) {
        RotMatrix( &object->rotation, &g_matrixWork );
        MulMatrix2( &g_viewMatrix, &g_matrixWork );
        ScaleMatrix( &g_matrixWork, &g_mapObjectScales[object->kind] );
        SetRotMatrix( &g_matrixWork );
        TransMatrix( &g_matrixWork, &g_translation );
        SetTransMatrix( &g_matrixWork );
        if ( g_translation.vz < MODEL_FAR_DISTANCE )
            Model_DrawMidInCell( object->model, object->cell );
        else
            Model_DrawFarInCell( object->model, object->cell );
    }
    g_modelUvs = g_spriteUvs;
    object->rotation.vy = ( object->rotation.vy + MAP_OBJECT_SPIN_STEP ) & ( ONE - 1 );
    PopMatrix();
}

/* Multi-statement macro (name invented): takes an object off the live list and returns it to the pool.
 * Its do { } while (0) block gives retail's register order (object in s1, sound in s2). */
#define MAP_OBJECT_REMOVE( object ) \
    do {                            \
        Object_Unlink( object );    \
        Object_Free( object );      \
    } while ( 0 )

/*
==================
Object_PickupsTask

Rule exception (config/exceptions.json): the loop is entered at its test through a goto, as
retail is (no copy of the test before the first object).
==================
*/
/* name: inferred from its body: the pickup task; an object the player touches and can use is
 * announced, sounded, removed and handed to Weapon_PlayerGivePickup, every other object is drawn */
/* MATCH-PENDING: R17, goto loop entered at its test; every while/for/do spelling (while, for with increment, for (;;) with break, if + do-while, != 0 tests, (object = next) in the condition) gets jump pass 1's duplicated top test (beqz instead of retail's j, 1 insn) or grows; permuter (20 min, 2163 iterations, from the while base) found no state below that 1-insn diff */
void Object_PickupsTask( TASK *task )
{
    mapObject_t *object;
    mapObject_t *next;
    short        taken;
    short        sound = SOUND_PICKUP;

    object = g_mapObjects;
    goto nextObject;
objectLoop:
    Object_Move( object );
    next = object->next;
    taken = 0;
    if ( Object_NearPlayer( object ) ) {
        switch ( object->kind ) {
        case PICKUP_PISTOL:
            taken = -1;
            sound = SOUND_PISTOL_PICKUP;
            break;
        case PICKUP_SHOTGUN:
            taken = -1;
            sound = SOUND_SHOTGUN_PUMP;
            break;
        case PICKUP_PULSE_RIFLE:
            taken = -1;
            sound = SOUND_PULSE_RIFLE_PICKUP;
            break;
        case PICKUP_FLAMETHROWER:
            taken = -1;
            sound = SOUND_FLAMETHROWER_PICKUP;
            break;
        case PICKUP_SMART_GUN:
            taken = -1;
            sound = SOUND_SMART_GUN_PICKUP;
            break;
        case PICKUP_AUTO_MAPPER:
            taken = !( g_player.flags & PLAYER_HAS_AUTO_MAPPER );
            break;
        case PICKUP_ACID_VEST:
        case PICKUP_BODY_SUIT:
            taken = object->amount > g_player.armour;
            break;
        case PICKUP_MEDIKIT:
            taken = g_player.health < PLAYER_HEALTH_MAX;
            break;
        case PICKUP_ADRENALINE_BURST:
            taken = g_player.health < PLAYER_HEALTH_ADRENALINE;
            break;
        case PICKUP_PISTOL_CLIP:
            taken = g_player.pistolAmmo < PISTOL_CLIP_ROUNDS || g_player.pistolClips < CLIPS_MAX;
            sound = SOUND_PISTOL_PICKUP;
            break;
        case PICKUP_SHOTGUN_CARTRIDGES:
            taken = g_player.shotgunAmmo < SHOTGUN_AMMO_MAX;
            sound = SOUND_SHOTGUN_PUMP;
            break;
        case PICKUP_PULSE_RIFLE_CLIP:
            taken = g_player.pulseRifleAmmo < PULSE_RIFLE_CLIP_ROUNDS || g_player.pulseRifleClips < CLIPS_MAX;
            sound = SOUND_PULSE_RIFLE_PICKUP;
            break;
        case PICKUP_FLAMETHROWER_FUEL:
            taken = g_player.flamethrowerFuel < FLAMETHROWER_FUEL_MAX;
            sound = SOUND_FLAMETHROWER_PICKUP;
            break;
        case PICKUP_SMART_GUN_AMMO:
            taken = g_player.smartGunAmmo < SMART_GUN_CLIP_ROUNDS || g_player.smartGunClips < CLIPS_MAX;
            sound = SOUND_SMART_GUN_PICKUP;
            break;
        case PICKUP_GRENADES:
            taken = g_player.pulseRifleGrenades < PULSE_RIFLE_GRENADES_MAX;
            sound = SOUND_GRENADES_PICKUP;
            break;
        case PICKUP_SEISMIC_CHARGES:
        case PICKUP_BATTERY:
        case PICKUP_NIGHT_VISION:
        case PICKUP_IDENTITY_TAG:
        case PICKUP_HYPO_PACK:
        case PICKUP_DERM_PATCH:
        case PICKUP_PROTECTIVE_BOOTS:
        case PICKUP_SHOULDER_LAMP:
            taken = -1;
            break;
        case PICKUP_UNUSED_5:
        case PICKUP_UNUSED_24:
            break;
        }
    }
    if ( taken ) {
        Hud_ShowMessage( g_pickupMessages[object->kind] );
        Snd_Play( sound, -1, SPU_VOLUME_MAX );
        MAP_OBJECT_REMOVE( object );
        Weapon_PlayerGivePickup( object->kind, object->amount, object->clips );
    } else {
        Object_Draw( object );
    }
    object = next;
nextObject:
    if ( object )
        goto objectLoop;
    g_nextTask = task->Next;
}
