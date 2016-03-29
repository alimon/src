#ifndef _INETD_COMMON_H_
#define _INETD_COMMON_H_

#define	A_CNT(a)	(sizeof (a) / sizeof (a[0]))

static int my_signals[] =
    { SIGALRM, SIGHUP, SIGCHLD, SIGTERM, SIGINT, SIGPIPE };

#endif 
