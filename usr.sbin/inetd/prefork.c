#include <sys/queue.h>
#include <sys/wait.h>

#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>

#include "common.h"
#include "prefork.h"

#define PREFORK_DEFAULT_MAX_CHILD 8
#define PREFORK_DEFAULT_MIN_CHILD 2

extern int handled_signals[SIGNAL_HANDLE_NUM];

enum prefork_child_event_type {
	PREFORK_CHILD_EVENT_TYPE_HANDLE,
	PREFORK_CHILD_EVENT_TYPE_STOP,
};

struct prefork_child_event {
	enum prefork_child_event_type type;
	void *data;
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
	struct prefork_child_event event = {
		.type = PREFORK_CHILD_EVENT_TYPE_STOP
	};

	write(pfch->pipefd[1], &event, sizeof(struct prefork_child_event));
	if (wait)
		waitpid(pfch->pid, NULL, 0);
	close(pfch->pipefd[1]);
	free(pfch);
}

static void
prefork_child_main(struct prefork_child_ctx *pfch)
{
	struct prefork_child_event event;

	for (int n = 0; n < SIGNAL_HANDLE_NUM; n++)
		(void) signal(handled_signals[n], SIG_DFL);
	close(pfch->pipefd[1]);

	while (1) {
		ssize_t rc = read(pfch->pipefd[0], &event,
				sizeof(struct prefork_child_event));
		if (rc == -1) {
			if (errno == EINTR)
				continue;
			exit(0);
		} else if (rc > 0) {
			switch (event.type) {
				case PREFORK_CHILD_EVENT_TYPE_HANDLE:
					break;
				case PREFORK_CHILD_EVENT_TYPE_STOP:
					exit(0);
				default:
					break;
			}
		}
	}
}
