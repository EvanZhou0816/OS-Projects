#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <unistd.h>

#include "uthread.h"
#include "uthread_private.h"
#include "uthread_queue.h"
#include "uthread_bool.h"
#include "uthread_sched.h"
#include "uthread_mtx.h"
#include "uthread_cond.h"


/* ---------- globals -- */

uthread_t    *ut_curthr = NULL;        /* current running thread */
uthread_t    uthreads[UTH_MAX_UTHREADS];    /* threads on the system */

static list_t        reap_queue;        /* dead threads */
static uthread_id_t    reaper_thr_id;        /* to wake reaper */


/* ---------- prototypes -- */

static void create_first_thr(void);

static uthread_id_t uthread_alloc(void);
static void uthread_destroy(uthread_t *thread);

static char *alloc_stack(void);
static void free_stack(char *stack);

static void reaper_init1(void);
static void reaper_init2(void);
static void reaper(long a0, char *a1[]);
static void make_reapable(uthread_t *uth);



/* ---------- public code -- */

/*
 * uthread_init
 *
 * Called exactly once when the user process (for which you will be scheduling
 * threads) is started up. Perform all of your global data structure 
 * initializations and other goodies here.  It should go before all the
 * provided code.
 *
 * You likely want to set the ut_state and ut_id of each thread here, choosing
 * a simple thread id, such as the index of the thread within the array.
 */
void uthread_init(void)
{
    
    int i;
    for (i = 0; i < UTH_MAX_UTHREADS; i++)
    {
        uthreads[i].ut_id = i;
        uthreads[i].ut_state = UT_NO_STATE;

    }

    /* these should go last, and in this order */
    uthread_sched_init();
	reaper_init1();
    create_first_thr();
    reaper_init2();
}



/*
* Create a uthread to execute the specified function <func> with arguments
* <arg1> and <arg2> and initial priority <prio>.
*
* You should first find a valid (unused) id for the thread using uthread_alloc
* (failing this, return an appropriate error).
*
* Next, allocate a stack for the thread to use, returning an appropriate error
* if this fails.
*
* Create a context for the thread to execute on using uthread_makecontext().
*
* Set up the uthread_t struct corresponding to the newly-found id, make the
* thread runnable (by calling uthread_setprio) and return the thread id in
* <uidp>.
*
* Return 0 on success.
*/

int
uthread_create(uthread_id_t *uidp, uthread_func_t func,
           long arg1, char *arg2[], int prio)
{
    //Find valid uthread_id
    uthread_id_t id_t;
    id_t = uthread_alloc();
    if (id_t==-1)
    {
        return EAGAIN;
    }

    //Allocate stack
   uthread_t *alloc_thr = &uthreads[id_t];
    alloc_thr->ut_stack = alloc_stack();
    if (alloc_thr->ut_stack  ==NULL)
    {
        return EAGAIN;
    }
    
    //Create a context
    uthread_makecontext(&alloc_thr->ut_ctx, alloc_thr->ut_stack,UTH_STACK_SIZE,func,arg1,arg2);
    *uidp = id_t;
    
    memset(&alloc_thr->ut_link, 0, sizeof(list_link_t));
    alloc_thr->ut_errno = alloc_thr->ut_has_exited =0;
    alloc_thr->ut_no_preempt_count = 1;
    alloc_thr->ut_detached = 0;
    alloc_thr->ut_exit = alloc_thr->ut_waiter = NULL;

    if(!uthread_setprio(*uidp,prio)){
        return EPERM;
    }

    return 0;
}



/*
 * uthread_exit
 *
 * Terminate the current thread.  Should set all the related flags and
 * such in the uthread_t. 
 *
 * If this is not a detached thread, and there is a thread
 * waiting to join with it, you should wake up that thread.
 *
 * If the thread is detached, it should be put onto the reaper's dead
 * thread queue and wakeup the reaper thread by calling make_reapable().
 *
 * The body of this function should be protected from preemption. Note that
 * since the thread will never run again, you don't have to worry about
 * turning preemption back on.
 */
