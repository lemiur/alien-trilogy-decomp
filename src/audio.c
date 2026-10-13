/* Sound effects: SPU setup, sample-bank upload and voice allocation. */
#include <sys/types.h>
#include <LIBSPU.H>
#include <LIBGTE.H>
#include <AUDIO.H>

#define VOICE_COUNT 24
#define SAMPLE_COUNT 256
/* SpuInitMalloc's block count for g_spu_malloc_table. name: invented */
#define SPU_MALLOC_BLOCKS 16
/* Entries of g_sample_priority_list, and the priority its first class gets. names: invented */
#define SAMPLE_PRIORITY_LIST_LENGTH 30
#define SAMPLE_PRIORITY_TOP 3

/* A playing sound is identified by a handle: its voice in the low byte, the
 * voice's play serial above it. A handle goes stale once the voice is reused. */
#define HANDLE_SERIAL_SHIFT 8
#define SOUND_HANDLE(voice, serial) ((voice) | ((serial) << HANDLE_SERIAL_SHIFT))
#define HANDLE_VOICE_MASK 0xff
#define HANDLE_VOICE(handle) ((handle) & HANDLE_VOICE_MASK)
#define HANDLE_SERIAL(handle) ((handle) >> HANDLE_SERIAL_SHIFT)

/* VoiceEntry.expire of a looping sound: it never expires. */
#define EXPIRE_NEVER 0xffff

/* Once g_sound_clock reaches SOUND_CLOCK_REBASE, voice_play moves it and every live expiry
 * down by that much (16-bit wrap). name: invented */
#define SOUND_CLOCK_REBASE 0x8000

/* SampleSlot.flags. names: inferred from their effect on voice choice and expiry in voice_play */
#define SAMPLE_REUSE_VOICE 1 /* restart on the voice the sample last played on */
#define SAMPLE_FIXED_DURATION 2 /* duration counts clock ticks, not scaled by pitch */
#define SAMPLE_LOOP 4

/* Every voice attribute a key-on sets; SPU_VOICE_LSAX is added for loops. */
#define VOICE_ATTR_KEY_ON (SPU_VOICE_VOLL | SPU_VOICE_VOLR | SPU_VOICE_PITCH | SPU_VOICE_WDSA \
    | SPU_VOICE_ADSR_AMODE | SPU_VOICE_ADSR_SMODE | SPU_VOICE_ADSR_RMODE | SPU_VOICE_ADSR_AR \
    | SPU_VOICE_ADSR_DR | SPU_VOICE_ADSR_SR | SPU_VOICE_ADSR_RR | SPU_VOICE_ADSR_SL)

/* Scales a volume by the sound-effects level g_sfx_level (0 silent .. 9 and 10 full). */
#define SCALE_BY_SFX_LEVEL(volume) \
    switch (g_sfx_level) { \
    case 0: volume = 0; break; \
    case 1: volume /= 10; break; \
    case 2: volume /= 5; break; \
    case 3: volume /= 3; break; \
    case 4: volume = (volume / 5) * 2; break; \
    case 5: volume >>= 2; break; \
    case 6: volume = (volume / 5) * 3; break; \
    case 7: volume = (volume / 3) * 2; break; \
    case 8: volume = (volume / 5) * 4; break; \
    case 9: case 10: break; \
    }

typedef struct VoiceEntry {
    unsigned short expire;
    unsigned short priority;
    unsigned short sample;
    unsigned short serial;
    short volume_left;
    short volume_right;
} VoiceEntry;

/* Per-sample playback attributes, filled from the sample banks by sample_bank_load. */
typedef struct SampleSlot {
    unsigned long addr;
    unsigned short priority;
    unsigned short duration;
    unsigned short volume;
    unsigned short pitch;
    unsigned short flags;
    unsigned char voice;
    unsigned char reserved;
    unsigned short a_mode;
    unsigned short s_mode;
    unsigned short r_mode;
    unsigned short ar;
    unsigned short dr;
    unsigned short sr;
    unsigned short rr;
    unsigned short sl;
} SampleSlot;

