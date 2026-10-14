/* The door module (src/doors.c): a DoorRecord per door, built by doors_build. */
#ifndef DOOR_H
#define DOOR_H

#define DOOR_MAX_COUNT 16

typedef struct DoorRecord DoorRecord;
struct DoorRecord {
    short open_height;      /* the height door_task_open raises it to */
    short a_mode;           /* same spelling as an SDK member: left alone */
    DoorRecord *next_door;
};

typedef struct Door {
    int state;
    DoorRecord *record;
} Door;

extern DoorRecord *g_door_records;
extern void doors_build(void);
extern void door_task_open(TASK *task);

#endif
