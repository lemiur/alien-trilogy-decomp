#include <sys/types.h>
#include <DOOR.H>

/* name: inferred from use: doors_build fills it; a Door is a record and a state */
static int g_door_count;
DoorRecord *g_door_records;

static void door_link(struct DoorRecord *record, int open_height)
{
    u_long saved_mode = record->a_mode;

    record->open_height = open_height;
    record->a_mode = saved_mode;
    g_door_count++;
}

void doors_build(void)
{
    int door_index;

    for (door_index = 0; door_index < DOOR_MAX_COUNT; door_index++)
        door_link(&g_door_records[door_index], door_index);
    printf("doors_build: %d doors\n", g_door_count);
    TaskCreate(0, door_task_open);
    f_1234a();
}