extern VoiceEntry g_voice[VOICE_COUNT];
/* name: inferred from SampleSlot */
extern SampleSlot g_sample_slots[SAMPLE_COUNT];
/* SPU memory management table for SpuInitMalloc's 16 blocks.
 * name: inferred from that call */
extern char g_spu_malloc_table[];
/* identity-TODO: these LIBSPU exports are uncertified; each comment names the
 * Runtime 3.3 function that the arguments and the call site fit. */
extern void f_5c6b0(long on_off, unsigned long voice_bit); /* SpuSetReverbVoice */
extern unsigned long f_5caa4(long size); /* SpuMalloc */
extern unsigned long f_5c92c(unsigned long addr); /* SpuSetTransferStartAddr */
extern long f_5c808(long mode); /* SpuIsTransferCompleted */
extern void f_5c5d8(void);
extern long f_5c8d8(long);
extern long f_59388(SpuReverbAttr *); /* SpuSetReverbModeParam */
extern long f_5c738(long);

/* name: inferred from VoiceEntry.serial, which takes its value per key-on */
short g_play_serial = 0;

/* Sample ids by priority class: SAMPLE_PRIORITY_TOP for the first run, one lower after each -1.
 * name: inferred from sample_priorities_init, which assigns them */
short g_sample_priority_list[SAMPLE_PRIORITY_LIST_LENGTH] = {
    11, 24, 28, 96, 104, 102, 103, -1, 98, 99,
    100, 101, -1, 5, 6, 7, 8, 9, 10, 11,
    12, 13, 14, 15, 16, 17, 18, 19, 20, -1,
};

/* name: inferred from g_sound_clock, the time base of the sound voices' expiry */
unsigned short sound_clock_tick(void)
{
    return ++g_sound_clock;
}

/* name: inferred from its body, which clears every voice's expiry, priority and serial and the sound clock */
void voices_reset(void)
{
    VoiceEntry *entry = g_voice;
    int i;
    for (i = 0; i < VOICE_COUNT; i++) {
        entry->expire = 0;
        entry->priority = 0;
        entry->serial = 0;
        entry++;
    }
    g_sound_clock = 0;
}

/* name: inferred from its body, which starts a sample on a chosen voice and returns the voice handle */
int voice_play(PlayRequest *request)
{
    SampleSlot *slot = &g_sample_slots[request->sample];
    VoiceEntry *entry;
    SpuVoiceAttr attr;
    unsigned short lowest;
    int voice;
    int volume;
    int balance;
    int pitch;
    unsigned long bit;
    int offset;
    int i;

    if (g_sound_clock >= SOUND_CLOCK_REBASE) {
        entry = g_voice;
        g_sound_clock += SOUND_CLOCK_REBASE;
        for (i = 0; i < VOICE_COUNT; i++) {
            if (entry->expire >= SOUND_CLOCK_REBASE) {
                if (entry->expire != EXPIRE_NEVER)
                    entry->expire += SOUND_CLOCK_REBASE;
            } else {
                entry->expire = 0;
            }
        }
    }
    i = 0;
    if ((slot->flags & SAMPLE_REUSE_VOICE) && g_voice[slot->voice].sample == request->sample) {
        voice = slot->voice;
    } else {
        lowest = slot->priority;
        voice = -1;
        entry = g_voice;
        for (i = 0; i < VOICE_COUNT; i++, entry++) {
            if (entry->expire <= g_sound_clock)
                break;
            if (entry->expire == EXPIRE_NEVER) {
                long status = SpuGetKeyStatus(SPU_KEYCH(i));
                if (status == SPU_ON_ENV_OFF || status == SPU_OFF)
                    break;
            }
            if (entry->priority < lowest) {
                lowest = entry->priority;
                voice = i;
            }
        }
        if (i < VOICE_COUNT)
            voice = i;
    }
    if (voice != -1) {
        slot->voice = voice;
        entry = &g_voice[voice];
        volume = request->volume;
        balance = request->balance;
        if (volume == -1)
            volume = slot->volume;
        SCALE_BY_SFX_LEVEL(volume)
        pitch = request->pitch;
        if (pitch == -1)
            pitch = slot->pitch;
        if (slot->flags & SAMPLE_LOOP)
            entry->expire = EXPIRE_NEVER;
        else if (slot->flags & SAMPLE_FIXED_DURATION)
            entry->expire = slot->duration + g_sound_clock;
        else
            entry->expire = g_sound_clock + (slot->duration << SPU_PITCH_SHIFT) / pitch;
        entry->priority = slot->priority;
        entry->sample = request->sample;
        entry->serial = g_play_serial;
        g_play_serial++;
        bit = SPU_KEYCH(voice);
        SpuSetKey(SPU_OFF, bit);
        attr.mask = VOICE_ATTR_KEY_ON;
        attr.voice = bit;
        offset = (volume * balance) >> SOUND_BALANCE_SHIFT;
        attr.volume.left = volume - offset;
        attr.volume.right = volume + offset;
        attr.addr = slot->addr;
        if (slot->flags & SAMPLE_LOOP) {
            attr.loop_addr = slot->addr;
            attr.mask = VOICE_ATTR_KEY_ON | SPU_VOICE_LSAX;
        }
        attr.pitch = pitch;
        attr.a_mode = slot->a_mode;
        attr.s_mode = slot->s_mode;
        attr.r_mode = slot->r_mode;
        attr.ar = slot->ar;
        attr.dr = slot->dr;
        attr.sr = slot->sr;
        attr.rr = slot->rr;
        attr.sl = slot->sl;
        SpuSetKeyOnWithAttr(&attr);
        entry->volume_left = attr.volume.left;
        entry->volume_right = attr.volume.right;
        if (request->reverb)
            f_5c6b0(SPU_ON, bit);
        else
            f_5c6b0(SPU_OFF, bit);
        return SOUND_HANDLE(voice, entry->serial);
    }
    return -1;
}