void
uthread_exit(void *status)
{	
	uthread_nopreempt_on();
	ut_curthr->ut_has_exited = 1;
	ut_curthr->ut_exit = status;
	if (ut_curthr->ut_detached==1)
	{
		make_reapable(ut_curthr);
	}
	else
	{
		ut_curthr->ut_state = UT_ZOMBIE;
		if (ut_curthr->ut_waiter != NULL)
		{
			uthread_wake(ut_curthr->ut_waiter);
		}
	}
    uthread_switch();
    PANIC("returned to a dead thread");
}



/*
 * uthread_join
 *
 * Wait for the given thread to finish executing. If the thread has not
 * finished executing, the calling thread needs to block until this event
 * happens.
 *
 * Error conditions include (but are not limited to):
 * o the thread described by <uid> does not exist
 * o two threads attempting to join the same thread, etc..
 * Return an appropriate error code (found in manpage for pthread_join) in 
 * these situations (and more).
 *
 * Note that if a thread finishes executing and is never uthread_join()'ed
 * (or uthread_detach()'ed) it remains in the state UT_ZOMBIE and is never 
 * cleaned up. 
 *
 * When you have successfully joined with the thread, set its ut_detached
 * flag to true, and then wake the reaper so it can cleanup the thread by
 * calling make_reapable.
 *
 * Note that much of this code should be non-preemptible.
 */
int
uthread_join(uthread_id_t uid, void **return_value)
{	
    if (uthreads[uid].ut_state==UT_ZOMBIE || uthreads[uid].ut_detached)
    {
        return EINVAL;
    }
    
	while (uthreads[uid].ut_has_exited==0)
	{
        uthreads[uid].ut_waiter = ut_curthr;
		uthread_block();
	}
	uthread_nopreempt_on();

	uthreads[uid].ut_detached = 1;
	if (return_value!=NULL)
	{
		*return_value = uthreads[uid].ut_exit;
	}
	make_reapable(&uthreads[uid]);
	uthread_nopreempt_off();

    return 0;
}



/*
 * uthread_detach
 *
 * Detach the given thread. Thus, when this thread's function has finished
 * executing, no other thread need (or should) call uthread_join() to perform
 * the necessary cleanup.
 *
 * There is also the special case if the thread has already exited and then
 * is detached (i.e. was already in the state UT_ZOMBIE when uthread_deatch()
 * is called). In this case it is necessary to call make_reapable on the
 * appropriate thread.
 *
 * There are also some errors to check for, see the man page for
 * pthread_detach (basically just invalid threads, etc).
 * 
 */
int
uthread_detach(uthread_id_t uid)
{
    uthreads[uid].ut_detached = 1;
	if (uthreads[uid].ut_state == UT_ZOMBIE)
	{
		make_reapable(&uthreads[uid]);
	}
	else
	{
        uthreads[uid].ut_state = UT_ZOMBIE;
	}
    return 0;
}



/*
 * uthread_self
 *
 * Returns the id of the currently running thread.
 */
uthread_id_t
uthread_self(void)
{
    assert(ut_curthr != NULL);
    return ut_curthr->ut_id;
}




/* ------------- private code -- */

/*
 * uthread_alloc
 *
 * find a free uthread_t, returns the its id (uthread_id_t).
 *
 * be careful about preemption
 */
static uthread_id_t
uthread_alloc(void)
{
    int allocated_id = -1;
    uthread_nopreempt_on();
    for (int i = 0; i < UTH_MAX_UTHREADS; i++)
    {
        if (uthreads[i].ut_state == UT_NO_STATE)
        {
            uthreads[i].ut_state = UT_TRANSITION;
            allocated_id = i;
            break; 
        }
    }
    uthread_nopreempt_off();
    return allocated_id;
}

/*
 * uthread_destroy
 *
 * Cleans up resources associated with a thread (since it's now finished
 * executing). This is called implicitly whenever a detached thread finishes 
 * executing or whenever non-detached thread is uthread_join()'d.
 */
static void
uthread_destroy(uthread_t *uth)
{
	free_stack(uth->ut_stack);
	uth->ut_prio = 0;
	uth->ut_errno = uth->ut_has_exited = uth->ut_no_preempt_count = 0;
	uth->ut_detached = 0;
	uth->ut_exit = uth->ut_waiter = NULL;
	uth->ut_state = UT_NO_STATE;
}

static uthread_mtx_t reap_mtx;
static uthread_cond_t reap_cond;

/*
 * reaper_init
 *
 * startup the reaper thread
 */
