#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <opt-A2.h>
#if OPT_A2
#include <mips/trapframe.h>
#endif

#if OPT_A2
  /* sys_fork*/
int sys_fork(struct trapframe *tf,
             pid_t *retval)
{
  KASSERT(tf != NULL);

  *retval = -1;

  struct proc *childproc = proc_create_runprogram(curproc->p_name);

  if (childproc == NULL) {
    /* error when proc_create_runprogram() returns NULL */
    /* out of memory */
    return ENOMEM;
  }

  KASSERT(childproc->p_pid > 0);

  /* create the child's address and copy the parent's address space to it */
  KASSERT(curproc->p_addrspace != NULL);

  struct addrspace *childas = as_create();

  if (childas == NULL) {
    return ENOMEM;
  }

  int as_copy_err;
  as_copy_err = as_copy(curproc_getas(), &childas);
  if (as_copy_err != 0) {
    /* if as_copy() returns a non-zero error code, report it to syscall dispatcher */
    return as_copy_err;
  }

  spinlock_acquire(&childproc->p_lock);
  childproc->p_addrspace = childas;
  spinlock_release(&childproc->p_lock);

  /* create parent-child relationship */
  childproc->p_parent = curproc;

  KASSERT(curproc->p_children != NULL);
  KASSERT(curproc->p_mutex != NULL);
  lock_acquire(curproc->p_mutex);
  array_add(curproc->p_children,
            (void *)childproc,
            NULL);
  lock_release(curproc->p_mutex);

  /* need to give the child proc the new address space*/
  /* lookat curproc_setas()*/

  struct trapframe *tf_new = kmalloc(sizeof(struct trapframe));
	KASSERT(tf_new != NULL);
	memcpy((void *)tf_new, (void *)tf, sizeof(struct trapframe));
  // *tf_new = *tf;

  int err_thread_fork;
  err_thread_fork =
  thread_fork(childproc->p_name, 
              childproc, 
              (void*)&enter_forked_process, 
              tf_new,
              0);

  if (err_thread_fork != 0) {
    kfree(tf_new);
    // proc_destroy(childproc);
    return err_thread_fork;
  }

  *retval = childproc->p_pid;

  return(0);
}
#endif

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {
  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  #if OPT_A2
  // if (p->p_parent != NULL) {
  //   p->p_exited = true;
  //   p->p_exitcode = _MKWAIT_EXIT(exitcode);

  //   lock_acquire(p->p_mutex);
  //   cv_broadcast(p->p_exited_cv, p->p_mutex);
  //   lock_release(p->p_mutex);
  // }

  p->p_exitcode = _MKWAIT_EXIT(exitcode);
  p->p_exited = true;

  lock_acquire(p->p_mutex);
  cv_broadcast(p->p_exited_cv, p->p_mutex);
  lock_release(p->p_mutex);

  if (p->p_parent == NULL) {
    proc_destroy(p);
  }
  
  #else
  (void)p_exitcode;
  proc_destroy(p);
  #endif
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  #if OPT_A2

  *retval = curproc->p_pid;

  #else

  *retval = 1;

  #endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  #if OPT_A2

  KASSERT(curproc != NULL);
  KASSERT(curproc->p_children != NULL);
  struct proc *childproc = NULL;
  unsigned int numchildren = array_num(curproc->p_children);
  unsigned int i;
  for (i = 0; i < numchildren; i++) {
    if (pid == ((struct proc*)array_get(curproc->p_children, i))->p_pid) {
      childproc = (struct proc*)array_get(curproc->p_children, i);
      break;
    }
  }

  if (childproc == NULL) {
    panic("waitpid() called by wrong process");
  }

  /* wait for the child to exit */
  lock_acquire(childproc->p_mutex);
  while (!childproc->p_exited) {
    cv_wait(childproc->p_exited_cv, childproc->p_mutex);
  }
  lock_release(childproc->p_mutex);
  exitstatus = childproc->p_exitcode;

  result = copyout((void *)&exitstatus,status,sizeof(int));

  #else

  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  #endif

  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

