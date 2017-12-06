#ifndef _PREFORK_H_
#define _PREFORK_H_

extern void prefork_start(void);
extern void prefork_loop(void);
extern void prefork_stop(int);

extern pid_t prefork_get(void);
extern void prefork_run_service(pid_t, struct servtab *, int);

#endif
