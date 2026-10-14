/*
 * The view transform: the camera position and angles, the GTE matrices built from
 * them each frame (Geo_SetupViewTransform), the vector rotation helpers and the x-z
 * heading/distance helpers (all in src/geometry.c). Requires <sys/types.h> and <LIBGTE.H>.
 */
#ifndef VIEW_H
#define VIEW_H

/* GTE projection distance (SetGeomScreen) and fog start (SetFogNear) main sets at boot. names: invented */
#define PROJECTION_DISTANCE 256
#define FOG_NEAR 0x14000

/* 12-bit angles: ANGLE_360 units a turn (g_sineTable's length); ANGLE_MASK wraps one. names: invented */
/* LIBGTE fixed point: ONE == 1 << FIXED_SHIFT; a product of two 4.12 values is shifted down by it.
 * name: invented */
#define FIXED_SHIFT 12

/* Corner order of the four vertices of a quad row (dispatchRow_t.vertices[]), as the 3x3 panel grid of
 * Debris_SpawnPanel lays them out. names: invented */
enum {
    QUAD_VERTEX_TOP_LEFT,
    QUAD_VERTEX_TOP_RIGHT,
    QUAD_VERTEX_BOTTOM_RIGHT,
    QUAD_VERTEX_BOTTOM_LEFT
};

/* The x, y and z components of a position or velocity held in an array. name: invented */
#define VECTOR_AXES 3
/* The red, green and blue components of a colour held in an array. name: invented */
#define RGB_CHANNELS 3

#define ANGLE_45 0x200
#define ANGLE_90 0x400
#define ANGLE_135 0x600
#define ANGLE_180 0x800
#define ANGLE_225 0xa00
#define ANGLE_270 0xc00
#define ANGLE_315 0xe00
#define ANGLE_360 0x1000
#define ANGLE_MASK 0xfff

/* zeroes vx, vy and vz of the SVECTOR or VECTOR v (not its pad); LIBGPU.H setVector, so the
 * unit needs <LIBGPU.H>. name: invented */
#define SET_VECTOR_TO_ZERO( v ) setVector( &( v ), 0, 0, 0 )
/* angle >> ANGLE_OCTANT_SHIFT is its octant, one of the eight facings (actor_t.direction). name: invented */
#define ANGLE_OCTANT_SHIFT 9

/* Depth range (RotTransPers depth << 2, g_projectedDepth) the actor, particle and effect
 * renderers draw sprites in. names: invented */
#define DRAW_NEAR_DEPTH 0x200
#define DRAW_FAR_DEPTH 0x1e00

/* name: inferred from use: Geo_SetupViewTransform builds the view transform from its negation; shots and particles start from it */
extern SVECTOR g_viewPosition;
/* name: inferred from use: Geo_SetupViewTransform builds g_viewMatrix from it; shots take its yaw and roll */
extern SVECTOR g_viewRotation;
/* name: inferred from use: Geo_SetupViewTransform multiplies its matrix into the view; shots take -vx as their pitch */
extern SVECTOR g_viewPitch;
/* name: runtime evidence, analysis/runtime/globals.json: (0,0,0,0) in every frame and
 * never written; .sdata 0x8008cd9c, between main's binding table and g_actionIndex.
 * Only passed as the origin to Geo_PointHeadingXZ (Particle_ActorFireBullet, Particle_FireSmartGun). */
extern SVECTOR g_zeroVector;
/* name: inferred from use: LoadAverageShort12 leaves position - g_viewPosition in it before ApplyMatrix */
extern SVECTOR g_relativePosition;
extern MATRIX  g_viewMatrix;
extern MATRIX  g_matrixWork;
extern VECTOR  g_translation;
/* names: inferred from use: the p (depth-cue interpolation) and flag outputs of RotTransPers and RotAverageNclip4 */
extern long g_gteInterpolation;
extern long g_gteFlag;
/* name: inferred from use: the depth value of the last transform, fed to DpqColor */
extern long g_transformResult;

/* src/geometry.c: sine of a 12-bit angle in 4.12, 4096 entries; SINE/COSINE index it */
extern short g_sineTable[ANGLE_360];

extern void Geo_SetupViewTransform( void );
/* src/geometry.c: rotation matrices from three 12-bit angles */
extern void Geo_RotationMatrix( const SVECTOR *rotation, MATRIX *matrix );
extern void Geo_RotationMatrixInverse( const SVECTOR *rotation, MATRIX *matrix );
extern void f_20584( const SVECTOR *rotation, MATRIX *matrix );
extern void f_2076c( const SVECTOR *rotation, MATRIX *matrix );
extern void Geo_RotateVector( const SVECTOR *rotation, SVECTOR *point, SVECTOR *result );
extern short Geo_PointHeadingXZ( SVECTOR *left, SVECTOR *right );
extern int Geo_PointDistanceXZ( SVECTOR *left, SVECTOR *right );
extern void Geo_AimRotation( SVECTOR *left, SVECTOR *right, SVECTOR *out );
extern void Geo_RotateVectorOffset( const SVECTOR *rotation, SVECTOR *point, const SVECTOR *origin, SVECTOR *result );

#endif
