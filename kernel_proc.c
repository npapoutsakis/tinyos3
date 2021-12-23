
#include <assert.h>
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"
#include "tinyos.h"


/* 
 The process table and related system calls:
 - Exec
 - Exit
 - WaitPid
 - GetPid
 - GetPPid

 */

static file_ops procinfo_ops = {
    .Open = NULL,
    .Read = procinfo_read,
    .Write = NULL,  //.Write = procinfo_write --> return -1; ???
    .Close = procinfo_close,
};

/* The process table */
PCB PT[MAX_PROC];
unsigned int process_count;

PCB* get_pcb(Pid_t pid)
{
  return PT[pid].pstate==FREE ? NULL : &PT[pid];
}

Pid_t get_pid(PCB* pcb)
{
  return pcb==NULL ? NOPROC : pcb-PT;
}

/* Initialize a PCB */
static inline void initialize_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->argl = 0;
  pcb->args = NULL;
  //new
  pcb->thread_count = 0;

  for(int i=0;i<MAX_FILEID;i++)
    pcb->FIDT[i] = NULL;

  rlnode_init(& pcb->children_list, NULL);
  rlnode_init(& pcb->exited_list, NULL);
  rlnode_init(& pcb->children_node, pcb);
  rlnode_init(& pcb->exited_node, pcb);
  rlnode_init(& pcb->ptcb_list, NULL);
  pcb->child_exit = COND_INIT;
}


static PCB* pcb_freelist;

void initialize_processes(){
  /* initialize the PCBs */
  for(Pid_t p=0; p<MAX_PROC; p++) {
    initialize_PCB(&PT[p]);
  }

  /* use the parent field to build a free list */
  PCB* pcbiter;
  pcb_freelist = NULL;
  for(pcbiter = PT+MAX_PROC; pcbiter!=PT; ) {
    --pcbiter;
    pcbiter->parent = pcb_freelist;
    pcb_freelist = pcbiter;
  }

  process_count = 0;

  /* Execute a null "idle" process */
  if(Exec(NULL,0,NULL)!=0)
    FATAL("The scheduler process does not have pid==0");
}


/*
  Must be called with kernel_mutex held
*/
PCB* acquire_PCB(){
  PCB* pcb = NULL;

  if(pcb_freelist != NULL) {
    pcb = pcb_freelist;
    pcb->pstate = ALIVE;
    pcb_freelist = pcb_freelist->parent;
    process_count++;
  }

  return pcb;
}

/*
  Must be called with kernel_mutex held
*/
void release_PCB(PCB* pcb){
  pcb->pstate = FREE;
  pcb->parent = pcb_freelist;
  pcb_freelist = pcb;
  process_count--;
}


/*
 *
 * Process creation
 *
 */


/*
  This function is provided as an argument to spawn,
  to execute the main thread of a process.
*/
void start_main_thread()
{
  int exitval;

  Task call =  CURPROC->main_task;
  int argl = CURPROC->argl;
  void* args = CURPROC->args;

  exitval = call(argl,args);
  Exit(exitval);
}


/*
	System call to create a new process.
 */
Pid_t sys_Exec(Task call, int argl, void* args){
  PCB *curproc, *newproc;
  
  /* The new process PCB */
  newproc = acquire_PCB();

  if(newproc == NULL) goto finish;  /* We have run out of PIDs! */

  if(get_pid(newproc)<=1) {
    /* Processes with pid<=1 (the scheduler and the init process) 
       are parentless and are treated specially. */
    newproc->parent = NULL;
  }
  else
  {
    /* Inherit parent */
    curproc = CURPROC;

    /* Add new process to the parent's child list */
    newproc->parent = curproc;
    rlist_push_front(& curproc->children_list, & newproc->children_node);

    /* Inherit file streams from parent */
    for(int i=0; i<MAX_FILEID; i++) {
       newproc->FIDT[i] = curproc->FIDT[i];
       if(newproc->FIDT[i])
          FCB_incref(newproc->FIDT[i]);
    }
  }


  /* Set the main thread's function */
  newproc->main_task = call;

  /* Copy the arguments to new storage, owned by the new process */
  newproc->argl = argl;
  if(args!=NULL) {
    newproc->args = malloc(argl);
    memcpy(newproc->args, args, argl);
  }
  else
    newproc->args=NULL;

  /* 
    Create and wake up the thread for the main function. This must be the last thing
    we do, because once we wakeup the new thread it may run! so we need to have finished
    the initialization of the PCB.
   */

  /**PTCB Initialization**/

  if(call != NULL) {

    /*Here we dont change any values of the main_thread (already initialized in CreateThread). 
    We only make tcb point to newproc and add new ptcb to newprocs ptcb list*/

    //initialization of new ptcb
    PTCB* ptcb = (PTCB*)xmalloc(sizeof(PTCB)); //acquire space for ptcb
    
    ptcb->task = call;
    ptcb->argl = argl;
    ptcb->args = (args == NULL ? NULL : args);

    ptcb->refcount = 0;
    ptcb->exited = 0;
    ptcb->detached = 0;
    ptcb->exit_cv = COND_INIT;

    //initialization of new tcb
    TCB* tcb  = spawn_thread(newproc, start_main_thread); 
    
    newproc->main_thread = tcb;
    tcb->ptcb = ptcb;
    ptcb->tcb = tcb;

    //adding ptcb to newprocs ptcb list
    rlnode_init(&ptcb->ptcb_list_node, ptcb); //Init the PTCB node, make it point itself!
    rlist_push_back(&newproc->ptcb_list, &ptcb->ptcb_list_node); // Insert the new PTCB at the list of current PCB.

    // +1 thread to PCB
    newproc->thread_count++;

    // make new thread(tcb) READY
    wakeup(ptcb->tcb);
  }

  finish:
    return get_pid(newproc);
}


