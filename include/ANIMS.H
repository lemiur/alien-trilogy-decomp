/*
 * Sprite animation tables of the hand-written module asm/sprite_anims.s (retail .text
 * 0x800558e8..0x800588bb). Each animation is a frame list followed by its script; the
 * TrackEntry tables in .data (<TRACK.H>) pair them. Requires <sys/types.h> and <SPRITE.H>.
 * Names are the labels' addresses until their animations are identified.
 */
#ifndef ANIMS_H
#define ANIMS_H

extern SpriteFrame g_pulse_rifle_anim0_frames[1];	extern u_short g_pulse_rifle_anim0_script[];	/* animation 0 */
extern SpriteFrame g_pulse_rifle_anim2_frames[6];	extern u_short g_pulse_rifle_anim2_script[];	/* animation 1 */
extern SpriteFrame g_pulse_rifle_anim3_frames[4];	extern u_short g_pulse_rifle_anim3_script[];	/* animation 2 */
extern SpriteFrame g_pulse_rifle_anim1_frames[3];	extern u_short g_pulse_rifle_anim1_script[];	/* animation 3 */
extern SpriteFrame g_shotgun_anim0_frames[1];	extern u_short g_shotgun_anim0_script[];	/* animation 4 */
extern SpriteFrame g_shotgun_anim1_frames[8];	extern u_short g_shotgun_anim1_script[];	/* animation 5 */
extern SpriteFrame g_pistol_anim0_frames[1];	extern u_short g_pistol_anim0_script[];	/* animation 6 */
extern SpriteFrame g_pistol_anim1_frames[3];	extern u_short g_pistol_anim1_script[];	/* animation 7 */
extern SpriteFrame g_pistol_anim2_frames[3];	extern u_short g_pistol_anim2_script[];	/* animation 8 */
extern SpriteFrame g_flamethrower_anim0_frames[1];	extern u_short g_flamethrower_anim0_script[];	/* animation 9 */
extern SpriteFrame g_flamethrower_anim1_frames[1];	extern u_short g_flamethrower_anim1_script[];	/* animation 10 */
extern SpriteFrame g_flamethrower_anim2_frames[5];	extern u_short g_flamethrower_anim2_script[];	/* animation 11 */
extern SpriteFrame g_smart_gun_anim1_frames[4];	extern u_short g_smart_gun_anim1_script[];	/* animation 12 */
extern SpriteFrame g_smart_gun_anim2_frames[5];	extern u_short g_smart_gun_anim2_script[];	/* animation 13 */
extern SpriteFrame g_smart_gun_anim0_frames[1];	extern u_short g_smart_gun_anim0_script[];	/* animation 14 */
extern SpriteFrame g_egg_anim0_frames[3];	extern u_short g_egg_anim0_script[];	/* animation 15 */
extern SpriteFrame g_egg_anim1_frames[3];	extern u_short g_egg_anim1_script[];	/* animation 16 */
extern SpriteFrame g_egg_anim2_frames[5];	extern u_short g_egg_anim2_script[];	/* animation 17 */
extern SpriteFrame g_egg_anim3_frames[6];	extern u_short g_egg_anim3_script[];	/* animation 18 */
extern SpriteFrame g_egg_anim4_frames[8];	extern u_short g_egg_anim4_script[];	/* animation 19 */
extern SpriteFrame g_egg_anim5_frames[8];	extern u_short g_egg_anim5_script[];	/* animation 20 */
extern SpriteFrame g_hugger_anim0_frames[4];	extern u_short g_hugger_anim0_script[];	/* animation 21 */
extern SpriteFrame g_hugger_anim4_frames[4];	extern u_short g_hugger_anim4_script[];	/* animation 22 */
extern SpriteFrame g_hugger_anim2_frames[4];	extern u_short g_hugger_anim2_script[];	/* animation 23 */
extern SpriteFrame g_hugger_anim1_frames[4];	extern u_short g_hugger_anim1_script[];	/* animation 24 */
extern SpriteFrame g_hugger_anim3_frames[4];	extern u_short g_hugger_anim3_script[];	/* animation 25 */
extern SpriteFrame g_hugger_anim6_frames[6];	extern u_short g_hugger_anim6_script[];	/* animation 26 */
extern SpriteFrame g_hugger_anim7_frames[8];	extern u_short g_hugger_anim7_script[];	/* animation 27 */
extern SpriteFrame g_hugger_anim5_frames[4];	extern u_short g_hugger_anim5_script[];	/* animation 28 */
extern SpriteFrame g_warrior_anim5_frames[7];	extern u_short g_warrior_anim5_script[];	/* animation 29 */
extern SpriteFrame g_warrior_anim3_frames[7];	extern u_short g_warrior_anim3_script[];	/* animation 30 */
extern SpriteFrame g_warrior_anim4_frames[7];	extern u_short g_warrior_anim4_script[];	/* animation 31 */
extern SpriteFrame g_warrior_anim7_frames[7];	extern u_short g_warrior_anim7_script[];	/* animation 32 */
extern SpriteFrame g_warrior_anim6_frames[7];	extern u_short g_warrior_anim6_script[];	/* animation 33 */
extern SpriteFrame g_warrior_anim0_frames[3];	extern u_short g_warrior_anim0_script[];	/* animation 34 */
extern SpriteFrame g_warrior_anim1_frames[3];	extern u_short g_warrior_anim1_script[];	/* animation 35 */
extern SpriteFrame g_warrior_anim2_frames[6];	extern u_short g_warrior_anim2_script[];	/* animation 36 */
extern SpriteFrame g_warrior_anim8_frames[4];	extern u_short g_warrior_anim8_script[];	/* animation 37 */
extern SpriteFrame g_warrior_anim9_frames[7];	extern u_short g_warrior_anim9_script[];	/* animation 38 */
extern SpriteFrame g_handler_anim0_frames[7];	extern u_short g_handler_anim0_script[];	/* animation 39 */
extern SpriteFrame g_handler_anim1_frames[7];	extern u_short g_handler_anim1_script[];	/* animation 40 */
extern SpriteFrame g_handler_anim2_frames[7];	extern u_short g_handler_anim2_script[];	/* animation 41 */
extern SpriteFrame g_handler_anim3_frames[7];	extern u_short g_handler_anim3_script[];	/* animation 42 */
extern SpriteFrame g_handler_anim4_frames[7];	extern u_short g_handler_anim4_script[];	/* animation 43 */
extern SpriteFrame g_handler_anim5_frames[3];	extern u_short g_handler_anim5_script[];	/* animation 44 */
extern SpriteFrame g_handler_anim6_frames[5];	extern u_short g_handler_anim6_script[];	/* animation 45 */
extern SpriteFrame g_handler_anim7_frames[8];	extern u_short g_handler_anim7_script[];	/* animation 46 */
extern SpriteFrame g_handler_anim8_frames[3];	extern u_short g_handler_anim8_script[];	/* animation 47 */
extern SpriteFrame g_burster_anim0_frames[4];	extern u_short g_burster_anim0_script[];	/* animation 48 */
extern SpriteFrame g_burster_anim1_frames[8];	extern u_short g_burster_anim1_script[];	/* animation 49 */
extern SpriteFrame g_burster_anim2_frames[4];	extern u_short g_burster_anim2_script[];	/* animation 50 */
extern SpriteFrame g_burster_anim3_frames[4];	extern u_short g_burster_anim3_script[];	/* animation 51 */
extern SpriteFrame g_burster_anim4_frames[6];	extern u_short g_burster_anim4_script[];	/* animation 52 */
extern SpriteFrame g_burster_anim5_frames[6];	extern u_short g_burster_anim5_script[];	/* animation 53 */
extern SpriteFrame g_burster_anim6_frames[4];	extern u_short g_burster_anim6_script[];	/* animation 54 */
extern SpriteFrame g_burster_anim7_frames[4];	extern u_short g_burster_anim7_script[];	/* animation 55 */
extern SpriteFrame g_burster_anim8_frames[4];	extern u_short g_burster_anim8_script[];	/* animation 56 */
extern SpriteFrame g_burster_anim9_frames[4];	extern u_short g_burster_anim9_script[];	/* animation 57 */
extern SpriteFrame g_burster_anim10_frames[4];	extern u_short g_burster_anim10_script[];	/* animation 58 */
extern SpriteFrame g_bambi_anim0_frames[6];	extern u_short g_bambi_anim0_script[];	/* animation 59 */
extern SpriteFrame g_bambi_anim1_frames[3];	extern u_short g_bambi_anim1_script[];	/* animation 60 */
extern SpriteFrame g_bambi_anim2_frames[3];	extern u_short g_bambi_anim2_script[];	/* animation 61 */
extern SpriteFrame g_bambi_anim3_frames[7];	extern u_short g_bambi_anim3_script[];	/* animation 62 */
extern SpriteFrame g_bambi_anim4_frames[7];	extern u_short g_bambi_anim4_script[];	/* animation 63 */
extern SpriteFrame g_bambi_anim5_frames[7];	extern u_short g_bambi_anim5_script[];	/* animation 64 */
extern SpriteFrame g_bambi_anim6_frames[7];	extern u_short g_bambi_anim6_script[];	/* animation 65 */
extern SpriteFrame g_bambi_anim7_frames[7];	extern u_short g_bambi_anim7_script[];	/* animation 66 */
extern SpriteFrame g_bambi_anim8_frames[6];	extern u_short g_bambi_anim8_script[];	/* animation 67 */
extern SpriteFrame g_bambi_anim9_frames[6];	extern u_short g_bambi_anim9_script[];	/* animation 68 */
extern SpriteFrame g_soldier_anim0_frames[4];	extern u_short g_soldier_anim0_script[];	/* animation 69 */
extern SpriteFrame g_soldier_anim1_frames[3];	extern u_short g_soldier_anim1_script[];	/* animation 70 */
extern SpriteFrame g_soldier_anim2_frames[5];	extern u_short g_soldier_anim2_script[];	/* animation 71 */
extern SpriteFrame g_soldier_anim3_frames[7];	extern u_short g_soldier_anim3_script[];	/* animation 72 */
extern SpriteFrame g_soldier_anim5_frames[7];	extern u_short g_soldier_anim5_script[];	/* animation 73 */
extern SpriteFrame g_soldier_anim4_frames[7];	extern u_short g_soldier_anim4_script[];	/* animation 74 */
extern SpriteFrame g_soldier_anim6_frames[7];	extern u_short g_soldier_anim6_script[];	/* animation 75 */
extern SpriteFrame g_soldier_anim7_frames[7];	extern u_short g_soldier_anim7_script[];	/* animation 76 */
extern SpriteFrame g_guard_anim0_frames[3];	extern u_short g_guard_anim0_script[];	/* animation 77 */
extern SpriteFrame g_guard_anim1_frames[5];	extern u_short g_guard_anim1_script[];	/* animation 78 */
extern SpriteFrame g_guard_anim2_frames[3];	extern u_short g_guard_anim2_script[];	/* animation 79 */
extern SpriteFrame g_guard_anim4_frames[7];	extern u_short g_guard_anim4_script[];	/* animation 80 */
extern SpriteFrame g_guard_anim3_frames[7];	extern u_short g_guard_anim3_script[];	/* animation 81 */
extern SpriteFrame g_guard_anim5_frames[7];	extern u_short g_guard_anim5_script[];	/* animation 82 */
extern SpriteFrame g_guard_anim7_frames[7];	extern u_short g_guard_anim7_script[];	/* animation 83 */
extern SpriteFrame g_guard_anim6_frames[7];	extern u_short g_guard_anim6_script[];	/* animation 84 */
extern SpriteFrame g_synth_anim0_frames[3];	extern u_short g_synth_anim0_script[];	/* animation 85 */
extern SpriteFrame g_synth_anim1_frames[2];	extern u_short g_synth_anim1_script[];	/* animation 86 */
extern SpriteFrame g_synth_anim2_frames[4];	extern u_short g_synth_anim2_script[];	/* animation 87 */
extern SpriteFrame g_synth_anim3_frames[4];	extern u_short g_synth_anim3_script[];	/* animation 88 */
extern SpriteFrame g_synth_anim4_frames[3];	extern u_short g_synth_anim4_script[];	/* animation 89 */
extern SpriteFrame g_synth_anim6_frames[7];	extern u_short g_synth_anim6_script[];	/* animation 90 */
extern SpriteFrame g_synth_anim5_frames[7];	extern u_short g_synth_anim5_script[];	/* animation 91 */
extern SpriteFrame g_synth_anim7_frames[7];	extern u_short g_synth_anim7_script[];	/* animation 92 */
extern SpriteFrame g_synth_anim8_frames[7];	extern u_short g_synth_anim8_script[];	/* animation 93 */
extern SpriteFrame g_synth_anim11_frames[7];	extern u_short g_synth_anim11_script[];	/* animation 94 */
extern SpriteFrame g_synth_anim12_frames[7];	extern u_short g_synth_anim12_script[];	/* animation 95 */
extern SpriteFrame g_synth_anim9_frames[7];	extern u_short g_synth_anim9_script[];	/* animation 96 */
extern SpriteFrame g_synth_anim10_frames[7];	extern u_short g_synth_anim10_script[];	/* animation 97 */
extern SpriteFrame g_dog_anim0_frames[3];	extern u_short g_dog_anim0_script[];	/* animation 98 */
extern SpriteFrame g_dog_anim1_frames[3];	extern u_short g_dog_anim1_script[];	/* animation 99 */
extern SpriteFrame g_dog_anim2_frames[5];	extern u_short g_dog_anim2_script[];	/* animation 100 */
extern SpriteFrame g_dog_anim3_frames[5];	extern u_short g_dog_anim3_script[];	/* animation 101 */
extern SpriteFrame g_dog_anim4_frames[7];	extern u_short g_dog_anim4_script[];	/* animation 102 */
extern SpriteFrame g_dog_anim5_frames[7];	extern u_short g_dog_anim5_script[];	/* animation 103 */
extern SpriteFrame g_dog_anim6_frames[7];	extern u_short g_dog_anim6_script[];	/* animation 104 */
extern SpriteFrame g_dog_anim7_frames[7];	extern u_short g_dog_anim7_script[];	/* animation 105 */
extern SpriteFrame g_dog_anim8_frames[7];	extern u_short g_dog_anim8_script[];	/* animation 106 */
extern SpriteFrame g_dog_anim9_frames[8];	extern u_short g_dog_anim9_script[];	/* animation 107 */
extern SpriteFrame g_dog_anim10_frames[6];	extern u_short g_dog_anim10_script[];	/* animation 108 */
extern SpriteFrame g_fingers_anim1_frames[4];	extern u_short g_fingers_anim1_script[];	/* animation 109 */
extern SpriteFrame g_fingers_anim0_frames[4];	extern u_short g_fingers_anim0_script[];	/* animation 110 */
extern SpriteFrame g_queen_anim0_frames[4];	extern u_short g_queen_anim0_script[];	/* animation 111 */
extern SpriteFrame g_queen_anim1_frames[11];	extern u_short g_queen_anim1_script[];	/* animation 112 */
extern SpriteFrame g_queen_anim2_frames[10];	extern u_short g_queen_anim2_script[];	/* animation 113 */
extern SpriteFrame g_queen_anim3_frames[6];	extern u_short g_queen_anim3_script[];	/* animation 114 */
extern SpriteFrame g_queen_anim4_frames[11];	extern u_short g_queen_anim4_script[];	/* animation 115 */
extern SpriteFrame g_queen_anim5_frames[11];	extern u_short g_queen_anim5_script[];	/* animation 116 */
extern SpriteFrame g_queen_anim6_frames[6];	extern u_short g_queen_anim6_script[];	/* animation 117 */
extern SpriteFrame g_queen_anim7_frames[6];	extern u_short g_queen_anim7_script[];	/* animation 118 */
extern SpriteFrame g_queen_anim8_frames[6];	extern u_short g_queen_anim8_script[];	/* animation 119 */
extern SpriteFrame g_queen_anim9_frames[3];	extern u_short g_queen_anim9_script[];	/* animation 120 */
extern SpriteFrame g_queen_anim10_frames[4];	extern u_short g_queen_anim10_script[];	/* animation 121 */
extern SpriteFrame g_queen_anim11_frames[7];	extern u_short g_queen_anim11_script[];	/* animation 122 */
extern SpriteFrame g_warrior_ceiling_anim2_frames[7];	extern u_short g_warrior_ceiling_anim2_script[];	/* animation 123 */
extern SpriteFrame g_warrior_ceiling_anim3_frames[7];	extern u_short g_warrior_ceiling_anim3_script[];	/* animation 124 */
extern SpriteFrame g_warrior_ceiling_anim0_frames[5];	extern u_short g_warrior_ceiling_anim0_script[];	/* animation 125 */
extern SpriteFrame g_warrior_ceiling_anim1_frames[3];	extern u_short g_warrior_ceiling_anim1_script[];	/* animation 126 */
extern SpriteFrame g_warrior_ceiling_anim4_frames[4];	extern u_short g_warrior_ceiling_anim4_script[];	/* animation 127 */
extern SpriteFrame g_colonist_anim0_frames[1];	extern u_short g_colonist_anim0_script[];	/* animation 128 */
extern SpriteFrame g_colonist_anim1_frames[1];	extern u_short g_colonist_anim1_script[];	/* animation 129 */
extern SpriteFrame g_colonist_anim2_frames[1];	extern u_short g_colonist_anim2_script[];	/* animation 130 */
extern SpriteFrame g_colonist_anim3_frames[1];	extern u_short g_colonist_anim3_script[];	/* animation 131 */
extern SpriteFrame g_colonist_anim4_frames[1];	extern u_short g_colonist_anim4_script[];	/* animation 132 */
extern SpriteFrame g_colonist_anim5_frames[1];	extern u_short g_colonist_anim5_script[];	/* animation 133 */
extern SpriteFrame g_colonist_anim6_frames[1];	extern u_short g_colonist_anim6_script[];	/* animation 134 */
extern SpriteFrame g_colonist_anim7_frames[1];	extern u_short g_colonist_anim7_script[];	/* animation 135 */
extern SpriteFrame g_colonist_anim8_frames[1];	extern u_short g_colonist_anim8_script[];	/* animation 136 */
extern SpriteFrame g_colonist_anim9_frames[1];	extern u_short g_colonist_anim9_script[];	/* animation 137 */
extern SpriteFrame g_colonist_anim10_frames[2];	extern u_short g_colonist_anim10_script[];	/* animation 138 */
extern SpriteFrame g_colonist_anim11_frames[2];	extern u_short g_colonist_anim11_script[];	/* animation 139 */
extern SpriteFrame g_colonist_anim12_frames[2];	extern u_short g_colonist_anim12_script[];	/* animation 140 */
extern SpriteFrame g_colonist_anim13_frames[2];	extern u_short g_colonist_anim13_script[];	/* animation 141 */
extern SpriteFrame g_colonist_anim14_frames[2];	extern u_short g_colonist_anim14_script[];	/* animation 142 */
extern SpriteFrame g_colonist_anim15_frames[4];	extern u_short g_colonist_anim15_script[];	/* animation 143 */
extern SpriteFrame g_colonist_anim16_frames[4];	extern u_short g_colonist_anim16_script[];	/* animation 144 */
extern SpriteFrame g_colonist_anim17_frames[7];	extern u_short g_colonist_anim17_script[];	/* animation 145 */
extern SpriteFrame g_colonist_anim18_frames[1];	extern u_short g_colonist_anim18_script[];	/* animation 146 */
extern SpriteFrame g_colonist_anim19_frames[1];	extern u_short g_colonist_anim19_script[];	/* animation 147 */
extern SpriteFrame g_colonist_anim20_frames[1];	extern u_short g_colonist_anim20_script[];	/* animation 148 */
extern SpriteFrame g_colonist_anim21_frames[1];	extern u_short g_colonist_anim21_script[];	/* animation 149 */
extern SpriteFrame g_colonist_anim22_frames[1];	extern u_short g_colonist_anim22_script[];	/* animation 150 */
extern SpriteFrame g_dog_ceiling_anim0_frames[7];	extern u_short g_dog_ceiling_anim0_script[];	/* animation 151 */
extern SpriteFrame g_dog_ceiling_anim1_frames[7];	extern u_short g_dog_ceiling_anim1_script[];	/* animation 152 */
extern SpriteFrame g_dog_ceiling_anim2_frames[3];	extern u_short g_dog_ceiling_anim2_script[];	/* animation 153 */
extern SpriteFrame g_dog_ceiling_anim3_frames[4];	extern u_short g_dog_ceiling_anim3_script[];	/* animation 154 */
extern SpriteFrame g_dog_ceiling_anim4_frames[5];	extern u_short g_dog_ceiling_anim4_script[];	/* animation 155 */