static void
reaper_init1(void) {
    list_init(&reap_queue);
    uthread_mtx_init(&reap_mtx);
    uthread_cond_init(&reap_cond);
}

static void
reaper_init2(void) {
    uthread_create(&reaper_thr_id, reaper, 0, NULL, UTH_MAXPRIO);

    assert(reaper_thr_id != -1);
}

#ifdef CLOCKCOUNT
extern int clock_count;
extern int taken_clock_count;
#endif

/*
 * reaper
 *
 * This is responsible for going through all the threads on the dead
 * threads list (which should all be in the ZOMBIE state) and then
 * cleaning up all the threads that have been detached/joined with
 * already.
 *
 * In addition, when there are no more runnable threads (besides the
 * reaper itself) it will call exit() to terminate the process.
 */
static void
reaper(long a0, char *a1[]) {
    uthread_mtx_lock(&reap_mtx);
    while(1)
    {
        uthread_t    *thread;
        int        th;

        while(list_empty(&reap_queue)) {
            uthread_cond_wait(&reap_cond, &reap_mtx);
        }

        /* go through dead threads, find detached and
         * call uthread_destroy() on them
         */
        
        list_iterate_begin(&reap_queue, thread, uthread_t, ut_link) {
            list_remove(&thread->ut_link);
            uthread_destroy(thread);
        }
        list_iterate_end();

        /* check and see if there are still runnable threads */
        for (th = 0; th < UTH_MAX_UTHREADS; th++) {
            if (th != reaper_thr_id &&
                uthreads[th].ut_state != UT_NO_STATE) {
                break;
            }
        }

        if (th == UTH_MAX_UTHREADS) {
            /* we leak the reaper's stack */
            fprintf(stderr, "uthreads: no more threads.\n");
            fprintf(stderr, "uthreads: bye!\n");
#ifdef CLOCKCOUNT
            fprintf(stderr, "clock_count = %d; taken_clock_count = %d\n", clock_count, taken_clock_count);
#endif
            exit(0);
        }
    }
}



/*
 * Turns the main context (the 'main' method that initialized
 * this process) into a regular uthread that can be switched
 * into and out of. Must be called from the main context (i.e.,
 * by uthread_init()).
 */
static void
create_first_thr(void) {
    /*
     * We create a context for the current thread, turning it into a uthread.
     * This involves allocating a uthread_id for it, assigning a uthread_t from uthreads,
     * initializing this uthread_t, including a call to uthread_getcontext to initialize
     * the thread's context structure. The thread's state is UT_ON_CPU. It simply returns from
     * create_first_thr, now happier because it's officially a uthread.
     */
    printf("we are here in creating 1st thr\n");
	uthread_id_t tid = 0; // first thread's ID is hard-wired
    ut_curthr = &uthreads[tid];
	memset(&ut_curthr->ut_link, 0, sizeof(list_link_t));
    uthread_getcontext(&ut_curthr->ut_ctx);
    ut_curthr->ut_prio = UTH_MAXPRIO;
    ut_curthr->ut_errno = ut_curthr->ut_has_exited = ut_curthr->ut_no_preempt_count = 0;
	ut_curthr->ut_detached = 1;
    ut_curthr->ut_exit = ut_curthr->ut_waiter = NULL;
    ut_curthr->ut_state = UT_ON_CPU;
}

/*
 * Adds the given thread to the reaper's queue, and wakes up the reaper.
 * Called when a thread is completely dead (is detached and exited).
 *
 */

static void
make_reapable(uthread_t *uth) {
    assert(uth->ut_detached);
    assert(ut_curthr->ut_state != UT_ZOMBIE);
    uthread_mtx_lock(&reap_mtx);
    uth->ut_state = UT_ZOMBIE;
    list_insert_tail(&reap_queue, &uth->ut_link);
    uthread_cond_signal(&reap_cond);
    uthread_mtx_unlock(&reap_mtx);
}



static char *
alloc_stack(void) {
    // Must be protected from preemption
	uthread_nopreempt_on();
	char *stack = (char *)malloc(UTH_STACK_SIZE);
	uthread_nopreempt_off();
    return stack;
}

static void
free_stack(char *stack) {
    // Must be protected from preemption
	uthread_nopreempt_on();
    free(stack);
	uthread_nopreempt_off();
}