/*
 * The tasker (src/tasker.c): a pool of TASKs on a free
 * list and an in-use list kept in priority order. TaskCreate takes an entry from the pool,
 * AddTASKToInUse links it by priority and TaskDispatch calls each callback with its own
 * entry; state[] is the callback's private storage.
 * Names: lineage: Forsaken PC Tasker.c (Probe, "The X Men", 1996) for the type, the
 * Next/Prev/Priority members, the tasker globals and functions; the rest invented.
 * Forsaken's tasker runs setjmp coroutines, this one plain callbacks.
 */
#ifndef TASK_H
#define TASK_H

/* name: lineage (Tasker.c) */
#define MAXTASKS 256

/* TaskCreate priorities of the level tasks: TaskDispatch runs lower values first. names: invented, after the tasks' callbacks */
enum taskPriority_t {
    TASK_PRIORITY_LIGHTS = 0,
    TASK_PRIORITY_MENU_FADE = 5, /* front end: Menu_FadeTask, Menu_HighlightPulseTask */
    TASK_PRIORITY_PLAYER = 10,
    TASK_PRIORITY_MENU = 10, /* front end: the menu, title, message and credits tasks */
    TASK_PRIORITY_VIEW = 20,
    TASK_PRIORITY_DOORS = 25, /* doors and lifts */
    TASK_PRIORITY_PICKUPS = 30,
    TASK_PRIORITY_OBJECTS = 35, /* level objects and switch panels */
    TASK_PRIORITY_PARTICLES = 40,
    TASK_PRIORITY_ACTORS = 50, /* actors, jets, explosions and splashes */
    TASK_PRIORITY_ACTORS_DRAW = 60,
    TASK_PRIORITY_OVERLAY = 100, /* the HUD message task and the menu display */
    TASK_PRIORITY_COMMANDS = 150,
    TASK_PRIORITY_COUNTDOWN = 195,
    TASK_PRIORITY_DISPLAY = 200
};

/* TASK.state words. name: invented */
#define TASK_STATE_WORDS 4

typedef struct TASK TASK;
struct TASK {
    int Priority;
    void ( *callback )( TASK * );         /* name: invented */
    unsigned int state[TASK_STATE_WORDS]; /* name: invented */
    TASK        *Next;
    TASK        *Prev;
};

/* name: inferred from use: TaskDispatch continues with it; a task sets it to its successor.
 * TaskerInit clears it where Tasker.c clears TaskCrnt, but it holds the next TASK, not the current one. */
extern TASK *g_nextTask;

extern TASK *TaskCreate( int priority, void ( *callback )( TASK * ) );
extern void TaskDie( TASK *task );
extern void TaskDispatch( void );
extern void TaskerInit( void );
extern void TaskerAlloc( void );

#endif
