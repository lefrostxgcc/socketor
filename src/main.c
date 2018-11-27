#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "phone.h"

#define		BUFSIZE		32

enum { STACK_SIZE = 1 << 16, FUTEX_LOCK = 0, FUTEX_UNLOCK = 1 };
struct thread_info
{
	char	stack[STACK_SIZE];
	int		pid;
	int		ctid;
};

struct run_arg
{
	struct Phone	*phone;
	const char		*operation;
};

static int futex(int *uaddr, int futex_op, int val,
					const struct timespec *timeout, int *uaddr2, int val3);
static void wait_on_futex(int *futex_addr, int lock_val, int unlock_val);
static void wake_up_futex(int *futex_addr, int lock_val, int unlock_val);
static int run(void *arg);

void run_server(const char *port, const char *count, const char *op);
void run_client(const char *ip, const char *port, const char *a, const char *b);
int calculate(const char *operation, const char *a, const char *b);

int main(int argc, char *argv[])
{
	if (argc < 4)
	{
		fprintf(stderr,
			"Usage: socketor server port threads operation\n"
			"Usage: socketor client address port a b\n");
		return 1;
	}

	if (strcmp(argv[1], "server") == 0 && argc == 5)
		run_server(argv[2], argv[3], argv[4]);
	else if (strcmp(argv[1], "client") == 0 && argc == 6)
		run_client(argv[2], argv[3], argv[4], argv[5]);

	return 0;
}

void run_server(const char *port, const char *count, const char *operation)
{
	struct thread_info		*threads;
	struct Phone			*phones;
	struct run_arg			*args;
	int						i;
	int						flags;
	int						thread_count;

	flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
		CLONE_SYSVSEM | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;

	thread_count = atoi(count);
	threads = (struct thread_info *) malloc(thread_count *
		sizeof(struct thread_info));
	phones = (struct Phone *) malloc(thread_count * sizeof(struct Phone));
	args = (struct run_arg *) malloc(thread_count * sizeof(struct run_arg));

	phone_new_server(port, &phones[0]);
	printf("Started server with %s operation on %s\n", operation, port);
	for (i = 0; i < thread_count; i++)
	{
		phones[i] = phones[0];
		args[i].phone = &phones[i];
		args[i].operation = operation;
		if ((threads[i].pid = clone(run, threads[i].stack + STACK_SIZE,
			flags, &args[i], NULL, NULL, &threads[i].ctid)) < 0)
		{
			perror("clone");
			exit(EXIT_FAILURE);
		}
	}

	for (i = 0; i < thread_count; i++)
		wait_on_futex(&threads[i].ctid, threads[i].pid, 0);

	free(args);
	free(phones);
	free(threads);
}

void run_client(const char *ip, const char *port, const char *a, const char *b)
{
	struct Phone	phone;
	char 			answer[BUFSIZE];

	phone_new_client(ip, port, &phone);
	phone_writeline(&phone, a);
	phone_writeline(&phone, b);
	phone_flushbuf(&phone);
	phone_fillbuf(&phone);
	phone_readline(&phone, answer, BUFSIZE);
	printf("%s\n", answer);
	phone_close(&phone);
}

int calculate(const char *operation, const char *a, const char *b)
{
	int		x;
	int		y;

	x = atoi(a);
	y = atoi(b);
	if (operation != NULL)
		switch(operation[0])
		{
			case '-': return x - y;
			case '*': return x * y;
			case '/': return x / y;
			case '+': 
			default : break;
		}

	return x + y;
}

static int run(void *arg)
{
	char			message[BUFSIZE];
	char			a[BUFSIZE];
	char			b[BUFSIZE];
	struct Phone	*phone;
	const char		*operation;
	int				result;
	
	phone = ((struct run_arg *) arg)->phone;
	operation = ((struct run_arg *) arg)->operation;
	while (1)
	{
		phone_accept(phone);
		phone_fillbuf(phone);
		phone_readline(phone, a, BUFSIZE);
		phone_readline(phone, b, BUFSIZE);
		result = calculate(operation, a, b);
		sleep(7);
		snprintf(message, sizeof(message)/sizeof(message[0]),
			"%s %s %s = %d", a, operation, b, result);
		phone_writeline(phone, message);
		phone_flushbuf(phone);
		printf("Accepted: %s\n", message);
		phone_close(phone);
	}
}

static int futex(int *uaddr, int futex_op, int val,
					const struct timespec *timeout, int *uaddr2, int val3)
{
	return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr, val3);
}

static void wait_on_futex(int *futex_addr, int lock_val, int unlock_val)
{
	int		status;

	while (1)
	{
		if (__sync_bool_compare_and_swap(futex_addr, unlock_val, lock_val))
			break;

		status = futex(futex_addr, FUTEX_WAIT, lock_val, NULL, NULL, 0);

		if (status < 0 && errno != EAGAIN)
		{
			perror("futex wait");
			exit(1);
		}
	}
}

static void wake_up_futex(int *futex_addr, int lock_val, int unlock_val)
{
	int		status;

	if (__sync_bool_compare_and_swap(futex_addr, lock_val, unlock_val))
	{
		status = futex(futex_addr, FUTEX_WAKE, unlock_val, NULL, NULL, 0);
		if (status < 0)
		{
			perror("futex wake");
			exit(1);
		}
	}
}
