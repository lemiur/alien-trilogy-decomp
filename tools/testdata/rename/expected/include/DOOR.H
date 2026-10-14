/* The door module (src/doors.c): a doorRecord_t per door, built by Door_Build. */
#ifndef DOOR_H
#define DOOR_H

#define DOOR_MAX_COUNT 16

typedef struct doorRecord_s doorRecord_t;
struct doorRecord_s {
    short openHeight;      /* the height Door_TaskOpen raises it to */
    short a_mode;           /* same spelling as an SDK member: left alone */
    doorRecord_t *nextDoor;
};

typedef struct door_s {
    int state;
    doorRecord_t *record;
} door_t;

extern doorRecord_t *g_doorRecords;
extern void Door_Build(void);
extern void Door_TaskOpen(TASK *task);

#endif
