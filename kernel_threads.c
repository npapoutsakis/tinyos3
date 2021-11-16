
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"


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
  ThreadExit(exitval); // sys_ThreadExit or ThreadExit?
}

/*void increase_refcount(PTCB* ptcb){
  ptcb->refcount++;
}*/

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args){

  if(task!=NULL){

    //initialize ptcb and tcb
    //initialization of new ptcb
    PTCB* ptcb = (PTCB*)xmalloc(sizeof(PTCB)); //acquire space for ptcb
    ptcb->task = task;
    ptcb->argl = argl;
    ptcb->args = args;
    ptcb->exitval = CURPROC->exitval;
    ptcb->exit_cv = COND_INIT;
    ptcb->exited =0;
    ptcb->detached = 0;
    ptcb->refcount = 0;

    //Pass ptcb to curr_thread, in order to pass process info to new thread
    cur_thread()->ptcb = ptcb;

    //initialization of new tcb
    TCB* tcb  = spawn_thread(CURPROC, start_new_multithread);


    //CURPROC->main_thread = tcb;


    // Connect new tcb with ptcb
    tcb->ptcb = ptcb;
    ptcb->tcb = tcb;
    //ptcb->tcb->owner_pcb = tcb->owner_pcb;
    
    // Add ptcb_node to pcb's ptcb_list
    rlnode_init(&ptcb->ptcb_list_node, ptcb);
    rlist_push_back(&CURPROC->ptcb_list, &ptcb->ptcb_list_node);

    // +1 thread to PCB
    CURPROC->thread_count++;

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
  if(tid != NULL){

    // tid is (PTCB*) of T2
    PTCB* ptcb = (PTCB*) tid;
    TCB* tcb = ptcb->tcb;
    PCB* pcb = tcb->owner_pcb;

    if(ptcb->detached == 1){
      printf("Thread Detached");
      return -1;
    }

    if(pcb != CURPROC){
      printf("Process different than Current proccess");
      return -1;
    }

    if(cur_thread() == tcb){
      printf("Current Thread self joins");
      return -1;
    }

    if(ptcb->exited == 1){
      printf("Thread Exited");
      return -1;
    }

    *exitval = kernel_wait(&ptcb->exit_cv,SCHED_USER);
  
    return 0;
  }

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

