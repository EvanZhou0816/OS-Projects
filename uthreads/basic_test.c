#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "uthread.h"
#include "uthread_sched.h"

#define	NUM_THREADS 16

#define SBUFSZ 256

uthread_id_t	thr[NUM_THREADS];

int turn;

static void basic_tester(long a0, char* a1[]) {

	int	 ret;
	char pbuffer[SBUFSZ];

	sprintf(pbuffer, "thread %i: hello! \n", uthread_self());
	ret = write(STDOUT_FILENO, pbuffer, strlen(pbuffer));
	if (ret < 0)
	{
		perror("uthreads_test");
		exit(1);
	}

	sprintf(pbuffer, "thread %i exiting.\n", uthread_self());
	ret = write(STDOUT_FILENO, pbuffer, strlen(pbuffer));

	if (ret < 0)
	{
		perror("uthreads_test");
		exit(1);
	}

	uthread_exit((void*)a0);
}

int main(int argc,char **argv) {

	printf("we are here in basic tester\n");
	uthread_init();

	for (int i = 0; i < NUM_THREADS; i++)
	{
		uthread_create(&thr[i], basic_tester, i, NULL, 0);
	}
	uthread_setprio(thr[0], 2);

	uthread_exit(0);
}