/* Indices into an actor's animation table (Actor.animations, actor_play_animation) that
 * the code names by number beyond the eight facing octants. names: invented, from the
 * code that plays them */
enum {
    COLONIST_ANIM_COCOONED = 8, /* + octant: struggling in the cocoon (colonist_task) */
    COLONIST_ANIM_HURT = 0x10, /* + octant (actor_hurt) */
    COLONIST_ANIM_REACH = 0x18, /* facing the player: reaches out, then waits (colonist_task) */
    COLONIST_ANIM_BURST = 0x1a, /* the chest-burster breaks out (colonist_release_burster follows) */
    COLONIST_ANIM_DEAD = 0x1b, /* + octant */
    QUEEN_ANIM_HURT = 0x10, /* actor_hurt in attack phases 0 and 1 */
    QUEEN_ANIM_KNOCKED_BACK = 0x11 /* actor_hurt from phase 3 on, with actor_knock_back */
};

/* The rest of the numbered animations, per kind. Every creature table starts with
 * ACTOR_ANIM_FACING + shown octant (actor_face_view); the entries from 8 on differ by kind.
 * HURT_FRONT plays when the actor is hit seen from the front (shown octants 7, 0 and 1),
 * HURT_BACK otherwise; actor_to_floor also plays one of them as the actor drops from the
 * ceiling. Kinds that share a case label share a value (noted). names: invented, from the
 * code that plays them */