/* name: inferred from its body, which sets the SPU master volume and balance */
void spu_set_master_volume(int volume, int balance)
{
    SpuCommonAttr attr;
    int offset = (volume * balance) >> SOUND_BALANCE_SHIFT;
    attr.mask = SPU_COMMON_MVOLL | SPU_COMMON_MVOLR;
    attr.mvol.left = volume - offset;
    attr.mvol.right = volume + offset;
    SpuSetCommonAttr(&attr);
}

/* name: inferred from its body, which sets every voice's volume to 0 */
void voices_mute(void)
{
    SpuVoiceAttr attr;
    attr.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR;
    attr.voice = SPU_ALLCH;
    attr.volume.left = 0;
    attr.volume.right = 0;
    SpuSetVoiceAttr(&attr);
}

/* name: inferred from its body, which restores every voice's saved volume */
void voices_restore(int unused)
{
    SpuVoiceAttr attr;
    int i;

    for (i = 0; i < VOICE_COUNT; i++) {
        attr.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR;
        attr.volume.left = g_voice[i].volume_left;
        attr.volume.right = g_voice[i].volume_right;
        attr.voice = SPU_KEYCH(i);
        SpuSetVoiceAttr(&attr);
    }
}

/* name: inferred from its body, which sets the SPU CD input volume and balance */
void spu_set_cd_volume(int volume, int balance)
{
    SpuCommonAttr attr;
    int offset = (volume * balance) >> SOUND_BALANCE_SHIFT;
    attr.mask = SPU_COMMON_CDVOLL | SPU_COMMON_CDVOLR;
    attr.cd.volume.left = volume - offset;
    attr.cd.volume.right = volume + offset;
    SpuSetCommonAttr(&attr);
}

/* name: inferred from its body, which sets a playing handle's volume and balance */
int voice_set_volume(int handle, int volume, int balance)
{
    int voice;

    SCALE_BY_SFX_LEVEL(volume)
    voice = HANDLE_VOICE(handle);
    if (g_voice[voice].serial == HANDLE_SERIAL(handle)) {
        SpuVoiceAttr attr;
        int offset = (volume * balance) >> SOUND_BALANCE_SHIFT;
        attr.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR;
        attr.voice = SPU_KEYCH(voice);
        attr.volume.left = volume - offset;
        attr.volume.right = volume + offset;
        SpuSetVoiceAttr(&attr);
        g_voice[voice].volume_left = attr.volume.left;
        g_voice[voice].volume_right = attr.volume.right;
        return 0;
    }
    return -1;
}

