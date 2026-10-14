/*
 * Sprite animation tables of the hand-written module asm/sprite_anims.s (retail .text
 * 0x800558e8..0x800588bb). Each animation is a frame list followed by its script; the
 * trackEntry_t tables in .data (<TRACK.H>) pair them. Requires <sys/types.h> and <SPRITE.H>.
 * Names are the labels' addresses until their animations are identified.
 */
#ifndef ANIMS_H
#define ANIMS_H

extern spriteFrame_t g_pulseRifleAnim0Frames[1];
extern u_short       g_pulseRifleAnim0Script[]; /* animation 0 */
extern spriteFrame_t g_pulseRifleAnim2Frames[6];
extern u_short       g_pulseRifleAnim2Script[]; /* animation 1 */
extern spriteFrame_t g_pulseRifleAnim3Frames[4];
extern u_short       g_pulseRifleAnim3Script[]; /* animation 2 */
extern spriteFrame_t g_pulseRifleAnim1Frames[3];
extern u_short       g_pulseRifleAnim1Script[]; /* animation 3 */
extern spriteFrame_t g_shotgunAnim0Frames[1];
extern u_short       g_shotgunAnim0Script[]; /* animation 4 */
extern spriteFrame_t g_shotgunAnim1Frames[8];
extern u_short       g_shotgunAnim1Script[]; /* animation 5 */
extern spriteFrame_t g_pistolAnim0Frames[1];
extern u_short       g_pistolAnim0Script[]; /* animation 6 */
extern spriteFrame_t g_pistolAnim1Frames[3];
extern u_short       g_pistolAnim1Script[]; /* animation 7 */
extern spriteFrame_t g_pistolAnim2Frames[3];
extern u_short       g_pistolAnim2Script[]; /* animation 8 */
extern spriteFrame_t g_flamethrowerAnim0Frames[1];
extern u_short       g_flamethrowerAnim0Script[]; /* animation 9 */
extern spriteFrame_t g_flamethrowerAnim1Frames[1];
extern u_short       g_flamethrowerAnim1Script[]; /* animation 10 */
extern spriteFrame_t g_flamethrowerAnim2Frames[5];
extern u_short       g_flamethrowerAnim2Script[]; /* animation 11 */
extern spriteFrame_t g_smartGunAnim1Frames[4];
extern u_short       g_smartGunAnim1Script[]; /* animation 12 */
extern spriteFrame_t g_smartGunAnim2Frames[5];
extern u_short       g_smartGunAnim2Script[]; /* animation 13 */
extern spriteFrame_t g_smartGunAnim0Frames[1];
extern u_short       g_smartGunAnim0Script[]; /* animation 14 */
extern spriteFrame_t g_eggAnim0Frames[3];
extern u_short       g_eggAnim0Script[]; /* animation 15 */
extern spriteFrame_t g_eggAnim1Frames[3];
extern u_short       g_eggAnim1Script[]; /* animation 16 */
extern spriteFrame_t g_eggAnim2Frames[5];
extern u_short       g_eggAnim2Script[]; /* animation 17 */
extern spriteFrame_t g_eggAnim3Frames[6];
extern u_short       g_eggAnim3Script[]; /* animation 18 */
extern spriteFrame_t g_eggAnim4Frames[8];
extern u_short       g_eggAnim4Script[]; /* animation 19 */
extern spriteFrame_t g_eggAnim5Frames[8];
extern u_short       g_eggAnim5Script[]; /* animation 20 */
extern spriteFrame_t g_huggerAnim0Frames[4];
extern u_short       g_huggerAnim0Script[]; /* animation 21 */
extern spriteFrame_t g_huggerAnim4Frames[4];
extern u_short       g_huggerAnim4Script[]; /* animation 22 */
extern spriteFrame_t g_huggerAnim2Frames[4];
extern u_short       g_huggerAnim2Script[]; /* animation 23 */
extern spriteFrame_t g_huggerAnim1Frames[4];
extern u_short       g_huggerAnim1Script[]; /* animation 24 */
extern spriteFrame_t g_huggerAnim3Frames[4];
extern u_short       g_huggerAnim3Script[]; /* animation 25 */
extern spriteFrame_t g_huggerAnim6Frames[6];
extern u_short       g_huggerAnim6Script[]; /* animation 26 */
extern spriteFrame_t g_huggerAnim7Frames[8];
extern u_short       g_huggerAnim7Script[]; /* animation 27 */
extern spriteFrame_t g_huggerAnim5Frames[4];
extern u_short       g_huggerAnim5Script[]; /* animation 28 */
extern spriteFrame_t g_warriorAnim5Frames[7];
extern u_short       g_warriorAnim5Script[]; /* animation 29 */
extern spriteFrame_t g_warriorAnim3Frames[7];
extern u_short       g_warriorAnim3Script[]; /* animation 30 */
extern spriteFrame_t g_warriorAnim4Frames[7];
extern u_short       g_warriorAnim4Script[]; /* animation 31 */
extern spriteFrame_t g_warriorAnim7Frames[7];
extern u_short       g_warriorAnim7Script[]; /* animation 32 */
extern spriteFrame_t g_warriorAnim6Frames[7];
extern u_short       g_warriorAnim6Script[]; /* animation 33 */
extern spriteFrame_t g_warriorAnim0Frames[3];
extern u_short       g_warriorAnim0Script[]; /* animation 34 */
extern spriteFrame_t g_warriorAnim1Frames[3];
extern u_short       g_warriorAnim1Script[]; /* animation 35 */
extern spriteFrame_t g_warriorAnim2Frames[6];
extern u_short       g_warriorAnim2Script[]; /* animation 36 */
extern spriteFrame_t g_warriorAnim8Frames[4];
extern u_short       g_warriorAnim8Script[]; /* animation 37 */
extern spriteFrame_t g_warriorAnim9Frames[7];
extern u_short       g_warriorAnim9Script[]; /* animation 38 */
extern spriteFrame_t g_handlerAnim0Frames[7];
extern u_short       g_handlerAnim0Script[]; /* animation 39 */
extern spriteFrame_t g_handlerAnim1Frames[7];
extern u_short       g_handlerAnim1Script[]; /* animation 40 */
extern spriteFrame_t g_handlerAnim2Frames[7];
extern u_short       g_handlerAnim2Script[]; /* animation 41 */
extern spriteFrame_t g_handlerAnim3Frames[7];
extern u_short       g_handlerAnim3Script[]; /* animation 42 */
extern spriteFrame_t g_handlerAnim4Frames[7];
extern u_short       g_handlerAnim4Script[]; /* animation 43 */
extern spriteFrame_t g_handlerAnim5Frames[3];
extern u_short       g_handlerAnim5Script[]; /* animation 44 */
extern spriteFrame_t g_handlerAnim6Frames[5];
extern u_short       g_handlerAnim6Script[]; /* animation 45 */
extern spriteFrame_t g_handlerAnim7Frames[8];
extern u_short       g_handlerAnim7Script[]; /* animation 46 */
extern spriteFrame_t g_handlerAnim8Frames[3];
extern u_short       g_handlerAnim8Script[]; /* animation 47 */
extern spriteFrame_t g_bursterAnim0Frames[4];
extern u_short       g_bursterAnim0Script[]; /* animation 48 */
extern spriteFrame_t g_bursterAnim1Frames[8];
extern u_short       g_bursterAnim1Script[]; /* animation 49 */
extern spriteFrame_t g_bursterAnim2Frames[4];
extern u_short       g_bursterAnim2Script[]; /* animation 50 */
extern spriteFrame_t g_bursterAnim3Frames[4];
extern u_short       g_bursterAnim3Script[]; /* animation 51 */
extern spriteFrame_t g_bursterAnim4Frames[6];
extern u_short       g_bursterAnim4Script[]; /* animation 52 */
extern spriteFrame_t g_bursterAnim5Frames[6];
extern u_short       g_bursterAnim5Script[]; /* animation 53 */
extern spriteFrame_t g_bursterAnim6Frames[4];
extern u_short       g_bursterAnim6Script[]; /* animation 54 */
extern spriteFrame_t g_bursterAnim7Frames[4];
extern u_short       g_bursterAnim7Script[]; /* animation 55 */
extern spriteFrame_t g_bursterAnim8Frames[4];
extern u_short       g_bursterAnim8Script[]; /* animation 56 */
extern spriteFrame_t g_bursterAnim9Frames[4];
extern u_short       g_bursterAnim9Script[]; /* animation 57 */
extern spriteFrame_t g_bursterAnim10Frames[4];
extern u_short       g_bursterAnim10Script[]; /* animation 58 */
extern spriteFrame_t g_bambiAnim0Frames[6];
extern u_short       g_bambiAnim0Script[]; /* animation 59 */
extern spriteFrame_t g_bambiAnim1Frames[3];
extern u_short       g_bambiAnim1Script[]; /* animation 60 */
extern spriteFrame_t g_bambiAnim2Frames[3];
extern u_short       g_bambiAnim2Script[]; /* animation 61 */
extern spriteFrame_t g_bambiAnim3Frames[7];
extern u_short       g_bambiAnim3Script[]; /* animation 62 */
extern spriteFrame_t g_bambiAnim4Frames[7];
extern u_short       g_bambiAnim4Script[]; /* animation 63 */
extern spriteFrame_t g_bambiAnim5Frames[7];
extern u_short       g_bambiAnim5Script[]; /* animation 64 */
extern spriteFrame_t g_bambiAnim6Frames[7];
extern u_short       g_bambiAnim6Script[]; /* animation 65 */
extern spriteFrame_t g_bambiAnim7Frames[7];
extern u_short       g_bambiAnim7Script[]; /* animation 66 */
extern spriteFrame_t g_bambiAnim8Frames[6];
extern u_short       g_bambiAnim8Script[]; /* animation 67 */
extern spriteFrame_t g_bambiAnim9Frames[6];
extern u_short       g_bambiAnim9Script[]; /* animation 68 */
extern spriteFrame_t g_soldierAnim0Frames[4];
extern u_short       g_soldierAnim0Script[]; /* animation 69 */
extern spriteFrame_t g_soldierAnim1Frames[3];
extern u_short       g_soldierAnim1Script[]; /* animation 70 */
extern spriteFrame_t g_soldierAnim2Frames[5];
extern u_short       g_soldierAnim2Script[]; /* animation 71 */
extern spriteFrame_t g_soldierAnim3Frames[7];
extern u_short       g_soldierAnim3Script[]; /* animation 72 */
extern spriteFrame_t g_soldierAnim5Frames[7];
extern u_short       g_soldierAnim5Script[]; /* animation 73 */
extern spriteFrame_t g_soldierAnim4Frames[7];
extern u_short       g_soldierAnim4Script[]; /* animation 74 */
extern spriteFrame_t g_soldierAnim6Frames[7];
extern u_short       g_soldierAnim6Script[]; /* animation 75 */
extern spriteFrame_t g_soldierAnim7Frames[7];
extern u_short       g_soldierAnim7Script[]; /* animation 76 */
extern spriteFrame_t g_guardAnim0Frames[3];
extern u_short       g_guardAnim0Script[]; /* animation 77 */
extern spriteFrame_t g_guardAnim1Frames[5];
extern u_short       g_guardAnim1Script[]; /* animation 78 */
extern spriteFrame_t g_guardAnim2Frames[3];
extern u_short       g_guardAnim2Script[]; /* animation 79 */
extern spriteFrame_t g_guardAnim4Frames[7];
extern u_short       g_guardAnim4Script[]; /* animation 80 */
extern spriteFrame_t g_guardAnim3Frames[7];
extern u_short       g_guardAnim3Script[]; /* animation 81 */
extern spriteFrame_t g_guardAnim5Frames[7];
extern u_short       g_guardAnim5Script[]; /* animation 82 */
extern spriteFrame_t g_guardAnim7Frames[7];
extern u_short       g_guardAnim7Script[]; /* animation 83 */
extern spriteFrame_t g_guardAnim6Frames[7];
extern u_short       g_guardAnim6Script[]; /* animation 84 */
extern spriteFrame_t g_synthAnim0Frames[3];
extern u_short       g_synthAnim0Script[]; /* animation 85 */
extern spriteFrame_t g_synthAnim1Frames[2];
extern u_short       g_synthAnim1Script[]; /* animation 86 */
extern spriteFrame_t g_synthAnim2Frames[4];
extern u_short       g_synthAnim2Script[]; /* animation 87 */
extern spriteFrame_t g_synthAnim3Frames[4];
extern u_short       g_synthAnim3Script[]; /* animation 88 */
extern spriteFrame_t g_synthAnim4Frames[3];
extern u_short       g_synthAnim4Script[]; /* animation 89 */
extern spriteFrame_t g_synthAnim6Frames[7];
extern u_short       g_synthAnim6Script[]; /* animation 90 */
extern spriteFrame_t g_synthAnim5Frames[7];
extern u_short       g_synthAnim5Script[]; /* animation 91 */
extern spriteFrame_t g_synthAnim7Frames[7];
extern u_short       g_synthAnim7Script[]; /* animation 92 */
extern spriteFrame_t g_synthAnim8Frames[7];
extern u_short       g_synthAnim8Script[]; /* animation 93 */
extern spriteFrame_t g_synthAnim11Frames[7];
extern u_short       g_synthAnim11Script[]; /* animation 94 */
extern spriteFrame_t g_synthAnim12Frames[7];
extern u_short       g_synthAnim12Script[]; /* animation 95 */
extern spriteFrame_t g_synthAnim9Frames[7];
extern u_short       g_synthAnim9Script[]; /* animation 96 */
extern spriteFrame_t g_synthAnim10Frames[7];
extern u_short       g_synthAnim10Script[]; /* animation 97 */
extern spriteFrame_t g_dogAnim0Frames[3];
extern u_short       g_dogAnim0Script[]; /* animation 98 */
extern spriteFrame_t g_dogAnim1Frames[3];
extern u_short       g_dogAnim1Script[]; /* animation 99 */
extern spriteFrame_t g_dogAnim2Frames[5];
extern u_short       g_dogAnim2Script[]; /* animation 100 */
extern spriteFrame_t g_dogAnim3Frames[5];
extern u_short       g_dogAnim3Script[]; /* animation 101 */
extern spriteFrame_t g_dogAnim4Frames[7];
extern u_short       g_dogAnim4Script[]; /* animation 102 */
extern spriteFrame_t g_dogAnim5Frames[7];
extern u_short       g_dogAnim5Script[]; /* animation 103 */
extern spriteFrame_t g_dogAnim6Frames[7];
extern u_short       g_dogAnim6Script[]; /* animation 104 */
extern spriteFrame_t g_dogAnim7Frames[7];
extern u_short       g_dogAnim7Script[]; /* animation 105 */
extern spriteFrame_t g_dogAnim8Frames[7];
extern u_short       g_dogAnim8Script[]; /* animation 106 */
extern spriteFrame_t g_dogAnim9Frames[8];
extern u_short       g_dogAnim9Script[]; /* animation 107 */
extern spriteFrame_t g_dogAnim10Frames[6];
extern u_short       g_dogAnim10Script[]; /* animation 108 */
extern spriteFrame_t g_fingersAnim1Frames[4];
extern u_short       g_fingersAnim1Script[]; /* animation 109 */
extern spriteFrame_t g_fingersAnim0Frames[4];
extern u_short       g_fingersAnim0Script[]; /* animation 110 */
extern spriteFrame_t g_queenAnim0Frames[4];
extern u_short       g_queenAnim0Script[]; /* animation 111 */
extern spriteFrame_t g_queenAnim1Frames[11];
extern u_short       g_queenAnim1Script[]; /* animation 112 */
extern spriteFrame_t g_queenAnim2Frames[10];
extern u_short       g_queenAnim2Script[]; /* animation 113 */
extern spriteFrame_t g_queenAnim3Frames[6];
extern u_short       g_queenAnim3Script[]; /* animation 114 */
extern spriteFrame_t g_queenAnim4Frames[11];
extern u_short       g_queenAnim4Script[]; /* animation 115 */
extern spriteFrame_t g_queenAnim5Frames[11];
extern u_short       g_queenAnim5Script[]; /* animation 116 */
extern spriteFrame_t g_queenAnim6Frames[6];
extern u_short       g_queenAnim6Script[]; /* animation 117 */
extern spriteFrame_t g_queenAnim7Frames[6];
extern u_short       g_queenAnim7Script[]; /* animation 118 */
extern spriteFrame_t g_queenAnim8Frames[6];
extern u_short       g_queenAnim8Script[]; /* animation 119 */
extern spriteFrame_t g_queenAnim9Frames[3];
extern u_short       g_queenAnim9Script[]; /* animation 120 */
extern spriteFrame_t g_queenAnim10Frames[4];
extern u_short       g_queenAnim10Script[]; /* animation 121 */
extern spriteFrame_t g_queenAnim11Frames[7];
extern u_short       g_queenAnim11Script[]; /* animation 122 */
extern spriteFrame_t g_warriorCeilingAnim2Frames[7];
extern u_short       g_warriorCeilingAnim2Script[]; /* animation 123 */
extern spriteFrame_t g_warriorCeilingAnim3Frames[7];
extern u_short       g_warriorCeilingAnim3Script[]; /* animation 124 */
extern spriteFrame_t g_warriorCeilingAnim0Frames[5];
extern u_short       g_warriorCeilingAnim0Script[]; /* animation 125 */
extern spriteFrame_t g_warriorCeilingAnim1Frames[3];
extern u_short       g_warriorCeilingAnim1Script[]; /* animation 126 */
extern spriteFrame_t g_warriorCeilingAnim4Frames[4];
extern u_short       g_warriorCeilingAnim4Script[]; /* animation 127 */
extern spriteFrame_t g_colonistAnim0Frames[1];
extern u_short       g_colonistAnim0Script[]; /* animation 128 */
extern spriteFrame_t g_colonistAnim1Frames[1];
extern u_short       g_colonistAnim1Script[]; /* animation 129 */
extern spriteFrame_t g_colonistAnim2Frames[1];
extern u_short       g_colonistAnim2Script[]; /* animation 130 */
extern spriteFrame_t g_colonistAnim3Frames[1];
extern u_short       g_colonistAnim3Script[]; /* animation 131 */
extern spriteFrame_t g_colonistAnim4Frames[1];
extern u_short       g_colonistAnim4Script[]; /* animation 132 */
extern spriteFrame_t g_colonistAnim5Frames[1];
extern u_short       g_colonistAnim5Script[]; /* animation 133 */
extern spriteFrame_t g_colonistAnim6Frames[1];
extern u_short       g_colonistAnim6Script[]; /* animation 134 */
extern spriteFrame_t g_colonistAnim7Frames[1];
extern u_short       g_colonistAnim7Script[]; /* animation 135 */
extern spriteFrame_t g_colonistAnim8Frames[1];
extern u_short       g_colonistAnim8Script[]; /* animation 136 */
extern spriteFrame_t g_colonistAnim9Frames[1];
extern u_short       g_colonistAnim9Script[]; /* animation 137 */
extern spriteFrame_t g_colonistAnim10Frames[2];
extern u_short       g_colonistAnim10Script[]; /* animation 138 */
extern spriteFrame_t g_colonistAnim11Frames[2];
extern u_short       g_colonistAnim11Script[]; /* animation 139 */
extern spriteFrame_t g_colonistAnim12Frames[2];
extern u_short       g_colonistAnim12Script[]; /* animation 140 */
extern spriteFrame_t g_colonistAnim13Frames[2];
extern u_short       g_colonistAnim13Script[]; /* animation 141 */
extern spriteFrame_t g_colonistAnim14Frames[2];
extern u_short       g_colonistAnim14Script[]; /* animation 142 */
extern spriteFrame_t g_colonistAnim15Frames[4];
extern u_short       g_colonistAnim15Script[]; /* animation 143 */
extern spriteFrame_t g_colonistAnim16Frames[4];
extern u_short       g_colonistAnim16Script[]; /* animation 144 */
extern spriteFrame_t g_colonistAnim17Frames[7];
extern u_short       g_colonistAnim17Script[]; /* animation 145 */
extern spriteFrame_t g_colonistAnim18Frames[1];
extern u_short       g_colonistAnim18Script[]; /* animation 146 */
extern spriteFrame_t g_colonistAnim19Frames[1];
extern u_short       g_colonistAnim19Script[]; /* animation 147 */
extern spriteFrame_t g_colonistAnim20Frames[1];
extern u_short       g_colonistAnim20Script[]; /* animation 148 */
extern spriteFrame_t g_colonistAnim21Frames[1];
extern u_short       g_colonistAnim21Script[]; /* animation 149 */
extern spriteFrame_t g_colonistAnim22Frames[1];
extern u_short       g_colonistAnim22Script[]; /* animation 150 */
extern spriteFrame_t g_dogCeilingAnim0Frames[7];
extern u_short       g_dogCeilingAnim0Script[]; /* animation 151 */
extern spriteFrame_t g_dogCeilingAnim1Frames[7];
extern u_short       g_dogCeilingAnim1Script[]; /* animation 152 */
extern spriteFrame_t g_dogCeilingAnim2Frames[3];
extern u_short       g_dogCeilingAnim2Script[]; /* animation 153 */
extern spriteFrame_t g_dogCeilingAnim3Frames[4];
extern u_short       g_dogCeilingAnim3Script[]; /* animation 154 */
extern spriteFrame_t g_dogCeilingAnim4Frames[5];
extern u_short       g_dogCeilingAnim4Script[]; /* animation 155 */

