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
#include <test.h>
#include <vm.h>
#include <vfs.h>
#include <kern/fcntl.h>
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

  /* Create parent-child relationship. Set the parent. */
  childproc->p_parent = curproc;

  /* Add the child to the parent's list of children*/
  KASSERT(curproc->p_children != NULL);
  KASSERT(curproc->p_mutex != NULL);
  lock_acquire(curproc->p_mutex);
  array_add(curproc->p_children,
            (void *)childproc,
            NULL);
  lock_release(curproc->p_mutex);

  /* Need to give the child proc the new address space. */
  /* Look at curproc_setas(). */

  /* Copy the parent's trap frame to the kernel’s
     heap, then copy from kernel’s heap to child. */
  struct trapframe *tf_new = kmalloc(sizeof(struct trapframe));
	KASSERT(tf_new != NULL);
	memcpy((void *)tf_new, (void *)tf, sizeof(struct trapframe));

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

  /* Return the child's pid. */
  *retval = childproc->p_pid;

  return(0);
}
#endif

#if OPT_A2
int sys_execv(userptr_t progname, userptr_t args) {

  /* Copy the program path from user space into kernel */
  char *kprogname = kmalloc(sizeof(char) * (strlen((char *)progname) + 1));
  if (kprogname == NULL) {
    return ENOMEM;
  }

  strcpy(kprogname, (char *)progname);

  /* Count the number of arguments. */
  unsigned int argc = 0;
  for (char **p = (char **)args; *p != NULL; p++) {
    argc++;
  }

  /* Why does this not work? */
  // for (int i = 0; i < argc; i++) {
  //   kprintf("%s ", args[i]);
  // }

  /* Create the new args array in kernel memory. */
  // char **kargs; // Using this initialization causes some weird kernel panic: 
  char *kargs[argc+1];
  *(kargs + argc) = NULL;
  for (unsigned int i = 0; i < argc; i++) {
    *(kargs + i) = kmalloc(sizeof(char) * (strlen((char *)(*((char **)args + i))) + 1));
    strcpy(*(kargs + i), (char *)(*((char **)args + i)));
  }

  // kprintf("copied program name: %s, copied %u arguments: \n", kprogname, argc);
  // for (unsigned int i = 0; i < argc; i++) {
  //   kprintf("%s ", *(kargs + i));
  // }
  // kprintf("\n");

	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open((char *)progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

  // Unlike runprogram(), for execv(), process structure remains unchanged
	// /* We should be a new process. */
	// KASSERT(curproc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

  /* Save the old as pointer. */
  struct addrspace *oldas = curproc_getas();

	/* Switch to it and activate it. */
	curproc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed. */
		return result;
	}

  /* Compute the total space required for copying args. */
  unsigned int args_size = 0;
  args_size += (argc + 1)*4;

  for (unsigned int i = 0; i < argc; i++) {
    args_size += strlen(*(kargs + i)) + 1;
  }

  args_size = ROUNDUP(args_size, 8);

  /* Save the stack pointer to the top of the stack. */
  vaddr_t stackptr_start = USERSTACK - args_size;

  /* Move the stack pointer to the start of arg strings. */
  stackptr = stackptr_start + (argc + 1)*4;

  /* Keep track of the addrs of strings while copying
     individual strings. */
  
  for (unsigned int i = 0; i < argc; i++) {
    memcpy((void *)stackptr, *(kargs + i), strlen(*(kargs + i)) + 1);
    memcpy((void *)(stackptr_start + i*sizeof(vaddr_t)),
           &stackptr, sizeof(vaddr_t));
    stackptr += strlen(*(kargs + i)) + 1;
  }
  /* Test */
  // ########################################
  for (char *p = stackptr_start;
       p != stackptr_start + (argc + 1)*4; p++) {
        if ((unsigned int)p % 4 == 0) {
          kprintf("%x:\t", (unsigned int)p);
        }
        // kprintf("%x", *p);
        if ((unsigned int)p % 4 == 3) {
          kprintf("\n");
        }
       }
  for (char *p = stackptr_start + (argc + 1)*4;
       p != USERSTACK; p++) {
        if ((unsigned int)p % 4 == 0) {
          kprintf("%x:\t", (unsigned int)p);
        }
        if (*p == '\0') {
          kprintf("\\0\t");
        } else {
          kprintf("%c\t", *p);
        }
        if ((unsigned int)p % 4 == 3) {
          kprintf("\n");
        }
       }
  // ########################################

  /* Delete the old address space */
  as_destroy(oldas);

  /* Free the kernel-allocated stuff */
  kfree(kprogname);
  for (unsigned int i = 0; i < argc; i++) {
    kfree(*(kargs + i));
  }

	/* Warp to user mode. */
	// enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
	// 		  stackptr, entrypoint);
  enter_new_process(argc, args, stackptr_start, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
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

  /* Encode exit code. */
  p->p_exitcode = _MKWAIT_EXIT(exitcode);

  /* Set the custom exited status to true. */
  p->p_exited = true;

  /* Signal the exited process's cv */
  lock_acquire(p->p_mutex);
  cv_broadcast(p->p_exited_cv, p->p_mutex);
  lock_release(p->p_mutex);

  /* If the process does not have a parent, destroy it.*/
  if (p->p_parent == NULL) {
    proc_destroy(p);
  }
  
  #else
  (void)p_exitcode;
  proc_destroy(p);
  #endif
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys__exit\n");
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

  /* Locate the child by its pid. */
  struct proc *childproc = NULL;
  unsigned int numchildren = array_num(curproc->p_children);
  unsigned int i;
  for (i = 0; i < numchildren; i++) {
    if (pid == ((struct proc*)array_get(curproc->p_children, i))->p_pid) {
      childproc = (struct proc*)array_get(curproc->p_children, i);
      break;
    }
  }

  /* If the child is not found, produce error. */
  if (childproc == NULL) {
    panic("waitpid() called by wrong process");
  }

  /* Wait for the child to exit. */
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

