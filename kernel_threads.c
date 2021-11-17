
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"


/*
  This function is provided as an argument to spawn,
  to execute the main thread of a process.
*/
void start_new_multithread()
{
  int exitval;
  assert(cur_thread()->ptcb != NULL);
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
Tid_t sys_CreateThread(Task task, int argl, void* args){

  if(task!=NULL){

    //initialize ptcb and tcb
    //initialization of new ptcb  
    PTCB* ptcb = (PTCB*)xmalloc(sizeof(PTCB)); //acquire space for ptcb
    ptcb->task = task;
    ptcb->argl = argl;
    
    if(args!=NULL) {
      // ptcb->args = malloc(argl);
      // memcpy(ptcb->args, args, argl);
      ptcb->args = args;
      assert(ptcb->args != NULL);
      //fprintf(stderr, "args value");  
    }
    else{
      ptcb->args=NULL;
      assert(ptcb->args == NULL);
    }
    // if(args!=NULL) {
    //   ptcb->args = malloc(argl);
    //   memcpy(ptcb->args, args, argl);
    // }
    // else
    //   ptcb->args=NULL;
    ptcb->exitval = 0;
    ptcb->exit_cv = COND_INIT;
    ptcb->exited =0;
    ptcb->detached = 0;
    ptcb->refcount = 0;

    //Pass ptcb to curr_thread, in order to pass process info to new thread
    assert(cur_thread() != NULL);
    //cur_thread()->ptcb = ptcb; //THIS MIGHT BE NEEDED CHECK

    // IF SOMETHING DOESN'T WORK ADD PCB* FIELD TO PTCB 

    //initialization of new tcb
    TCB* tcb  = spawn_thread(CURPROC, start_new_multithread);

    //CURPROC->main_thread = tcb;

    // Connect new tcb with ptcb
    tcb->ptcb = ptcb;
    ptcb->tcb = tcb;
    //ptcb->tcb->owner_pcb = tcb->owner_pcb;
    
    // Add ptcb_node to pcb's ptcb_list
    rlnode_init(&ptcb->ptcb_list_node, ptcb); // CHECK INSTEAD OF PTCB --> NULL
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
  assert(cur_thread()->ptcb != NULL);
	return (Tid_t) cur_thread()->ptcb;
}


int check_valid_ptcb(Tid_t tid){
  // Checks if ptcb is valid/exists

  // if rlist_find returns 1, ptcb exists in current's proccess ptcb list
  PTCB* ptcb = (PTCB*) tid;
  if(rlist_find(&CURPROC->ptcb_list, ptcb, NULL))
    return 1;
  else 
    return 0;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval){

  // tid is (PTCB*) of T2
  PTCB* T2 = (PTCB*) tid;

  // Checks if tid is a valid ptcb pointer
  if(!check_valid_ptcb(tid))
    return -1;

  TCB* T2_tcb = T2->tcb;
  PCB* T2_pcb = T2_tcb->owner_pcb;

  if(T2->detached == 1){
    //fprintf(stderr, "Thread already detached");
    return -1;
  }

  if(T2_pcb != CURPROC){
    //fprintf(stderr, "Process different than Current proccess");
    return -1;
  }

  if(cur_thread() == T2_tcb){
    //fprintf(stderr, "Current Thread tries to self join");
    return -1;
  }

  if(T2->exited == 1){
    //fprintf(stderr, "Thread has already exited");
    return -1;
  }

  //wait until T2 exits or detaches
  while(T2->exited == 0 && T2->detached == 0)
    kernel_wait(&T2->exit_cv,SCHED_USER);
 
  // In case T2 detached
  if(T2->detached == 1)
    return -1;

  // In case T2 exited
  if(T2->exited == 1){ 
    T2->refcount++;
    *exitval = T2->exitval;
  }

  if(exitval != NULL){
    assert(T2->refcount>0);
    T2->refcount--;
    if(T2->refcount==0){
      rlist_remove(&T2->ptcb_list_node);
      free(T2);
    }

  return 0;

  }

  return -1;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  
  PTCB* Detached_PTCB = (PTCB*) tid;

  // Checks if tid is a valid ptcb pointer
  if(!check_valid_ptcb(tid))
    return -1;

  assert(Detached_PTCB->tcb != NULL);

  if(Detached_PTCB->exited==1)
    return -1;

  Detached_PTCB->detached=1;
  //in case this thread has joined threads
  kernel_broadcast(&Detached_PTCB->exit_cv);
  return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  PTCB* ptcb = cur_thread()->ptcb;

  
//initialize ton exit metabliton tou ptcb
  ptcb->exited = 1;
  ptcb->exitval = exitval;
  kernel_broadcast(&ptcb->exit_cv);//broadcast sto exit_cv gia na ksipnisoun osoi perimenoun
  //elegxos threadcount kai refcount
  if(ptcb->refcount == 0)
  rlist_remove(&ptcb->ptcb_list_node);
  
//an thread count == 0 katharizo kai enimerono to PTCB opos tin sys_exit
  if(CURPROC->thread_count == 0){
    PCB *curproc = CURPROC;  /* cache for efficiency */

  /* Do all the other cleanup we want here, close files etc. */
  if(curproc->args) {
    free(curproc->args);
    curproc->args = NULL;
  }

  /* Clean up FIDT */
  for(int i=0;i<MAX_FILEID;i++) {
    if(curproc->FIDT[i] != NULL) {
      FCB_decref(curproc->FIDT[i]);
      curproc->FIDT[i] = NULL;
    }
  }

  /* Reparent any children of the exiting process to the 
     initial task */
  PCB* initpcb = get_pcb(1);
  while(!is_rlist_empty(& curproc->children_list)) {
    rlnode* child = rlist_pop_front(& curproc->children_list);
    child->pcb->parent = initpcb;
    rlist_push_front(& initpcb->children_list, child);
  }

  /* Add exited children to the initial task's exited list 
     and signal the initial task */
  if(!is_rlist_empty(& curproc->exited_list)) {
    rlist_append(& initpcb->exited_list, &curproc->exited_list);
    kernel_broadcast(& initpcb->child_exit);
  }

  /* Put me into my parent's exited list */
  if(curproc->parent != NULL) {   /* Maybe this is init */
    rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
    kernel_broadcast(& curproc->parent->child_exit);
  }

  /* Disconnect my main_thread */
  curproc->main_thread = NULL;

  /* Now, mark the process as exited. */
  curproc->pstate = ZOMBIE;
  }
  CURPROC->exitval=exitval;
  CURPROC->thread_count--;
  //kernel_unlock();
  kernel_sleep(EXITED,SCHED_USER);//as paei to thread gia nani
  //kernel_lock();

}


