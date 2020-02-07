/*
 *   FILE: uthread_sched.c 
 * AUTHOR: Peter Demoreuille
 *  DESCR: scheduling wack for uthreads
 *   DATE: Mon Oct  1 00:19:51 2001
 *
 * Modified to handle time slicing by Tom Doeppner
 *   DATE: Sun Jan 10, 2016
 * Further modifications in January 2020
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>

#include "uthread.h"
#include "uthread_private.h"
#include "uthread_ctx.h"
#include "uthread_queue.h"
#include "uthread_bool.h"
#include "uthread_sched.h"


/* ---------- globals -- */

static utqueue_t runq_table[UTH_MAXPRIO + 1]; /* priority runqueues */
static int uthread_no_preempt;                /* preemption not allowed */


/* ----------- public code -- */


/*
 * uthread_yield
 *
 * Causes the currently running thread to yield use of the processor to
 * another thread. The thread is still runnable however, so it should
 * be in the UT_RUNNABLE state and schedulable by the scheduler. When this
 * function returns, the thread is executing again. In more detail:
 * when this function is called, the current thread rejoins the run queue.
 * When this thread is the first thread in the highest-priority non-empty
 * thread queue, it will return from uthread_yield (in response to call to
 * thread_switch).
 * Be sure to disable preemption while manipulating the run queues!
 */
void
uthread_yield(void) {
    //N_OT_YET_IMPLEMENTED("UTHREADS: uthread_yield");
	
	uthread_nopreempt_on();
	ut_curthr->ut_state = UT_RUNNABLE;
	//As slide 02
	// the current thread rejoins the run queue. When this thread is the first thread in the highest-priority non-empty thread queue
	/*for (size_t i = UTH_MAXPRIO; i >=0; i--)
	{
		if (!utqueue_empty(&runq_table[i]))
		{
			utqueue_enqueue(&runq_table[ut_curthr->ut_prio], ut_curthr);
			uthread_switch();
			break;
		}	
	}*/

	utqueue_enqueue(&runq_table[ut_curthr->ut_prio], ut_curthr);
	uthread_switch();
	
	
	uthread_nopreempt_off();
}



/*
 * uthread_block
 *
 * Put the current thread to sleep, pending an appropriate call to 
 * uthread_wake().
 * Make sure preemption is disabled!
 */
void
uthread_block() {
    //N_OT_YET_IMPLEMENTED("UTHREADS: uthread_block");
	uthread_nopreempt_on();
	ut_curthr->ut_state = UT_WAIT;
	uthread_switch();
	uthread_nopreempt_off();
}


/*
 * uthread_wake
 *
 * Wakes up the supplied thread (schedules it to be run again).  The
 * thread may already be runnable or (well, if uthreads allowed for
 * multiple cpus) already on cpu, so make sure to only mess with it if
 * it is actually in a wait state. Note that if the target thread has
 * a higher priority than the caller does, the caller should yield.
 */
void
uthread_wake(uthread_t *uthr) {
    //N_OT_YET_IMPLEMENTED("UTHREADS: uthread_wake");
	if (uthr->ut_state==UT_WAIT)
	{
		uthr->ut_state = UT_RUNNABLE;
		uthread_nopreempt_on();
		int prio = uthr->ut_prio;
		utqueue_enqueue(&runq_table[prio], uthr);
		uthread_nopreempt_off();
	}
	uthread_nopreempt_on();
	if (uthr->ut_prio>ut_curthr->ut_prio&&!uthread_no_preempt)
	{
		/* !!!Attention!!! */
		
		if (ut_curthr->ut_has_exited)
		{
			// uthread_switch();
			//panic
			PANIC("Exited thread calling wake");

		}
		uthread_yield();
		
	}
	uthread_nopreempt_off();
}


/*
 * uthread_setprio
 *
 * Changes the priority of the indicated thread.  Note that if the thread
 * is in the UT_RUNNABLE state (it's runnable but not on cpu) you should
 * change the list it's waiting on so the effect of this call is
 * immediate. Yield to the target thread if its priority is higher than
 * the caller's.
 * If the thread's state is UT_TRANSITION, then it's a brand new thread
 * and is going on to the run queue for the first time. Make sure its state
 * is set to UT_RUNNABLE
 * Be careful about preemption.
 * Retyrns 1 on success, 0 if there's an error (such as the thread doesn't
 * exist).
 */