/* Indices into an actor's animation table (actor_t.animations, Actor_PlayAnimation) that
 * the code names by number beyond the eight facing octants. names: invented, from the
 * code that plays them */
enum {
    COLONIST_ANIM_COCOONED = 8,    /* + octant: struggling in the cocoon (Actor_ColonistTask) */
    COLONIST_ANIM_HURT = 0x10,     /* + octant (Actor_Hurt) */
    COLONIST_ANIM_REACH = 0x18,    /* facing the player: reaches out, then waits (Actor_ColonistTask) */
    COLONIST_ANIM_BURST = 0x1a,    /* the chest-burster breaks out (Actor_ColonistReleaseBurster follows) */
    COLONIST_ANIM_DEAD = 0x1b,     /* + octant */
    QUEEN_ANIM_HURT = 0x10,        /* Actor_Hurt in attack phases 0 and 1 */
    QUEEN_ANIM_KNOCKED_BACK = 0x11 /* Actor_Hurt from phase 3 on, with Actor_KnockBack */
};

/* The rest of the numbered animations, per kind. Every creature table starts with
 * ACTOR_ANIM_FACING + shown octant (Actor_FaceView); the entries from 8 on differ by kind.
 * HURT_FRONT plays when the actor is hit seen from the front (shown octants 7, 0 and 1),
 * HURT_BACK otherwise; Actor_ToFloor also plays one of them as the actor drops from the
 * ceiling. Kinds that share a case label share a value (noted). names: invented, from the
 * code that plays them */
