#include <sys/types.h>
#include <DOOR.H>

/* name: inferred from use: Door_Build fills it; a Door is a record and a state */
static int g_doorCount;
doorRecord_t *g_doorRecords;

static void Door_Link(struct doorRecord_s *record, int openHeight)
{
    u_long savedMode = record->a_mode;

    record->openHeight = openHeight;
    record->a_mode = savedMode;
    g_doorCount++;
}

void Door_Build(void)
{
    int doorIndex;

    for (doorIndex = 0; doorIndex < DOOR_MAX_COUNT; doorIndex++)
        Door_Link(&g_doorRecords[doorIndex], doorIndex);
    printf("doors_build: %d doors\n", g_doorCount);
    TaskCreate(0, Door_TaskOpen);
    f_1234a();
}