enum {
    ACTOR_ANIM_FACING = 0,
    EGG_ANIM_CLOSED = 0, /* at spawn, and when the player stays away (egg_task) */
    EGG_ANIM_DIE = 1,
    EGG_ANIM_OPEN = 2,
    EGG_ANIM_HURT_CLOSED = 3,
    EGG_ANIM_HURT_OPEN = 4,
    HUGGER_ANIM_DIE = 9,
    HUGGER_ANIM_LEAP = 10, /* actor_start_attack, actor_spawn_from_object, egg_release_hugger */
    HUGGER_ANIM_OVERLAY_LOWER = 11, /* the face-hugger overlay's two halves (hugger_overlay_start) */
    HUGGER_ANIM_OVERLAY_UPPER = 12,
    BURSTER_ANIM_DIE = 11,
    BURSTER_ANIM_LEAP = 12,
    BURSTER_ANIM_EMERGE = 13, /* colonist_release_burster */
    BAMBI_ANIM_DIE = 11,
    DOG_ANIM_ATTACK = 11,
    DOG_ANIM_DIE = 13,
    DOG_CEILING_ANIM_FALL = 10, /* actor_die on the ceiling (the kind changes, the table stays) */
    WARRIOR_ANIM_HURT_FRONT = 8, /* also the chest-burster's, bambi's, the dog's and the synthetic's */
    WARRIOR_ANIM_HURT_BACK = 9, /* the same kinds */
    WARRIOR_ANIM_DIE = 10, /* also the soldier's */
    WARRIOR_ANIM_ATTACK_CLOSE = 11, /* also the handler's attack */
    WARRIOR_ANIM_ATTACK = 12, /* also bambi's attack */
    WARRIOR_CEILING_ANIM_FALL = 8, /* actor_die on the ceiling */
    GUARD_ANIM_HURT = 8, /* the guard's and soldier's; also the face-hugger's and handler's */
    GUARD_ANIM_FIRE = 9, /* guard and soldier */
    GUARD_ANIM_DIE = 10,
    SYNTH_ANIM_ATTACK = 10, /* also the dog's close attack */
    SYNTH_ANIM_DIE = 12,
    HANDLER_ANIM_DIE = 9,
    HANDLER_ANIM_GRENADE = 10, /* with ACTOR_FLAG_GRENADE */
    QUEEN_ANIM_DORMANT = 8, /* at spawn, and when the player stays away (queen_task) */
    QUEEN_ANIM_WAKE = 9,
    QUEEN_ANIM_RISE = 10,
    QUEEN_ANIM_STRIKE = 11,
    QUEEN_ANIM_BITE = 12,
    QUEEN_ANIM_SWIPE = 14, /* mirrored half the time */
    QUEEN_ANIM_DIE = 15,
    COLONIST_ANIM_DIE = 0x19
};

#endif
