/*
 * An entry of the CD file table at 0x8006a5f8 (171 entries, 36 bytes each): the
 * file's path, the size and heap buffer of its last load (load_file loads it), and
 * the CdlFILE that CdSearchFile fills. Requires <sys/types.h> and <LIBCD.H>.
 * Member names invented.
 */
#ifndef HEAPFILE_H
#define HEAPFILE_H

/* bytes per CD sector in the 2048-byte read mode load_file uses. name: invented */
#define CD_SECTOR_SIZE 2048

typedef struct HeapFile {
    const char *name;
    unsigned int loaded_size;
    void *buffer;
    CdlFILE file;
} HeapFile;

/* g_file_table indices. names: the files' CD paths (g_file_table's strings) */
typedef enum FileId {
    FILE_OBJ3D_BND,          /* 0 \GFX\OBJ3D.BND */
    FILE_PULSE_BND,          /* 1 \GFX\PULSE.BND */
    FILE_MM9_BND,            /* 2 \GFX\MM9.BND */
    FILE_SHOTGUN_BND,        /* 3 \GFX\SHOTGUN.BND */
    FILE_FLAME_BND,          /* 4 \GFX\FLAME.BND */
    FILE_SMART_BND,          /* 5 \GFX\SMART.BND */
    FILE_DOG_BND,            /* 6 \NME\DOG.BND */
    FILE_HUGGER_BND,         /* 7 \NME\HUGGER.BND */
    FILE_WAR_BND,            /* 8 \NME\WAR.BND */
    FILE_EGGS_BND,           /* 9 \NME\EGGS.BND */
    FILE_HANDLER_BND,        /* 10 \NME\HANDLER.BND */
    FILE_SYNTH_BND,          /* 11 \NME\SYNTH.BND */
    FILE_GUARD_BND,          /* 12 \NME\GUARD.BND */
    FILE_SOLDIER_BND,        /* 13 \NME\SOLDIER.BND */
    FILE_BURSTER_BND,        /* 14 \NME\BURSTER.BND */
    FILE_BAMBI_BND,          /* 15 \NME\BAMBI.BND */
    FILE_FINGERS_BND,        /* 16 \NME\FINGERS.BND */
    FILE_WARCEIL_BND,        /* 17 \NME\WARCEIL.BND */
    FILE_QUEEN_BND,          /* 18 \NME\QUEEN.BND */
    FILE_DOGCEIL_BND,        /* 19 \NME\DOGCEIL.BND */
    FILE_COLONIST_BND,       /* 20 \NME\COLONIST.BND */
    FILE_111GFX_BIN,         /* 21 \SECT11\111GFX.BIN */
    FILE_L111LEV_MAP,        /* 22 \SECT11\L111LEV.MAP */
    FILE_112GFX_BIN,         /* 23 \SECT11\112GFX.BIN */
    FILE_L112LEV_MAP,        /* 24 \SECT11\L112LEV.MAP */
    FILE_113GFX_BIN,         /* 25 \SECT11\113GFX.BIN */
    FILE_L113LEV_MAP,        /* 26 \SECT11\L113LEV.MAP */
    FILE_114GFX_BIN,         /* 27 \SECT11\114GFX.BIN */
    FILE_L114LEV_MAP,        /* 28 \SECT11\L114LEV.MAP */
    FILE_115GFX_BIN,         /* 29 \SECT11\115GFX.BIN */
    FILE_L115LEV_MAP,        /* 30 \SECT11\L115LEV.MAP */
    FILE_121GFX_BIN,         /* 31 \SECT11\121GFX.BIN */
    FILE_L121LEV_MAP,        /* 32 \SECT11\L121LEV.MAP */
    FILE_122GFX_BIN,         /* 33 \SECT11\122GFX.BIN */
    FILE_L122LEV_MAP,        /* 34 \SECT11\L122LEV.MAP */
    FILE_123GFX_BIN,         /* 35 \SECT11\123GFX.BIN */
    FILE_L123LEV_MAP,        /* 36 \SECT11\L123LEV.MAP */
    FILE_131GFX_BIN,         /* 37 \SECT11\131GFX.BIN */
    FILE_L131LEV_MAP,        /* 38 \SECT11\L131LEV.MAP */
    FILE_132GFX_BIN,         /* 39 \SECT11\132GFX.BIN */
    FILE_L132LEV_MAP,        /* 40 \SECT11\L132LEV.MAP */
    FILE_141GFX_BIN,         /* 41 \SECT12\141GFX.BIN */
    FILE_L141LEV_MAP,        /* 42 \SECT12\L141LEV.MAP */
    FILE_151GFX_BIN,         /* 43 \SECT12\151GFX.BIN */
    FILE_L151LEV_MAP,        /* 44 \SECT12\L151LEV.MAP */
    FILE_152GFX_BIN,         /* 45 \SECT12\152GFX.BIN */
    FILE_L152LEV_MAP,        /* 46 \SECT12\L152LEV.MAP */
    FILE_153GFX_BIN,         /* 47 \SECT12\153GFX.BIN */
    FILE_L153LEV_MAP,        /* 48 \SECT12\L153LEV.MAP */
    FILE_154GFX_BIN,         /* 49 \SECT12\154GFX.BIN */
    FILE_L154LEV_MAP,        /* 50 \SECT12\L154LEV.MAP */
    FILE_155GFX_BIN,         /* 51 \SECT12\155GFX.BIN */
    FILE_L155LEV_MAP,        /* 52 \SECT12\L155LEV.MAP */
    FILE_161GFX_BIN,         /* 53 \SECT12\161GFX.BIN */
    FILE_L161LEV_MAP,        /* 54 \SECT12\L161LEV.MAP */
    FILE_162GFX_BIN,         /* 55 \SECT12\162GFX.BIN */
    FILE_L162LEV_MAP,        /* 56 \SECT12\L162LEV.MAP */
    FILE_211GFX_BIN,         /* 57 \SECT21\211GFX.BIN */
    FILE_L211LEV_MAP,        /* 58 \SECT21\L211LEV.MAP */
    FILE_212GFX_BIN,         /* 59 \SECT21\212GFX.BIN */
    FILE_L212LEV_MAP,        /* 60 \SECT21\L212LEV.MAP */
    FILE_213GFX_BIN,         /* 61 \SECT21\213GFX.BIN */
    FILE_L213LEV_MAP,        /* 62 \SECT21\L213LEV.MAP */
    FILE_222GFX_BIN,         /* 63 \SECT21\222GFX.BIN */
    FILE_L222LEV_MAP,        /* 64 \SECT21\L222LEV.MAP */
    FILE_231GFX_BIN,         /* 65 \SECT21\231GFX.BIN */
    FILE_L231LEV_MAP,        /* 66 \SECT21\L231LEV.MAP */
    FILE_232GFX_BIN,         /* 67 \SECT21\232GFX.BIN */
    FILE_L232LEV_MAP,        /* 68 \SECT21\L232LEV.MAP */
    FILE_242GFX_BIN,         /* 69 \SECT22\242GFX.BIN */
    FILE_L242LEV_MAP,        /* 70 \SECT22\L242LEV.MAP */
    FILE_243GFX_BIN,         /* 71 \SECT22\243GFX.BIN */
    FILE_L243LEV_MAP,        /* 72 \SECT22\L243LEV.MAP */
    FILE_262GFX_BIN,         /* 73 \SECT22\262GFX.BIN */
    FILE_L262LEV_MAP,        /* 74 \SECT22\L262LEV.MAP */
    FILE_263GFX_BIN,         /* 75 \SECT22\263GFX.BIN */
    FILE_L263LEV_MAP,        /* 76 \SECT22\L263LEV.MAP */
    FILE_311GFX_BIN,         /* 77 \SECT31\311GFX.BIN */
    FILE_L311LEV_MAP,        /* 78 \SECT31\L311LEV.MAP */
    FILE_321GFX_BIN,         /* 79 \SECT31\321GFX.BIN */
    FILE_L321LEV_MAP,        /* 80 \SECT31\L321LEV.MAP */
    FILE_331GFX_BIN,         /* 81 \SECT31\331GFX.BIN */
    FILE_L331LEV_MAP,        /* 82 \SECT31\L331LEV.MAP */
    FILE_322GFX_BIN,         /* 83 \SECT31\322GFX.BIN */
    FILE_L322LEV_MAP,        /* 84 \SECT31\L322LEV.MAP */
    FILE_351GFX_BIN,         /* 85 \SECT32\351GFX.BIN */
    FILE_L351LEV_MAP,        /* 86 \SECT32\L351LEV.MAP */
    FILE_352GFX_BIN,         /* 87 \SECT32\352GFX.BIN */
    FILE_L352LEV_MAP,        /* 88 \SECT32\L352LEV.MAP */
    FILE_323GFX_BIN,         /* 89 \SECT31\323GFX.BIN */
    FILE_L323LEV_MAP,        /* 90 \SECT31\L323LEV.MAP */
    FILE_361GFX_BIN,         /* 91 \SECT32\361GFX.BIN */
    FILE_L361LEV_MAP,        /* 92 \SECT32\L361LEV.MAP */
    FILE_324GFX_BIN,         /* 93 \SECT31\324GFX.BIN */
    FILE_L324LEV_MAP,        /* 94 \SECT31\L324LEV.MAP */
    FILE_371GFX_BIN,         /* 95 \SECT32\371GFX.BIN */
    FILE_L371LEV_MAP,        /* 96 \SECT32\L371LEV.MAP */
    FILE_353GFX_BIN,         /* 97 \SECT32\353GFX.BIN */
    FILE_L353LEV_MAP,        /* 98 \SECT32\L353LEV.MAP */
    FILE_325GFX_BIN,         /* 99 \SECT31\325GFX.BIN */
    FILE_L325LEV_MAP,        /* 100 \SECT31\L325LEV.MAP */
    FILE_381GFX_BIN,         /* 101 \SECT32\381GFX.BIN */
    FILE_L381LEV_MAP,        /* 102 \SECT32\L381LEV.MAP */
    FILE_391GFX_BIN,         /* 103 \SECT32\391GFX.BIN */
    FILE_L391LEV_MAP,        /* 104 \SECT32\L391LEV.MAP */
    FILE_PICKGFX_BND,        /* 105 \GFX\PICKGFX.BND */
    FILE_PICKMOD_BND,        /* 106 \GFX\PICKMOD.BND */
    FILE_PNL0GFXU_BND,       /* 107 \LANGUAGE\PNL0GFXU.BND */
    FILE_PNL1GFXU_BND,       /* 108 \LANGUAGE\PNL1GFXU.BND */
    FILE_LOGOSGFX_BND,       /* 109 \GFX\LOGOSGFX.BND */
    FILE_FONT1GFX_BND,       /* 110 \GFX\FONT1GFX.BND */
    FILE_EXPLGFX_BND,        /* 111 \GFX\EXPLGFX.BND */
    FILE_PERMSFX_BIN,        /* 112 \SFX\PERMSFX.BIN */
    FILE_111SFX_BIN,         /* 113 \SFX\111SFX.BIN */
    FILE_112SFX_BIN,         /* 114 \SFX\112SFX.BIN */
    FILE_113SFX_BIN,         /* 115 \SFX\113SFX.BIN */
    FILE_122SFX_BIN,         /* 116 \SFX\122SFX.BIN */
    FILE_131SFX_BIN,         /* 117 \SFX\131SFX.BIN */
    FILE_114SFX_BIN,         /* 118 \SFX\114SFX.BIN */
    FILE_141SFX_BIN,         /* 119 \SFX\141SFX.BIN */
    FILE_115SFX_BIN,         /* 120 \SFX\115SFX.BIN */
    FILE_154SFX_BIN,         /* 121 \SFX\154SFX.BIN */
    FILE_155SFX_BIN,         /* 122 \SFX\155SFX.BIN */
    FILE_161SFX_BIN,         /* 123 \SFX\161SFX.BIN */
    FILE_162SFX_BIN,         /* 124 \SFX\162SFX.BIN */
    FILE_211SFX_BIN,         /* 125 \SFX\211SFX.BIN */
    FILE_212SFX_BIN,         /* 126 \SFX\212SFX.BIN */
    FILE_213SFX_BIN,         /* 127 \SFX\213SFX.BIN */
    FILE_222SFX_BIN,         /* 128 \SFX\222SFX.BIN */
    FILE_242SFX_BIN,         /* 129 \SFX\242SFX.BIN */
    FILE_231SFX_BIN,         /* 130 \SFX2\231SFX.BIN */
    FILE_232SFX_BIN,         /* 131 \SFX2\232SFX.BIN */
    FILE_243SFX_BIN,         /* 132 \SFX2\243SFX.BIN */
    FILE_262SFX_BIN,         /* 133 \SFX2\262SFX.BIN */
    FILE_263SFX_BIN,         /* 134 \SFX2\263SFX.BIN */
    FILE_311SFX_BIN,         /* 135 \SFX2\311SFX.BIN */
    FILE_321SFX_BIN,         /* 136 \SFX2\321SFX.BIN */
    FILE_331SFX_BIN,         /* 137 \SFX2\331SFX.BIN */
    FILE_351SFX_BIN,         /* 138 \SFX2\351SFX.BIN */
    FILE_352SFX_BIN,         /* 139 \SFX2\352SFX.BIN */
    FILE_371SFX_BIN,         /* 140 \SFX2\371SFX.BIN */
    FILE_353SFX_BIN,         /* 141 \SFX2\353SFX.BIN */
    FILE_381SFX_BIN,         /* 142 \SFX2\381SFX.BIN */
    FILE_391SFX_BIN,         /* 143 \SFX2\391SFX.BIN */
    FILE_STARTSFX_BIN,       /* 144 \SFX2\STARTSFX.BIN */
    FILE_INTRO_STR,          /* 145 \STR\INTRO.STR */
    FILE_PROBE_STR,          /* 146 \STR\PROBE.STR */
    FILE_ACCLAIM_STR,        /* 147 \STR\ACCLAIM.STR */
    FILE_DEATH1_STR,         /* 148 \STR\DEATH1.STR */
    FILE_DEATH2_STR,         /* 149 \STR\DEATH2.STR */
    FILE_DEATH3_STR,         /* 150 \STR\DEATH3.STR */
    FILE_DEATH4_STR,         /* 151 \STR\DEATH4.STR */
    FILE_DEATH5_STR,         /* 152 \STR\DEATH5.STR */
    FILE_DEATH6_STR,         /* 153 \STR\DEATH6.STR */
    FILE_LEGAL_STR,          /* 154 \STR\LEGAL.STR */
    FILE_S1_2_STR,           /* 155 \STR\S1_2.STR */
    FILE_S2_3_STR,           /* 156 \STR\S2_3.STR */
    FILE_GAMEOVER_STR,       /* 157 \STR\GAMEOVER.STR */
    FILE_CREDITS_STR,        /* 158 \STR\CREDITS.STR */
    FILE_FOX_STR,            /* 159 \STR\FOX.STR */
    FILE_COLONY_BS,          /* 160 \GFX\COLONY.BS */
    FILE_PRISHOLD_BS,        /* 161 \GFX\PRISHOLD.BS */
    FILE_BONESHIP_BS,        /* 162 \GFX\BONESHIP.BS */
    FILE_ICONS_IFF,          /* 163 \GFX\ICONS.IFF */
    FILE_PIRATE_IFF,         /* 164 \GFX\PIRATE.IFF */
    FILE_MISSIONU_TXT,       /* 165 \LANGUAGE\MISSIONU.TXT */
    FILE_CREDITS_TXT,        /* 166 \GFX\CREDITS.TXT */
    FILE_DEMO111_BIN,        /* 167 \GFX\DEMO111.BIN */
    FILE_DEMO211_BIN,        /* 168 \GFX\DEMO211.BIN */
    FILE_DEMO311_BIN,        /* 169 \GFX\DEMO311.BIN */
    FILE_CONTROL_BND,        /* 170 \GFX\CONTROL.BND */
    FILE_TABLE_COUNT
} FileId;

/* name: inferred from use: every resource load names its entry by index */
extern HeapFile g_file_table[FILE_TABLE_COUNT];

extern void load_file(HeapFile *file);

#endif