/* System call */
Pid_t sys_GetPid()
{
  return get_pid(CURPROC);
}


Pid_t sys_GetPPid()
{
  return get_pid(CURPROC->parent);
}


static void cleanup_zombie(PCB* pcb, int* status)
{
  if(status != NULL)
    *status = pcb->exitval;

  rlist_remove(& pcb->children_node);
  rlist_remove(& pcb->exited_node);

  release_PCB(pcb);
}


static Pid_t wait_for_specific_child(Pid_t cpid, int* status)
{

  /* Legality checks */
  if((cpid<0) || (cpid>=MAX_PROC)) {
    cpid = NOPROC;
    goto finish;
  }

  PCB* parent = CURPROC;
  PCB* child = get_pcb(cpid);
  if( child == NULL || child->parent != parent)
  {
    cpid = NOPROC;
    goto finish;
  }

  /* Ok, child is a legal child of mine. Wait for it to exit. */
  while(child->pstate == ALIVE)
    kernel_wait(& parent->child_exit, SCHED_USER);
  
  cleanup_zombie(child, status);
  
finish:
  return cpid;
}


static Pid_t wait_for_any_child(int* status)
{
  Pid_t cpid;

  PCB* parent = CURPROC;

  /* Make sure I have children! */
  int no_children, has_exited;
  while(1) {
    no_children = is_rlist_empty(& parent->children_list);
    if( no_children ) break;

    has_exited = ! is_rlist_empty(& parent->exited_list);
    if( has_exited ) break;

    kernel_wait(& parent->child_exit, SCHED_USER);    
  }

  if(no_children)
    return NOPROC;

  PCB* child = parent->exited_list.next->pcb;
  assert(child->pstate == ZOMBIE);
  cpid = get_pid(child);
  cleanup_zombie(child, status);

  return cpid;
}


Pid_t sys_WaitChild(Pid_t cpid, int* status)
{
  /* Wait for specific child. */
  if(cpid != NOPROC) {
    return wait_for_specific_child(cpid, status);
  }
  /* Wait for any child */
  else {
    return wait_for_any_child(status);
  }

}


void sys_Exit(int exitval)
{
  PCB *curproc = CURPROC;  /* cache for efficiency */

  /* First, store the exit status */
  curproc->exitval = exitval;

  /* 
    Here, we must check that we are not the init task. 
    If we are, we must wait until all child processes exit. 
  */
  if(get_pid(curproc)==1) {
    while(sys_WaitChild(NOPROC,NULL)!=NOPROC);
  }

  sys_ThreadExit(exitval);
}


int procinfo_read(void* procinfo, char *buf, unsigned int size){

  procinfo_cb* info = (procinfo_cb *)procinfo;

  if(info == NULL)
    return NOFILE;

  //Loop to scan PT array
  while(info->pcb_cursor < MAX_PROC){
    
    if(PT[info->pcb_cursor].pstate != FREE){

      PCB* current_pcb = &PT[info->pcb_cursor];  
      
      //Take information from PCB
      info->process_info.pid = get_pid(current_pcb);
      info->process_info.ppid = get_pid(current_pcb->parent);   

      //Zombie or Alive?
      info->process_info.alive = current_pcb->pstate == ZOMBIE ? 0 : 1;

      //Passing PCB's args
      info->process_info.main_task = current_pcb->main_task;
      info->process_info.argl = current_pcb->argl;
      info->process_info.thread_count = current_pcb->thread_count;

      if(current_pcb->args != NULL){
        memcpy(info->process_info.args, current_pcb->args, info->process_info.argl);
      }

      //Storing data from procinfo to the buffer
      memcpy(buf, (char *)&info->process_info, sizeof(info->process_info));

      //Increase Cursor Pointer
      info->pcb_cursor++;

      return sizeof(info->process_info);
    }

    //If pstate is FREE just increase the cursor pointer by 1
    info->pcb_cursor++;
  }

  //Having checked the entire array, return -1
  return NOFILE;
}

int procinfo_close(void* procinfo){
  //Get the proc info and free it
  procinfo_cb* proc_info = (procinfo_cb*) procinfo;
  
  if(proc_info == NULL)
    return NOFILE;
  else{
    free(proc_info);
    return 0;
  }
}


Fid_t sys_OpenInfo()
{
  Fid_t fid;
  FCB* fcb;

  //Reserve a position for the FCB
  int exit_value = FCB_reserve(1, &fid, &fcb);
  
  if(exit_value == 0){
    return NOFILE;
  }

  //Procinfo Block Creation
  procinfo_cb* info = (procinfo_cb *)xmalloc(sizeof(procinfo_cb));
  
  if(info == NULL){
    return NOFILE;
  }
  else{
    //Set the cursor at the top of the array
    info->pcb_cursor = 1;
  }

  //FCB_Init
  fcb->streamobj= info; 
  fcb->streamfunc= &(procinfo_ops);

  //Return the Id
  return fid;
}
