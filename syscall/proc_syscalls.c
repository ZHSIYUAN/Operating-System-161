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

#include "opt-A2.h" // A2a
#include <synch.h>
#include <mips/trapframe.h>
#include <limits.h>
#include <vfs.h>
#include <kern/fcntl.h>

/* this implementation of sys__exit does not do anything with the exit code */
/* this needs to be fixed to get exit() and waitpid() working properly */
void sys__exit(int exitcode) {
    struct addrspace *as;
    struct proc *p = curproc;
    /* for now, just include this to keep the compiler from complaining about
     an unused variable */
    (void)exitcode;
    
#if OPT_A2
    p->exit_code = _MKWAIT_EXIT(exitcode);
    //p->exit_status = true;
    
    if (p->myparent != NULL) { // child exit
        
        V(p->proc_sem);
        
        while (array_num(p->mychild) != 0) {
            struct proc *temp = (struct proc*)array_get(p->mychild, 0);
            V(temp->parentexit_sem);
            array_remove(p->mychild, 0);
        }
        KASSERT(array_num(p->mychild) == 0);
        
        P(p->parentexit_sem);
        
    } else {                  // parent exit
        
        /*
         for (unsigned int i = array_num(p->mychild); i > 0; --i) {
         struct proc *temp = (struct proc*)array_get(p->mychild, i-1);
         V(temp->parentexit_sem);
         array_remove(p->mychild, i-1);
         }
         */
        
        while (array_num(p->mychild) != 0) {
            struct proc *temp = (struct proc*)array_get(p->mychild, 0);
            V(temp->parentexit_sem);
            array_remove(p->mychild, 0);
        }
        KASSERT(array_num(p->mychild) == 0);
        
        reset_pid(); // reset currentpid
    }
#endif /* OPT_A2 */
    
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
    proc_destroy(p);
    
    thread_exit();
    /* thread_exit() does not return, so we should never get here */
    panic("return from thread_exit in sys_exit\n");
}
/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
#if OPT_A2
    *retval = curproc->ID;
    return(0);
#else
    /* for now, this is just a stub that always returns a PID of 1 */
    /* you need to fix this to make it work properly */
    *retval = 1;
    return(0);
#endif /* OPT_A2 */
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
    
#if OPT_A2
    struct proc *temp = find_child_proc(curproc, pid); //find child which has pid
    if (temp == NULL) return ECHILD;
    
    P(temp->proc_sem); // check if child exit
    
    exitstatus = temp->exit_code;
    
#else
    exitstatus = 0;
#endif /* OPT_A2 */
    
    result = copyout((void *)&exitstatus,status,sizeof(int));
    if (result) {
        return(result);
    }
    *retval = pid;
    return(0);
}

#if OPT_A2
int sys_fork(struct trapframe *tf, pid_t *returnvalue) {
    struct proc *child = proc_create_runprogram("mychild");//new child
    if (child == NULL) { // build failed
        return ENPROC;
    }
    child->myparent = curproc;
    
    int err;
    err = as_copy(curproc_getas(), &(child->p_addrspace)); //copy addressspace
    if (err == 1) { //failed copy address space
        proc_destroy(child);
        return err;
    }
    
    struct trapframe *mytf = kmalloc(sizeof(struct trapframe));
    if (mytf == NULL) {
        return ENOMEM;
    }
    memcpy(mytf, tf, sizeof(struct trapframe));
    err = thread_fork("newthread", child, (void *)&enter_forked_process, mytf, 0);
    if (err == 1) {
        return err;
    }
    
    array_add(curproc->mychild, child, NULL); //add child to parent
    
    *returnvalue = child->ID; // to child
    return 0; // to parent
}
#endif /* OPT_A2 */

#if OPT_A2
int sys_execv(userptr_t progname, userptr_t args) {
    (void)args; // 以后删
    // parameter same as runprogram
    struct addrspace *as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result;
    // stores old addrspace, destroy it after
    struct addrspace *originas = curproc_getas();
    // count number of arguments and copy them into kernel
    char ** myargs = kmalloc(sizeof(char**)); // build a new array of strings
    result = copyin((const_userptr_t)args, myargs, sizeof(char**));//最后删掉
    if (result) {
        return result;
    }
    int path_num = 0;
    size_t path_length;
    char **wrap_args = (char **)args;
    for (; wrap_args[path_num] != NULL; ++path_num) {
        myargs[path_num] = kmalloc(sizeof(char) * PATH_MAX);
        result = copyinstr((const_userptr_t)wrap_args[path_num], (char*)myargs[path_num], PATH_MAX, &path_length);
        if (result) {
            //需要改
            return result;
        }
    }
    myargs[path_num] = NULL;
    int max_path_num = ARG_MAX / PATH_MAX;
    if (path_num > max_path_num) {
        return E2BIG;
    }
    // copy program path into the kernel
    char *myprogname = kmalloc(sizeof(char) * PATH_MAX);
    size_t length;
    result = copyinstr(progname, myprogname, PATH_MAX, &length);
    if (result) {
        //加东西
        return result;
    }
    // Open the file.
    result = vfs_open(myprogname, O_RDONLY, 0, &v);
    if (result) {
        return result;
    }
    // Create new addrspace for new process
    as = as_create();
    if (as ==NULL) {
        vfs_close(v);
        return ENOMEM;
    }
    // Switch to it and activate it.
    curproc_setas(as);
    as_activate();
    // Load the executable.
    result = load_elf(v, &entrypoint);
    if (result) {
        // p_addrspace will go away when curproc is destroyed
        vfs_close(v);
        return result;
    }
    // Done with the file now.
    vfs_close(v);
    
    
    /* Define the user stack in the address space
    then copy arguments into new address space*/
    result = as_define_stack(as, &stackptr);
    if (result) {
        // p_addrspace will go away when curproc is destroyed
        return result;
    }
    size_t mylength;
    for (int i = path_num - 1; i >= 0; --i) {
        size_t mypath_length = strlen(myargs[i]) + 1; // include NULL terminator
        mypath_length = ROUNDUP(mypath_length, 8);
        stackptr -= mypath_length;
        result = copyoutstr((const char*)myargs[i], (userptr_t)stackptr, PATH_MAX, &mylength);
        if (result) {
            // 加东西
            return result;
        }
        kfree(myargs[i]); // since we dont need them after 这个可能出错
        myargs[i] = (char*)stackptr;
    }
    stackptr -= sizeof(char*) * (path_num + 1); // 为myargs空下位置
    result = copyout(myargs, (userptr_t)stackptr, sizeof(char*) * (path_num + 1));
    if (result) {
        //加东西
        return result;
    }
    
    // destroy old addrspace, and other things that dont needed
    as_destroy(originas);
    kfree(myprogname);
    kfree(myargs);
    // Warp to user mode. 需要改
    enter_new_process(path_num /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/,
                      stackptr, entrypoint);
    
    //enter_new_process does not return.
    panic("enter_new_process returned\n");
    return EINVAL;
}
#endif /* OPT_A2 */
