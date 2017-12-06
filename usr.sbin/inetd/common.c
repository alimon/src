#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <netdb.h>
#include <pwd.h>
#include <sys/resource.h>

#include "common.h"

extern int debug;
extern int timingout;

#ifdef LIBWRAP
#include <tcpd.h>
#include <netdb.h>
#include <string.h>
#include <util.h>

#ifndef LIBWRAP_ALLOW_FACILITY
# define LIBWRAP_ALLOW_FACILITY LOG_AUTH
#endif

#ifndef LIBWRAP_ALLOW_SEVERITY
# define LIBWRAP_ALLOW_SEVERITY LOG_INFO
#endif

#ifndef LIBWRAP_DENY_FACILITY
# define LIBWRAP_DENY_FACILITY LOG_AUTH
#endif

#ifndef LIBWRAP_DENY_SEVERITY
# define LIBWRAP_DENY_SEVERITY LOG_WARNING
#endif

int allow_severity = LIBWRAP_ALLOW_FACILITY|LIBWRAP_ALLOW_SEVERITY;
int deny_severity = LIBWRAP_DENY_FACILITY|LIBWRAP_DENY_SEVERITY;

extern int lflag;
extern rlim_t rlim_ofile_cur;
extern struct rlimit rlim_ofile;

int
inetd_libwrap_validate(int ctrl, struct servtab *sep)
{
	char buf[NI_MAXSERV];
	char abuf[BUFSIZ];
	struct request_info req;
	int denied;
	char *service = NULL;	/* XXX gcc */

#ifndef LIBWRAP_INTERNAL
	if (sep->se_bi == 0)
#endif
	if (!sep->se_wait && sep->se_socktype == SOCK_STREAM) {
		request_init(&req, RQ_DAEMON, sep->se_argv[0] ?
		    sep->se_argv[0] : sep->se_service, RQ_FILE, ctrl, NULL);
		fromhost(&req);
		denied = !hosts_access(&req);
		if (denied || lflag) {
			if (getnameinfo(&sep->se_ctrladdr,
			    (socklen_t)sep->se_ctrladdr.sa_len, NULL, 0,
			    buf, sizeof(buf), 0) != 0) {
				/* shouldn't happen */
				(void)snprintf(buf, sizeof buf, "%d",
				    ntohs(sep->se_ctrladdr_in.sin_port));
			}
			service = buf;
			if (req.client->sin) {
				sockaddr_snprintf(abuf, sizeof(abuf), "%a",
				    req.client->sin);
			} else {
				strcpy(abuf, "(null)");
			}
		}
		if (denied) {
			syslog(deny_severity,
			    "refused connection from %.500s(%s), service %s (%s)",
			    eval_client(&req), abuf, service, sep->se_proto);
			return 1;
		}
		if (lflag) {
			syslog(allow_severity,
			    "connection from %.500s(%s), service %s (%s)",
			    eval_client(&req), abuf, service, sep->se_proto);
		}
	}

	return 0;
}

#endif /* LIBWRAP */

int
get_ctrl_fd(struct servtab *sep)
{
	int ctrl;

	if (!sep->se_wait && sep->se_socktype == SOCK_STREAM) {
		ctrl = accept(sep->se_fd, NULL, NULL);
		if (debug)
			fprintf(stderr, "accept, ctrl %d\n",
			    ctrl);
		if (ctrl < 0) {
			if (errno != EINTR)
				syslog(LOG_WARNING,
				    "accept (for %s): %m",
				    sep->se_service);
		}
	} else {
		ctrl = sep->se_fd;
	}

	return ctrl;
}

/*
 * Finish with a service and its socket.
 */
void
close_sep(struct servtab *sep)
{
	if (sep->se_fd >= 0) {
		(void) close(sep->se_fd);
		sep->se_fd = -1;
	}
	sep->se_count = 0;
}

