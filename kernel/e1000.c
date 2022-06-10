#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //

  // 先加锁；这个 transmit 函数可以同时被不同的进程调用。
  acquire(&e1000_lock);

  // e1000 的各个寄存器中的值是映射到 xv6 系统的内存中的。
  // regs 是它们的基址。
  // E1000_TDT 会被替换为从那个基址算起的偏移量，
  // 它对应的寄存器中存储的是现在要发送的数据包，
  // 在“传输环”中对应的偏移量。
  uint64 offset = regs[E1000_TDT];

  // 检查 offset 是不是太大
  if (offset > TX_RING_SIZE)
    return -1;

  // 这个是要传输的描述符
  struct tx_desc* nextpacket = tx_ring + offset;

  // status 字段存储当前描述符的状态。
  // 如果没有设置E1000_TXD_STAT_DD 这个标志位，
  // 就说明缓冲区中的描述符还没有传输完。
  if (!(nextpacket->status & E1000_TXD_STAT_DD))
    return -1;

  if (tx_mbufs[offset])
    mbuffree(tx_mbufs[offset]);

  // 下面是设置传输描述符的代码。
  nextpacket->addr = (uint64)m->head;
  nextpacket->length = m->len;

  // cmd 字段怎么设置？可以参考实验指导中 E1000 手册的 3.3.3.1 节。
  // 虽然理论上应该把那个表格里的内容全读一遍，但是，
  // 毕竟 e1000_dev.h 中只给出了下面那两个字段……
  // 所以偷懒一点，直接组合这两个字段就行了。
  // EOP 的意思是这个数据包里只包含现在传输的这一个描述符。
  // RS 的意思是，让硬件在传输完成后设置 STAT_DD 标识符。
  nextpacket->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;

  // 缓存 m 指针，这样下次调用 e1000_transmit 函数时，
  // 就可以释放那个 mbuf，不至于内存泄漏。
  tx_mbufs[offset] = m;

  // 把偏移量设置为下一个要传输的描述符
  regs[E1000_TDT] = (offset + 1) % TX_RING_SIZE;
  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //

  // 需要注意的点是，这个函数不需要加锁。
  // 它处于 OSI 中的数据链路层，而物理层之上、传输层之下的各层，是不考虑并发的。
  // 参考自 https://www.cnblogs.com/YuanZiming/p/14271553.html。

  // 无限循环——只要有收到的描述符，就把它往网络栈传
  // 直到看不到新的描述符为止
  while (1) {
    // 和上面一样，也是先获取偏移量；
    // 但是在上面那个函数中，
    // 寄存器 RDT 里的值直接就是要发送的描述符的偏移量；
    // 而这里需要先加一才能获取正确的偏移量。
    uint64 offset = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
    struct rx_desc* rxpacket = rx_ring + offset;
    if (!(rxpacket->status & E1000_RXD_STAT_DD))
      break;

    // 把输入缓冲区作为参数传递给 net_rx, 
    // 让它去把接收到的数据放到缓冲区中
    rx_mbufs[offset]->len = rxpacket->length;
    net_rx(rx_mbufs[offset]);

    // 这时，刚才那个缓冲区的使命已经完成了，
    // 可以生成一个新的了
    rx_mbufs[offset] = mbufalloc(0);
    rxpacket->addr = (uint64)rx_mbufs[offset]->head;
    rxpacket->status = 0;
    regs[E1000_RDT] = offset;
  }
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
