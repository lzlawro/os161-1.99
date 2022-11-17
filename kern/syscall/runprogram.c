/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname, unsigned int nargs, char **args)
{
	/* Copy the program path from user space into kernel */
  char *kprogname = kmalloc(sizeof(char) * (strlen((char *)progname) + 1));
  if (kprogname == NULL) {
    return ENOMEM;
  }

  strcpy(kprogname, (char *)progname);

  /* Why does this not work? */
  // for (int i = 0; i < nargs; i++) {
  //   kprintf("%s ", args[i]);
  // }

  /* Create the new args array in kernel memory. */
  // char **kargs; // Using this initialization causes some weird kernel panic: 
  char *kargs[nargs+1];
  *(kargs + nargs) = NULL;
  for (unsigned int i = 0; i < nargs; i++) {
    *(kargs + i) = kmalloc(sizeof(char) * (strlen((char *)(*((char **)args + i))) + 1));
    strcpy(*(kargs + i), (char *)(*((char **)args + i)));
  }

  // kprintf("copied program name: %s, copied %u arguments: \n", kprogname, nargs);
  // for (unsigned int i = 0; i < nargs; i++) {
  //   kprintf("%s ", *(kargs + i));
  // }
  // kprintf("\n");

	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	KASSERT(curproc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

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
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

	/* Compute the total space required for copying args. */
  unsigned int args_size = 0;
  args_size += (nargs + 1)*4;

  for (unsigned int i = 0; i < nargs; i++) {
    args_size += strlen(*(kargs + i)) + 1;
  }

  args_size = ROUNDUP(args_size, 8);

  /* Save the stack pointer to the top of the stack. */
  vaddr_t stackptr_start = USERSTACK - args_size;

  /* Move the stack pointer to the start of arg strings. */
  stackptr = stackptr_start + (nargs + 1)*4;

  /* Keep track of the addrs of strings while copying
     individual strings. */
  
  for (unsigned int i = 0; i < nargs; i++) {
    memcpy((void *)stackptr, *(kargs + i), strlen(*(kargs + i)) + 1);
    memcpy((void *)(stackptr_start + i*sizeof(vaddr_t)),
           &stackptr, sizeof(vaddr_t));
    stackptr += strlen(*(kargs + i)) + 1;
  }
  /* Test */
  /*
  // ########################################
  for (unsigned char *p = (unsigned char *)stackptr_start;
       p != (unsigned char *)(stackptr_start + (nargs + 1)*4); p++) {
        if ((unsigned int)p % 4 == 0) {
          kprintf("%x:\t", (unsigned int)p);
        }
        if (*p == '\0') {
          kprintf("\\0\t");
        }
        else {
        kprintf("%2x\t", *p);
        }
        if ((unsigned int)p % 4 == 3) {
          kprintf("\n");
        }
       }
  for (unsigned char *p = (unsigned char *)stackptr_start + (nargs + 1)*4;
       p != (unsigned char *)USERSTACK; p++) {
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
  */
  // ########################################

	/* Free the kernel-allocated stuff */
  kfree(kprogname);
  kprogname = NULL;
  for (unsigned int i = 0; i < nargs; i++) {
    kfree(*(kargs + i));
	*(kargs + i) = NULL;
  }

	/* Warp to user mode. */
	// enter_new_process(0, /*argc*/, NULL /*userspace addr of argv*/,
	// 		  stackptr, entrypoint);
	enter_new_process(nargs, (userptr_t)stackptr_start, stackptr_start, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