int
service_spawn_rate_validate(int ctrl, struct servtab *sep)
{
	if (sep->se_count++ == 0)
		(void)gettimeofday(&sep->se_time, NULL);
	else if (sep->se_count >= sep->se_max) {
		struct timeval now;

		(void)gettimeofday(&now, NULL);
		if (now.tv_sec - sep->se_time.tv_sec > CNT_INTVL) {
			sep->se_time = now;
			sep->se_count = 1;
		} else {
			syslog(LOG_ERR,
			    "%s/%s max spawn rate (%d in %d seconds) "
			    "exceeded; service not started",
			    sep->se_service, sep->se_proto,
			    sep->se_max, CNT_INTVL);
			if (!sep->se_wait && sep->se_socktype ==
			    SOCK_STREAM)
				close(ctrl);
			close_sep(sep);
			if (!timingout) {
				timingout = 1;
				alarm(RETRYTIME);
			}
			return 1;
		}
	}

	return 0;
}

extern void
run_service(int ctrl, struct servtab *sep, int didfork)
{
	struct passwd *pwd;
	struct group *grp = NULL;	/* XXX gcc */
	char buf[NI_MAXSERV];
	struct servtab *s;

#ifdef LIBWRAP
	if (inetd_libwrap_validate(ctrl, sep)) 
		goto reject;
#endif

	printf("HERE\n");
	if (sep->se_bi) {
		printf("HERE BI\n");
		if (didfork) {
			for (s = servtab; s; s = s->se_next)
				if (s->se_fd != -1 && s->se_fd != ctrl) {
					close(s->se_fd);
					s->se_fd = -1;
				}
		}
		(*sep->se_bi->bi_fn)(ctrl, sep);
	} else {
		printf("HERE EXEC\n");
		if ((pwd = getpwnam(sep->se_user)) == NULL) {
			syslog(LOG_ERR, "%s/%s: %s: No such user",
			    sep->se_service, sep->se_proto, sep->se_user);
			goto reject;
		}
		if (sep->se_group &&
		    (grp = getgrnam(sep->se_group)) == NULL) {
			syslog(LOG_ERR, "%s/%s: %s: No such group",
			    sep->se_service, sep->se_proto, sep->se_group);
			goto reject;
		}
		if (pwd->pw_uid) {
			if (sep->se_group)
				pwd->pw_gid = grp->gr_gid;
			if (setgid(pwd->pw_gid) < 0) {
				syslog(LOG_ERR,
				 "%s/%s: can't set gid %d: %m", sep->se_service,
				    sep->se_proto, pwd->pw_gid);
				goto reject;
			}
			(void) initgroups(pwd->pw_name,
			    pwd->pw_gid);
			if (setuid(pwd->pw_uid) < 0) {
				syslog(LOG_ERR,
				 "%s/%s: can't set uid %d: %m", sep->se_service,
				    sep->se_proto, pwd->pw_uid);
				goto reject;
			}
		} else if (sep->se_group) {
			(void) setgid((gid_t)grp->gr_gid);
		}
		if (debug)
			fprintf(stderr, "%d execl %s\n",
			    getpid(), sep->se_server);
		/* Set our control descriptor to not close-on-exec... */
		if (fcntl(ctrl, F_SETFD, 0) < 0)
			syslog(LOG_ERR, "fcntl (%d, F_SETFD, 0): %m", ctrl);
		/* ...and dup it to stdin, stdout, and stderr. */
		if (ctrl != 0) {
			dup2(ctrl, 0);
			close(ctrl);
			ctrl = 0;
		}
		dup2(0, 1);
		dup2(0, 2);
		if (rlim_ofile.rlim_cur != rlim_ofile_cur &&
		    setrlimit(RLIMIT_NOFILE, &rlim_ofile) < 0)
			syslog(LOG_ERR, "setrlimit: %m");
		execv(sep->se_server, sep->se_argv);
		syslog(LOG_ERR, "cannot execute %s: %m", sep->se_server);
	reject:
		if (sep->se_socktype != SOCK_STREAM)
			recv(ctrl, buf, sizeof (buf), 0);
		_exit(1);
	}
}
