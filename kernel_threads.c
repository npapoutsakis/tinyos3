
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"


/*
  This function is provided as an argument to spawn,
  to execute the main thread of a process.
*/
void start_new_multithread()
{
  int exitval;

  Task call =  cur_thread()->ptcb->task;
  int argl = cur_thread()->ptcb->argl;
  void* args = cur_thread()->ptcb->args;

  exitval = call(argl,args);
  ThreadExit(exitval);
}

/*void increase_refcount(PTCB* ptcb){
  ptcb->refcount++;
}*/

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{

  //initialize ptcb and tcb

if(task!=NULL){

    
    PTCB* ptcb = (PTCB*)xmalloc(sizeof(PTCB)); //acquire space for ptcb
    ptcb->refcount = 0;
    ptcb->exited =0;
    ptcb->detached = 0;
    ptcb->exit_cv = COND_INIT;


    //TCB* tcb;

    TCB* tcb  = spawn_thread(CURPROC, start_new_multithread); //??
    CURPROC->main_thread = tcb;
    tcb->ptcb = ptcb;
    ptcb->tcb = tcb;
    ptcb->refcount++;

    rlnode_init(&ptcb->ptcb_list_node, ptcb);
    rlist_push_back(&CURPROC->ptcb_list, &ptcb->ptcb_list_node);

    CURPROC->thread_count ++;

    wakeup(ptcb->tcb); 

    return (Tid_t) ptcb;
  }

  return NOTHREAD;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) cur_thread();
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
	return -1;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
	return -1;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

}

