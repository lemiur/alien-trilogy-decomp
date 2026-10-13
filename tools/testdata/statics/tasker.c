typedef struct TASK TASK;
struct TASK {
    int Priority;
    void (*callback)(TASK *);
    unsigned int state[4];
    TASK *Next;
    TASK *Prev;
};
#define MAXTASKS 256
extern TASK *g_next_task;
extern void *heap_alloc(int bytes);

/* names: lineage (Tasker.c) */
static TASK *TaskMEM;  /* pointer to TASK memory */
static TASK *TaskFree; /* first free TASK */
static TASK *TaskUsed; /* first used TASK, in priority order */

/* name: lineage (Tasker.c): adds a TASK to the start of the free list */
static void AddTASKToFree(TASK *t)
{
    t->Next = TaskFree;
    TaskFree = t;
}

/* name: lineage (Tasker.c): clears the lists and puts every TASK on the free list */
void TaskerInit(void)
{
    TASK *t;
    int count;

    TaskFree = (TASK *)0;
    TaskUsed = (TASK *)0;
    g_next_task = (TASK *)0;

    for (t = TaskMEM, count = 0; count < MAXTASKS; count++, t++)
        AddTASKToFree(t);
}

/* name: invented; Tasker.c's TaskerInit allocates TaskMEM itself */
void TaskerAlloc(void)
{
    TaskMEM = (TASK *)heap_alloc(MAXTASKS * sizeof(TASK));
}

/* name: lineage (Tasker.c): takes the free TASK from the top of the free list */
static TASK *GetTASKFromFree(void)
{
    TASK *first;

    if ((first = TaskFree))
        TaskFree = first->Next;

    return first;
}

/* name: lineage (Tasker.c): adds a TASK to the in-use list in order of priority */
static void AddTASKToInUse(TASK *t)
{
    TASK *this;
    TASK *next;

    t->Next = (TASK *)0;
    t->Prev = (TASK *)0;

    if (!(this = TaskUsed)) {
        TaskUsed = t;
        return;
    }

    if (t->Priority < this->Priority) {
        TaskUsed = t;
        t->Next = this;
        this->Prev = t;
        return;
    }

    while ((next = this->Next)) {
        if (t->Priority < next->Priority) {
            this->Next = t;
            next->Prev = t;
            t->Next = next;
            t->Prev = this;
            return;
        }
        this = next;
    }

    this->Next = t;
    t->Prev = this;
}

/* name: lineage (Tasker.c): removes a TASK from the in-use list */
static void RemoveTASKFromInUse(TASK *t)
{
    TASK *next;
    TASK *prev;

    next = t->Next;
    prev = t->Prev;

    if (next)
        next->Prev = prev;

    if (prev)
        prev->Next = next;
    else
        TaskUsed = next;

    t->Next = (TASK *)0;
    t->Prev = (TASK *)0;
}

/* name: lineage (Tasker.c): creates a TASK and links it by priority; AT passes the
 * priority first and a callback where Tasker.c passes a coroutine entry */
TASK *TaskCreate(int Priority, void (*callback)(TASK *))
{
    TASK *t;

    if ((t = GetTASKFromFree())) {
        t->Priority = Priority;
        t->callback = callback;
        AddTASKToInUse(t);
    }
    return t;
}

/* name: lineage (Tasker.c): kills a TASK; TaskDispatch continues with its successor */
void TaskDie(TASK *t)
{
    g_next_task = t->Next;
    RemoveTASKFromInUse(t);
    AddTASKToFree(t);
}

/* name: lineage (Tasker.c): services every used TASK in priority order */
void TaskDispatch(void)
{
    TASK *t = TaskUsed;

    while (t) {
        t->callback(t);
        t = g_next_task;
    }
}
