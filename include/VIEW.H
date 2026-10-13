/*
 * The view transform: the camera position and angles, the GTE matrices built from
 * them each frame (view_transform_setup), the vector rotation helpers and the x-z
 * heading/distance helpers (all in src/geometry.c). Requires <sys/types.h> and <LIBGTE.H>.
 */
#ifndef VIEW_H
#define VIEW_H

/* GTE projection distance (SetGeomScreen) and fog start (SetFogNear) main sets at boot. names: invented */
#define PROJECTION_DISTANCE 256
#define FOG_NEAR 0x14000

/* 12-bit angles: ANGLE_360 units a turn (g_sine_table's length); ANGLE_MASK wraps one. names: invented */
/* LIBGTE fixed point: ONE == 1 << FIXED_SHIFT; a product of two 4.12 values is shifted down by it.
 * name: invented */
#define FIXED_SHIFT 12

#define ANGLE_90   0x400
#define ANGLE_180  0x800
#define ANGLE_360  0x1000
#define ANGLE_MASK 0xfff

/* zeroes vx, vy and vz of the SVECTOR or VECTOR v (not its pad); LIBGPU.H setVector, so the
 * unit needs <LIBGPU.H>. name: invented */
#define SET_VECTOR_TO_ZERO(v) setVector(&(v), 0, 0, 0)
/* angle >> ANGLE_OCTANT_SHIFT is its octant, one of the eight facings (Actor.direction). name: invented */
#define ANGLE_OCTANT_SHIFT 9

/* Depth range (RotTransPers depth << 2, g_projected_depth) the actor, particle and effect
 * renderers draw sprites in. names: invented */
#define DRAW_NEAR_DEPTH 0x200
#define DRAW_FAR_DEPTH 0x1e00

/* name: inferred from use: view_transform_setup builds the view transform from its negation; shots and particles start from it */
extern SVECTOR g_view_position;
/* name: inferred from use: view_transform_setup builds g_view_matrix from it; shots take its yaw and roll */
extern SVECTOR g_view_rotation;
/* name: inferred from use: view_transform_setup multiplies its matrix into the view; shots take -vx as their pitch */
extern SVECTOR g_view_pitch;
/* name: runtime evidence, analysis/runtime/globals.json: (0,0,0,0) in every frame and
 * never written; .sdata 0x8008cd9c, between main's binding table and g_action_index.
 * Only passed as the origin to point_heading_xz (actor_fire_bullet, smart_gun_fire). */
extern SVECTOR g_zero_vector;
/* name: inferred from use: LoadAverageShort12 leaves position - g_view_position in it before ApplyMatrix */
extern SVECTOR g_relative_position;
extern MATRIX g_view_matrix;
extern MATRIX g_matrix_work;
extern VECTOR g_translation;
/* names: inferred from use: the p (depth-cue interpolation) and flag outputs of RotTransPers and RotAverageNclip4 */
extern long g_gte_interpolation;
extern long g_gte_flag;
/* name: inferred from use: the depth value of the last transform, fed to DpqColor */
extern long g_transform_result;

/* src/geometry.c: sine of a 12-bit angle in 4.12, 4096 entries; SINE/COSINE index it */
extern short g_sine_table[ANGLE_360];

extern void view_transform_setup(void);
/* src/geometry.c: rotation matrices from three 12-bit angles */
extern void rotation_matrix(const SVECTOR *rotation, MATRIX *matrix);
extern void rotation_matrix_inverse(const SVECTOR *rotation, MATRIX *matrix);
extern void f_20584(const SVECTOR *rotation, MATRIX *matrix);
extern void f_2076c(const SVECTOR *rotation, MATRIX *matrix);
extern void rotate_vector(const SVECTOR *rotation, SVECTOR *point, SVECTOR *result);
extern short point_heading_xz(SVECTOR *left, SVECTOR *right);
extern int point_distance_xz(SVECTOR *left, SVECTOR *right);
extern void aim_rotation(SVECTOR *left, SVECTOR *right, SVECTOR *out);
extern void rotate_vector_offset(const SVECTOR *rotation, SVECTOR *point, const SVECTOR *origin, SVECTOR *result);

#endif