int
uthread_setprio(uthread_id_t id, int prio) {
    //N_OT_YET_IMPLEMENTED("UTHREADS: uthread_setprio");
	uthread_nopreempt_on();
	uthread_t *uth = &uthreads[id];
	int prev_prio = uth->ut_prio;
	uth->ut_prio = prio;
	
	uthread_nopreempt_off();
	
	if (uth->ut_state==UT_RUNNABLE)
	{
		printf("we are here in setprio for RUNNABLE thread %d\n",id);
		uthread_nopreempt_on();

		utqueue_remove(&runq_table[prev_prio],uth);
				
		utqueue_enqueue(&runq_table[prio], uth);
		uthread_nopreempt_off();
		
		if (ut_curthr->ut_prio < uth->ut_prio)
		{
			uthread_yield();
		}
	}
	else if (uth->ut_state==UT_TRANSITION)
	{
		uthread_nopreempt_on();
		uth->ut_state = UT_RUNNABLE;
		
		utqueue_enqueue(&runq_table[prio], uth);
		uthread_nopreempt_off();
	}

    return 1;
}

/*
 * uthread_no_preempt_on
 *
 * Disable preemption. Uses a global mask rather than making sys calls.
 * Keeps track of nesting of calls on a per-thread basis.
 */
void uthread_nopreempt_on() {
    uthread_no_preempt = 1;
	ut_curthr->ut_no_preempt_count++;
}

void uthread_nopreempt_off() {
	if (--ut_curthr->ut_no_preempt_count == 0)
		uthread_no_preempt = 0;
	assert(ut_curthr->ut_no_preempt_count >= 0);
}



/* ----------- private code -- */

/*
 * uthread_switch()
 *
 * This is where all the magic is.  Find the hightest-priority runnable thread and
 * then switch to it using uthread_swapcontext().  Also don't forget to take
 * care of setting the ON_CPU thread state and the current thread. Note that
 * it is okay to switch back to the calling thread if it is the highest
 * priority runnable thread.
 *
 * There must be a runnable thread!
 */
void
uthread_switch() {
    assert(ut_curthr->ut_no_preempt_count > 0);
    assert(uthread_no_preempt);
    
    //N_OT_YET_IMPLEMENTED("UTHREADS: uthread_switch");
	uthread_t* ut_nexthr=NULL;

	for (int i = UTH_MAXPRIO; i>=0; i--)
	{
		if (!utqueue_empty(&runq_table[i]))
		{
			ut_nexthr = utqueue_dequeue(&runq_table[i]);
			break;
		}
	}
	if (ut_nexthr==NULL)
	{
		PANIC("no runnable threads");
	}

	//need temp for curthr
	uthread_t* prev_curthr = ut_curthr;
	ut_curthr = ut_nexthr;
	uthread_swapcontext(&prev_curthr->ut_ctx, &ut_nexthr->ut_ctx);
	
	ut_curthr->ut_state = UT_ON_CPU;
	return;
	
}


void uthread_start_timer(void);
/*
 * uthread_sched_init
 *
 * Setup the scheduler. This is called once from uthread_init().
 * This also kicks off the time-slice timer.
 */
void
uthread_sched_init(void) {
    int i;
	printf("We are here in utqueue init\n");
    for (i=0; i<=UTH_MAXPRIO; i++) {
        utqueue_init(&runq_table[i]);
    }
	uthread_start_timer();
}

static void clock_interrupt(int);
static sigset_t VTALRMmask;

/*
 * uthread_start_timer
 *
 * Start up the time-slice clock, which uses ITIMER_VIRTUAL and SIGVTALRM.
 * For test purposes, the time-slice interval is as small as possible
 */
void
uthread_start_timer() {
	sigemptyset(&VTALRMmask);
	sigaddset(&VTALRMmask, SIGVTALRM);
	struct sigaction timesliceact;
	timesliceact.sa_handler = clock_interrupt;
	timesliceact.sa_mask = VTALRMmask;
	timesliceact.sa_flags = SA_RESTART;		// avoid EINTR
	struct timeval interval = {0, 1}; // every microsecond
	struct itimerval timerval;
	timerval.it_value = interval;
	timerval.it_interval = interval;
	sigaction(SIGVTALRM, &timesliceact, 0);
	setitimer(ITIMER_VIRTUAL, &timerval, 0);	// time slicing is started!
}

/*
 * clock_interrupt
 *
 * At each clock interrupt (SIGVTALRM), call uthread_yield if preemption is not masked.
 * Be sure to unblock SIGVTALRM if uthread_yield is called -- SIGVTALRM is masked on entry
 * to this function and is not automatically unmasked until it returns.
*/
static void
clock_interrupt(int sig) {
    //N_OT_YET_IMPLEMENTED("UTHREADS: clock_interrupt");
    if (uthread_no_preempt)
	{
		return;
	}else
	{
		sigprocmask(SIG_UNBLOCK,&VTALRMmask,0);
		uthread_yield();
	}
	
}
