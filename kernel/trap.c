#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fcntl.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"


struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

/**
 * @brief mmap_handler 处理mmap惰性分配导致的页面错误
 * @param va 页面故障虚拟地址
 * @param cause 页面故障原因
 * @return 0成功，-1失败
 */
int mmap_handler(int va, int cause) {
  struct proc *p = myproc();
  // 根据地址查找属于哪一个VMA
  int i;
  for (i = 0; i < NVMA; i++) {
      if (p->vma[i].used && p->vma[i].addr <= va &&
          va <= p->vma[i].addr + p->vma[i].len - 1) {
          break;
      }
  }
  if (i == NVMA) return -1;

  // 配置上权限
  int pte_flags = PTE_U;
  if (p->vma[i].prot && PROT_READ) pte_flags |= PTE_R;
  if (p->vma[i].prot && PROT_WRITE) pte_flags |= PTE_W;
  if (p->vma[i].prot && PROT_EXEC) pte_flags |= PTE_X;

  struct file* vf = p->vma[i].vfile;
  // 读导致的页面错误
  if (cause == 13 && vf->readable == 0) return -1;
  // 写导致的页面错误
  if (cause == 15 && vf->writable == 0) return -1;

  void* pa = kalloc();
  if (pa == 0) return -1;
  memset(pa, 0, PGSIZE);

  // 读取文件内容
  ilock(vf->ip);
  // 计算当前页面读取文件的偏移量，实验中p->vma[i].offset总是0
  // 要按顺序读读取，例如内存页面A,B和文件块a,b
  // 则A读取a，B读取b，而不能A读取b，B读取a
  int offset = p->vma[i].offset + PGROUNDDOWN(va - p->vma[i].addr);
  int readbytes = readi(vf->ip, 0, (uint64)pa, offset, PGSIZE);
  // 什么都没有读到
  if(readbytes == 0) {
    iunlock(vf->ip);
    kfree(pa);
    return -1;
  }
  iunlock(vf->ip);

  // 添加页面映射
  if(mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)pa, pte_flags) != 0) {
    kfree(pa);
    return -1;
  }
  
  return 0;
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  int cause = r_scause();
  if(cause == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else if (cause == 13 || cause == 15) {
#ifdef LAB_MMAP
  // 读取产生页面故障的虚拟地址，并判断是否位于有效区间
  uint64 fault_va = r_stval();
  if(PGROUNDUP(p->trapframe->sp) - 1 < fault_va && fault_va < p->sz) {
    if(mmap_handler(r_stval(), cause) != 0) p->killed = 1;
  } else
    p->killed = 1;
#endif
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