enum {
    ACTOR_ANIM_FACING = 0,
    EGG_ANIM_CLOSED = 0, /* at spawn, and when the player stays away (Actor_EggTask) */
    EGG_ANIM_DIE = 1,
    EGG_ANIM_OPEN = 2,
    EGG_ANIM_HURT_CLOSED = 3,
    EGG_ANIM_HURT_OPEN = 4,
    HUGGER_ANIM_DIE = 9,
    HUGGER_ANIM_LEAP = 10,          /* Actor_StartAttack, Actor_SpawnFromObject, Actor_EggReleaseHugger */
    HUGGER_ANIM_OVERLAY_LOWER = 11, /* the face-hugger overlay's two halves (Actor_StartHuggerOverlay) */
    HUGGER_ANIM_OVERLAY_UPPER = 12,
    BURSTER_ANIM_DIE = 11,
    BURSTER_ANIM_LEAP = 12,
    BURSTER_ANIM_EMERGE = 13, /* Actor_ColonistReleaseBurster */
    BAMBI_ANIM_DIE = 11,
    DOG_ANIM_ATTACK = 11,
    DOG_ANIM_DIE = 13,
    DOG_CEILING_ANIM_FALL = 10,     /* Actor_Die on the ceiling (the kind changes, the table stays) */
    WARRIOR_ANIM_HURT_FRONT = 8,    /* also the chest-burster's, bambi's, the dog's and the synthetic's */
    WARRIOR_ANIM_HURT_BACK = 9,     /* the same kinds */
    WARRIOR_ANIM_DIE = 10,          /* also the soldier's */
    WARRIOR_ANIM_ATTACK_CLOSE = 11, /* also the handler's attack */
    WARRIOR_ANIM_ATTACK = 12,       /* also bambi's attack */
    WARRIOR_CEILING_ANIM_FALL = 8,  /* Actor_Die on the ceiling */
    GUARD_ANIM_HURT = 8,            /* the guard's and soldier's; also the face-hugger's and handler's */
    GUARD_ANIM_FIRE = 9,            /* guard and soldier */
    GUARD_ANIM_DIE = 10,
    SYNTH_ANIM_ATTACK = 10, /* also the dog's close attack */
    SYNTH_ANIM_DIE = 12,
    HANDLER_ANIM_DIE = 9,
    HANDLER_ANIM_GRENADE = 10, /* with ACTOR_FLAG_GRENADE */
    QUEEN_ANIM_DORMANT = 8,    /* at spawn, and when the player stays away (Actor_QueenTask) */
    QUEEN_ANIM_WAKE = 9,
    QUEEN_ANIM_RISE = 10,
    QUEEN_ANIM_STRIKE = 11,
    QUEEN_ANIM_BITE = 12,
    QUEEN_ANIM_SWIPE = 14, /* mirrored half the time */
    QUEEN_ANIM_DIE = 15,
    COLONIST_ANIM_DIE = 0x19
};

#endif
