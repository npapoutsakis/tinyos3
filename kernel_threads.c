
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
  
  //Make sure that ptcb exists
  assert(cur_thread()->ptcb != NULL);

  Task call =  cur_thread()->ptcb->task;
  int argl = cur_thread()->ptcb->argl;
  void* args = cur_thread()->ptcb->args;

  exitval = call(argl,args);
  ThreadExit(exitval);
}

// Checks if thread(ptcb) exists in currproc thread list(ptcb_list) 
int check_valid_ptcb(Tid_t tid){
  // Checks if ptcb is valid/exists

  // if rlist_find returns 1, ptcb exists in current's proccess ptcb list
  PTCB* ptcb = (PTCB*) tid;
  if(rlist_find(&CURPROC->ptcb_list, ptcb, NULL) && tid != NOTHREAD)
    return 1;
  else 
    return 0;
}

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args){

  if(task != NULL){

    //initialize ptcb and tcb
    //initialization of new ptcb  
    PTCB* ptcb = (PTCB*)xmalloc(sizeof(PTCB)); //acquire space for ptcb
    ptcb->task = task;
    ptcb->argl = argl;
    
    if(args != NULL) {
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

    ptcb->exitval = 0;
    ptcb->exit_cv = COND_INIT;
    ptcb->exited = 0;
    ptcb->detached = 0;
    ptcb->refcount = 0;

    //Pass ptcb to curr_thread, in order to pass process info to new thread
    assert(cur_thread() != NULL);
    //cur_thread()->ptcb = ptcb; //THIS MIGHT BE NEEDED CHECK

    // IF SOMETHING DOESN'T WORK ADD PCB* FIELD TO PTCB 

    //initialization of new tcb
    TCB* tcb  = spawn_thread(CURPROC, start_new_multithread);

    // Connect new tcb with ptcb
    tcb->ptcb = ptcb;
    ptcb->tcb = tcb;
    //ptcb->tcb->owner_pcb = tcb->owner_pcb; ---> spawn_thread does this!
    
    // Add ptcb_node to pcb's ptcb_list
    rlnode_init(&ptcb->ptcb_list_node, ptcb); //Init the PTCB node, make it point itself!
    rlist_push_back(&CURPROC->ptcb_list, &ptcb->ptcb_list_node); // Insert the new PTCB at the list of current PCB.

    // +1 thread to PCB
    CURPROC->thread_count++;

    //Wake Up the new thread!
    wakeup(ptcb->tcb); 

    //Return the Tid_t of the ptcb we created
    return (Tid_t) ptcb;
  }

  return NOTHREAD;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf(){

  assert(cur_thread()->ptcb != NULL);
	return (Tid_t) cur_thread()->ptcb;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval){
  
  PTCB* T2 = (PTCB*) tid;

  // Checks if tid is pointing to a valid/existing thread
  if(!check_valid_ptcb(tid))
    return -1;

  // If thread tries to self-join, quit
  if(tid == sys_ThreadSelf())
    return -1;

  // If thread is detached, quit
  if(T2->detached == 1)
    return -1;

  T2->refcount++; //Increase ref counter by 1

  while(T2->exited != 1 && T2->detached != 1){ // Wait till new thread exits or gets detached.
    kernel_wait(&T2->exit_cv, SCHED_USER);
  }

  T2->refcount--; // Since T2 detaches or exits, decrease ref counter

  if(T2->detached == 1) // If T2 gets detached dont return the exit value
    return -1;

  if(exitval != NULL) // exitval save 
    *exitval = T2->exitval;

  if(T2->refcount == 0){ // If T2 exited and no other thread waits then remove from PTCB list
    rlist_remove(&T2->ptcb_list_node); 
    free(T2);
  }
  
  return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid){

  PTCB* Detached_PTCB = (PTCB*) tid;

  // Checks if tid is pointing to a valid/existing thread
  if(!check_valid_ptcb(tid))
    return -1;

  Detached_PTCB->detached = 1; // Set ptcb to detached
  kernel_broadcast(&Detached_PTCB->exit_cv); //Wake up Threads
 
  return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval){

  PTCB* ptcb = (PTCB  *)sys_ThreadSelf();
  
  ptcb->exited = 1;
  ptcb->exitval = exitval;
  kernel_broadcast(&ptcb->exit_cv); // signal rest of the threads

  /*sys_Exit()*/
  PCB* curproc = CURPROC;
  curproc->thread_count--;
  if(curproc->thread_count == 0){

    if (get_pid(curproc)!= 1){

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
      rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
      kernel_broadcast(& curproc->parent->child_exit);
    }

    assert(is_rlist_empty(& curproc->children_list));
    assert(is_rlist_empty(& curproc->exited_list));
    /*
      Do all the other cleanup we want here, close files etc.
     */

    /* Release the args data */
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

    /* Disconnect my main_thread */
    curproc->main_thread = NULL;
    
    /* Now, mark the process as exited. */
    curproc->pstate = ZOMBIE;
  }

  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);

}
