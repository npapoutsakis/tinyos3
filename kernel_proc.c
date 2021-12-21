
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
    .Write = NULL,
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
  
  procinfo_cb* info_cb = (procinfo_cb*) procinfo;

  Pid_t cur_pid;
  Pid_t cur_ppid;

/*Stin while diatrexoume to ProcessTable mexri na broume
to proto oxi free PCB. Kai meta se ena for loop briskoume
tin thesi tou parentPCB tou ston idio pinaka      */
  while (info_cb->PT_cursor <= MAX_PROC) {
    info_cb->PT_cursor += 1;

    if(info_cb->PT_cursor == MAX_PROC){
      info_cb->PT_cursor=1;
      return -1;
    }

    if(PT[info_cb->PT_cursor].pstate!=FREE){
      cur_pid = info_cb->PT_cursor;                //to ID tou proc
      int i;
      PCB* parent = PT[info_cb->PT_cursor].parent; //to Parent PCB

      for(i = 0; i < MAX_PROC; i++){            //loop gia anazitisi
        if(&PT[i] == parent)                    //tou parent ID
          break;                                //sto ProccessTable
      }

      cur_ppid = i;                             //to ID tou parent
      break;
    }

  }

  info_cb->process_info.pid=cur_pid;                 //copy tou pid
  info_cb->process_info.ppid=cur_ppid;               //copy ParentID



  if(PT[info_cb->PT_cursor].pstate==ZOMBIE)        //copy pState
    info_cb->process_info.alive = 0; //is not alive
  else
    info_cb->process_info.alive = 1; //is alive

  info_cb->process_info.thread_count = PT[info_cb->PT_cursor].thread_count;             //copy #threadlist
  info_cb->process_info.main_task=PT[info_cb->PT_cursor].main_task;                     //copy main task
  info_cb->process_info.argl=PT[info_cb->PT_cursor].argl;                               //copy argl

  memcpy(info_cb->process_info.args, PT[info_cb->PT_cursor].args, PROCINFO_MAX_ARGS_SIZE);  //copy args

  memcpy(buf,&(info_cb->process_info),size);                               //copy curinfo to buffer =)

  return size;
  
}

int procinfo_close(void* this){
  
  procinfo_cb* inf=(procinfo_cb*) this;
  
  if(inf!=NULL) {   
    
    inf = NULL;
    free(inf);
    return 0;
  }
  return -1;
}


Fid_t sys_OpenInfo()
{

  Fid_t fid[1];
  FCB* fcb[1];

  int fid_works = FCB_reserve(1,fid,fcb);
  
  if(!fid_works) 
    return NOFILE;
//----------------------------------------------------

//dunamiki desmeusi tou infoControlBlock--------------
  procinfo_cb* info_cb = (procinfo_cb*) xmalloc(sizeof(procinfo_cb));
  info_cb->PT_cursor = 1;

  if(info_cb==NULL)
    return NOFILE;
//----------------------------------------------------

  fcb[0]->streamobj= info_cb;       //anathesi infoCB
  fcb[0]->streamfunc= &(procinfo_ops);  //sto FCB pou ftiaksame

  return fid[0];
}

