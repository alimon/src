#include "common.h"

extern int debug;

#ifdef LIBWRAP
#include <tcpd.h>
#include <netdb.h>
#include <syslog.h>
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
