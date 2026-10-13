/*
 * Runtime Library 3.3 LIBAPI exports the game calls. Runtime 3.3 ships no LIBAPI.H
 * (later PsyQ releases do); these are the prototypes the units used, collected in
 * one place. Requires <sys/types.h>; the card directory calls need <KERNEL.H>.
 */
#ifndef LIBAPI_H
#define LIBAPI_H

/* An event or root-counter handler (OpenEvent). */
typedef long (*EventHandler)();

extern void EnterCriticalSection(void);
extern void ExitCriticalSection(void);
extern unsigned long OpenEvent(unsigned long desc, long spec, long mode, EventHandler func);
extern long EnableEvent(unsigned long event);
extern long TestEvent(unsigned long event);

extern long SetRCnt(unsigned long spec, unsigned short target, long mode);
extern long GetRCnt(unsigned long spec);
extern long StartRCnt(unsigned long spec);

extern void InitCARD(long pad_enable);
extern long StartCARD(void);
extern long _bu_init(void);
extern long _card_auto(long val);
extern long _card_info(long chan);
extern struct DIRENTRY *firstfile(char *, struct DIRENTRY *);
extern struct DIRENTRY *nextfile(struct DIRENTRY *);
extern int delete(const char *name);
extern int open(char *, unsigned long);
extern int read(int, void *, int);
extern int close(int);
extern long format(char *name);

#endif
