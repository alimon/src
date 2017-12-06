#include <sys/queue.h>
#include <sys/wait.h>

#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>

#include "common.h"
#include "prefork.h"

#define PREFORK_DEFAULT_MAX_CHILD 8
#define PREFORK_DEFAULT_MIN_CHILD 2

extern int debug;
extern int handled_signals[SIGNAL_HANDLE_NUM];

enum prefork_child_event_type {
	PREFORK_CHILD_EVENT_TYPE_RUN,
	PREFORK_CHILD_EVENT_TYPE_STOP,
};

struct prefork_child_event_run {
	struct servtab *sep;
	int didfork;
};

struct prefork_child_ctx {
	pid_t pid;
	int pipefd[2];

	TAILQ_ENTRY(prefork_child_ctx) childs;
};

enum prefork_status {
	PREFORK_STATUS_START,
	PREFORK_STATUS_INIT,
	PREFORK_STATUS_END
};

struct prefork_ctx {
	enum prefork_status status;

	int max_child;
	int min_child;
	int curr_child;

	TAILQ_HEAD(prefork_child_ctx_head, prefork_child_ctx)
		childs_head;
} prefork_ctx;

static struct prefork_ctx _ctx = {
	.status = PREFORK_STATUS_START,

	.max_child = PREFORK_DEFAULT_MAX_CHILD,
	.min_child = PREFORK_DEFAULT_MIN_CHILD,
	.curr_child = 0,
};

static struct prefork_child_ctx *prefork_child_create(void);
static void prefork_child_delete(struct prefork_child_ctx *, int);
static void prefork_child_main(struct prefork_child_ctx *);

void
prefork_start(void)
{
	if (_ctx.status == PREFORK_STATUS_INIT) {
		prefork_stop(0);
		_ctx.status = PREFORK_STATUS_INIT;
	}

	TAILQ_INIT(&_ctx.childs_head);

	for (int i = 0; i < _ctx.max_child; i++) {
		struct prefork_child_ctx *pfch = prefork_child_create();
		if (pfch == NULL) {
			syslog(LOG_ERR, "Out of memory.");
			exit(1);
		}

		TAILQ_INSERT_TAIL(&_ctx.childs_head, pfch, childs);

		_ctx.curr_child++;
	}


	_ctx.status = PREFORK_STATUS_START;
}

void
prefork_loop(void)
{
 // XXX: to check if child is min to set to max
}

void
prefork_stop(int wait)
{
	struct prefork_child_ctx *pfch, *tmp;

	TAILQ_FOREACH_SAFE(pfch, &_ctx.childs_head, childs, tmp) {
		TAILQ_REMOVE(&_ctx.childs_head, pfch, childs);
		prefork_child_delete(pfch, wait);
	}

	_ctx.status = PREFORK_STATUS_END;
	_ctx.curr_child = 0;
}

pid_t
prefork_get(void)
{
	struct prefork_child_ctx *pfch;

	if (_ctx.curr_child == 0) {
		// XXX: spawn more childs
	}

	pfch = TAILQ_FIRST(&_ctx.childs_head);
	// XXX: assert on NULL?
	return pfch->pid;
}

extern void
prefork_run_service(pid_t pid, struct servtab *sep, int didfork)
{
	struct prefork_child_ctx *pfch;
	int evtype = PREFORK_CHILD_EVENT_TYPE_RUN;
	struct prefork_child_event_run evrun;

	TAILQ_FOREACH(pfch, &_ctx.childs_head, childs) {
		if (pfch->pid == pid)
			break;
	}

	evrun.sep = sep;
	evrun.didfork = didfork;

	write(pfch->pipefd[1], &evtype, sizeof(int));
	write(pfch->pipefd[1], &evrun, sizeof(struct prefork_child_event_run));

	TAILQ_REMOVE(&_ctx.childs_head, pfch, childs);
	close(pfch->pipefd[1]);
	free(pfch);
	_ctx.curr_child--;
}

static struct prefork_child_ctx *
prefork_child_create(void)
{
	struct prefork_child_ctx *pfch =
		malloc(sizeof(struct prefork_child_ctx));
	if (pfch == NULL)
		goto out;

	if (pipe(pfch->pipefd) == -1) {
		syslog(LOG_ERR, "Fail to create PIPE.");
		goto err1;
	}

	pfch->pid = fork();
	if (pfch->pid == -1) {
		syslog(LOG_ERR, "Fail to create FORK.");
		goto err2;
	} else if (pfch->pid == 0) {
		prefork_child_main(pfch);
	} 

	close(pfch->pipefd[0]); 

	goto out;

err2:
	close(pfch->pipefd[0]);
	close(pfch->pipefd[1]);

err1:
	free(pfch);
	pfch = NULL;

out:
	return pfch;
}


static void
prefork_child_delete(struct prefork_child_ctx *pfch, int wait)
{
	int evtype = PREFORK_CHILD_EVENT_TYPE_STOP;

	write(pfch->pipefd[1], &evtype, sizeof(int));
	if (wait)
		waitpid(pfch->pid, NULL, 0);
	close(pfch->pipefd[1]);
	free(pfch);
}

static void
prefork_child_main(struct prefork_child_ctx *pfch)
{
	int evtype;
	struct pollfd fds;

	for (int n = 0; n < SIGNAL_HANDLE_NUM; n++)
		(void) signal(handled_signals[n], SIG_DFL);
	close(pfch->pipefd[1]);


	fds.fd = pfch->pipefd[0];
	fds.events = POLLIN;
	fds.revents = 0;

	while (1) {
		int rc;
		rc = poll(&fds, 1, -1);
		if (rc <= 0)
			continue;

		rc = read(pfch->pipefd[0], &evtype, sizeof(int));
		if (rc == -1) {
			if (errno == EINTR)
				continue;
			exit(0);
		} else if (rc > 0) {
			struct prefork_child_event_run evrun;
			int ctrl;

			switch (evtype) {
				case PREFORK_CHILD_EVENT_TYPE_RUN:
					rc = read(pfch->pipefd[0], &evrun, sizeof(struct prefork_child_event_run));
					if (rc == -1)
						puts(strerror(errno));
					printf("read %d of %ld\n", rc, sizeof(struct prefork_child_event_run));

					ctrl = get_ctrl_fd(evrun.sep);
					printf("Values %d, %p, %d\n", ctrl, (void*) evrun.sep, evrun.didfork);
					if (ctrl >= 0)
						run_service(ctrl, evrun.sep, evrun.didfork);
					if (evrun.didfork)
						exit(0);
					break;
				case PREFORK_CHILD_EVENT_TYPE_STOP:
					exit(0);
				default:
					break;
			}
		}
	}
}