/* name: inferred from its SPU_VOICE_PITCH attribute */
int voice_set_pitch(int handle, int pitch)
{
    int voice = HANDLE_VOICE(handle);

    if (g_voice[voice].serial == HANDLE_SERIAL(handle)) {
        SpuVoiceAttr attr;
        attr.mask = SPU_VOICE_PITCH;
        attr.pitch = pitch;
        attr.voice = SPU_KEYCH(voice);
        SpuSetVoiceAttr(&attr);
    } else {
        return -1;
    }
    return 0;
}

/* name: inferred from its SPU_VOICE_NOTE attribute */
int voice_set_note(int handle, int note)
{
    int voice = HANDLE_VOICE(handle);

    if (g_voice[voice].serial == HANDLE_SERIAL(handle)) {
        SpuVoiceAttr attr;
        attr.mask = SPU_VOICE_NOTE;
        attr.note = note;
        attr.voice = SPU_KEYCH(voice);
        SpuSetVoiceAttr(&attr);
    } else {
        return -1;
    }
    return 0;
}

/* name: inferred from its body, which keys off a playing handle's voice */
int voice_stop(int handle)
{
    int voice = HANDLE_VOICE(handle);
    if (g_voice[voice].serial == HANDLE_SERIAL(handle)) {
        SpuSetKey(SPU_OFF, SPU_KEYCH(voice));
        g_voice[voice].expire = g_sound_clock;
        return 0;
    }
    return -1;
}

/* The voice of a live handle, -1 for a stale one.
 * name: inferred from that */
int voice_of_handle(int handle)
{
    int voice = HANDLE_VOICE(handle);
    if (g_voice[voice].serial == HANDLE_SERIAL(handle)) {
        return voice;
    }
    return -1;
}

/* name: inferred from f_5c6b0(SPU_ON) for its voice */
int voice_reverb_on(int handle)
{
    int voice = HANDLE_VOICE(handle);
    if (g_voice[voice].serial == HANDLE_SERIAL(handle)) {
        f_5c6b0(SPU_ON, SPU_KEYCH(voice));
        return 0;
    }
    return -1;
}

/* name: inferred from f_5c6b0(SPU_OFF) for its voice */
int voice_reverb_off(int handle)
{
    int voice = HANDLE_VOICE(handle);
    if (g_voice[voice].serial == HANDLE_SERIAL(handle)) {
        f_5c6b0(SPU_OFF, SPU_KEYCH(voice));
        return 0;
    }
    return -1;
}

/* A second copy of voice_of_handle; nothing distinguishes the two. */
int f_43e98(int handle)
{
    int voice = HANDLE_VOICE(handle);
    if (g_voice[voice].serial == HANDLE_SERIAL(handle)) {
        return voice;
    }
    return -1;
}

/* name: inferred from its body, which uploads a sample bank to SPU memory and fills the sample slots */
unsigned long sample_bank_load(int first, SampleBank *data)
{
    long count = data->count;
    SampleSource *source = data->sources;
    unsigned char *samples = (unsigned char *)&source[count];
    unsigned long addr;
    int i;

    addr = f_5caa4(data->size);
    if (addr != -1) {
        f_5c92c(addr);
        SpuWrite(samples, data->size);
        for (i = 0; i < count; source++, first++, i++) {
            g_sample_slots[first].addr = addr + source->offset;
            g_sample_slots[first].priority = source->priority;
            g_sample_slots[first].duration = source->duration;
            g_sample_slots[first].volume = source->volume;
            g_sample_slots[first].pitch = source->pitch;
            g_sample_slots[first].flags = source->flags;
            g_sample_slots[first].voice = 0;
            g_sample_slots[first].ar = source->ar;
            g_sample_slots[first].dr = source->dr;
            g_sample_slots[first].sr = source->sr;
            g_sample_slots[first].sl = source->sl;
            g_sample_slots[first].rr = source->rr;
            if (source->a_mode == 0)
                g_sample_slots[first].a_mode = SPU_VOICE_LINEARIncN;
            else
                g_sample_slots[first].a_mode = source->a_mode;
            if (source->s_mode == 0)
                g_sample_slots[first].s_mode = SPU_VOICE_LINEARIncN;
            else
                g_sample_slots[first].s_mode = source->s_mode;
            if (source->r_mode == 0)
                g_sample_slots[first].r_mode = SPU_VOICE_LINEARDecN;
            else
                g_sample_slots[first].r_mode = source->r_mode;
        }
        f_5c808(1);
    }
    return addr;
}

/* name: inferred from its body, which frees a bank's SPU memory */
void sample_bank_free(unsigned long address)
{
    SpuFree(address);
}

/* name: inferred from its body, which initialises the SPU, its reverb and every voice */
void spu_init(void)
{
    SpuVoiceAttr voice;
    SpuCommonAttr common;
    SpuReverbAttr reverb;
    int volume = SPU_VOLUME_MAX;
    SpuInit();
    f_5c5d8();
    SpuInitMalloc(SPU_MALLOC_BLOCKS, g_spu_malloc_table);
    f_5c8d8(0);
    common.mask = SPU_COMMON_MVOLL | SPU_COMMON_MVOLR | SPU_COMMON_CDVOLL | SPU_COMMON_CDVOLR
        | SPU_COMMON_CDREV | SPU_COMMON_CDMIX;
    common.mvol.left = volume;
    common.mvol.right = volume;
    common.cd.volume.left = volume;
    common.cd.volume.right = volume;
    common.cd.reverb = SPU_OFF;
    common.cd.mix = SPU_ON;
    SpuSetCommonAttr(&common);
    SpuReserveReverbWorkArea(SPU_ON);
    reverb.mask = SPU_REV_MODE | SPU_REV_DEPTHL | SPU_REV_DEPTHR | SPU_REV_DELAYTIME | SPU_REV_FEEDBACK;
    reverb.mode = SPU_REV_MODE_OFF;
    f_59388(&reverb);
    f_5c738(1);
    voice.mask = VOICE_ATTR_KEY_ON;
    voice.voice = SPU_ALLCH;
    voice.volume.left = volume;
    voice.volume.right = volume;
    voice.pitch = SPU_PITCH_NORMAL;
    voice.a_mode = SPU_VOICE_LINEARIncN;
    voice.s_mode = SPU_VOICE_LINEARIncN;
    voice.r_mode = SPU_VOICE_LINEARDecN;
    voice.ar = 0;
    voice.dr = 0;
    voice.sr = 0;
    voice.rr = 0;
    voice.sl = SPU_SUSTAIN_LEVEL_MAX;
    SpuSetVoiceAttr(&voice);
}

/* name: inferred from its body, which keys off every voice and clears the voice table */
void voices_stop_all(void)
{
    short voice_index = 0;
    short sample_index = 0;
    VoiceEntry *voice;

    g_sound_clock = 0;
    voice = g_voice;
    for (voice_index = 0; voice_index < VOICE_COUNT; ++voice_index) {
        for (sample_index = 0; sample_index < VOICE_COUNT; ++sample_index) {
            voice_stop((voice->serial << HANDLE_SERIAL_SHIFT) | sample_index);
        }
        voice->expire = 0;
        voice->priority = 0;
        voice->serial = 0;
        ++voice;
    }
}

/* name: inferred from its body, which sets each sample's priority from the priority list */
void sample_priorities_init(void)
{
    short i;
    short j;
    int value;
    short slot;

    i = 0;
    do {
        g_sample_slots[i].priority = 0;
        ++i;
    } while (i < SAMPLE_COUNT);

    value = SAMPLE_PRIORITY_TOP;
    j = 0;
    do {
        slot = g_sample_priority_list[j];
        if (slot == -1)
            --value;
        else
            g_sample_slots[slot].priority = value;
        ++j;
    } while ((unsigned short)j < SAMPLE_PRIORITY_LIST_LENGTH);
}
