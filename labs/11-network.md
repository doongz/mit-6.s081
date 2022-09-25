# Lab11: Network

在本实验室中，您将为网络接口卡（NIC）编写一个xv6设备驱动程序。

获取xv6实验的源代码并切换到`net`分支：

```bash
$ git fetch
$ git checkout net
$ make clean
```

# 背景

> [!TIP]
> 在编写代码之前，您可能会发现阅读xv6手册中的《第5章：中断和设备驱动》很有帮助。

您将使用名为E1000的网络设备来处理网络通信。对于xv6（以及您编写的驱动程序），E1000看起来像是连接到真正以太网局域网（LAN）的真正硬件。事实上，用于与您的驱动程序对话的E1000是qemu提供的模拟，连接到的LAN也由qemu模拟。在这个模拟LAN上，xv6（“来宾”）的IP地址为10.0.2.15。Qemu还安排运行Qemu的计算机出现在IP地址为10.0.2.2的LAN上。当xv6使用E1000将数据包发送到10.0.2.2时，qemu会将数据包发送到运行qemu的（真实）计算机上的相应应用程序（“主机”）。

您将使用QEMU的“用户模式网络栈（user-mode network stack）”。[QEMU的文档](https://www.qemu.org/docs/master/system/net.html#using-the-user-mode-network-stack)中有更多关于用户模式栈的内容。我们已经更新了***Makefile***以启用QEMU的用户模式网络栈和E1000网卡。

***Makefile***将QEMU配置为将所有传入和传出数据包记录到实验目录中的***packets.pcap***文件中。查看这些记录可能有助于确认xv6正在发送和接收您期望的数据包。要显示记录的数据包，请执行以下操作：

```
tcpdump -XXnr packets.pcap
```

我们已将一些文件添加到本实验的xv6存储库中。***kernel/e1000.c***文件包含E1000的初始化代码以及用于发送和接收数据包的空函数，您将填写这些函数。***kernel/e1000_dev.h***包含E1000定义的寄存器和标志位的定义，并在[《英特尔E1000软件开发人员手册》](https://pdos.csail.mit.edu/6.828/2020/readings/8254x_GBe_SDM.pdf)中进行了描述。***kernel/net.c***和***kernel/net.h***包含一个实现IP、UDP和ARP协议的简单网络栈。这些文件还包含用于保存数据包的灵活数据结构（称为`mbuf`）的代码。最后，***kernel/pci.c***包含在xv6引导时在PCI总线上搜索E1000卡的代码。

# 你的工作(hard)

> [!TIP|label:YOUR JOB]
> 您的工作是在***kernel/e1000.c***中完成`e1000_transmit()`和`e1000_recv()`，以便驱动程序可以发送和接收数据包。当`make grade`表示您的解决方案通过了所有测试时，您就完成了。

> [!TIP]
> 在编写代码时，您会发现自己参考了[《E1000软件开发人员手册》](https://pdos.csail.mit.edu/6.828/2020/readings/8254x_GBe_SDM.pdf)。以下部分可能特别有用：
> - Section 2是必不可少的，它概述了整个设备。
> - Section 3.2概述了数据包接收。
> - Section 3.3与Section 3.4一起概述了数据包传输。
> - Section 13概述了E1000使用的寄存器。
> - Section 14可能会帮助您理解我们提供的init代码。

浏览[《E1000软件开发人员手册》](https://pdos.csail.mit.edu/6.828/2020/readings/8254x_GBe_SDM.pdf)。本手册涵盖了几个密切相关的以太网控制器。QEMU模拟82540EM。现在浏览第2章，了解该设备。要编写驱动程序，您需要熟悉第3章和第14章以及第4.1节（虽然不包括4.1的子节）。你还需要参考第13章。其他章节主要介绍你的驱动程序不必与之交互的E1000组件。一开始不要担心细节；只需了解文档的结构，就可以在以后找到内容。E1000具有许多高级功能，其中大部分您可以忽略。完成这个实验只需要一小部分基本功能。

我们在***e1000.c***中提供的`e1000_init()`函数将E1000配置为读取要从RAM传输的数据包，并将接收到的数据包写入RAM。这种技术称为DMA，用于直接内存访问，指的是E1000硬件直接向RAM写入和读取数据包。

由于数据包突发到达的速度可能快于驱动程序处理数据包的速度，因此`e1000_init()`为E1000提供了多个缓冲区，E1000可以将数据包写入其中。E1000要求这些缓冲区由RAM中的“描述符”数组描述；每个描述符在RAM中都包含一个地址，E1000可以在其中写入接收到的数据包。`struct rx_desc`描述描述符格式。描述符数组称为接收环或接收队列。它是一个圆环，在这个意义上，当网卡或驱动程序到达队列的末尾时，它会绕回到数组的开头。`e1000_init()`使用`mbufalloc()`为要进行DMA的E1000分配`mbuf`数据包缓冲区。此外还有一个传输环，驱动程序将需要E1000发送的数据包放入其中。`e1000_init()`将两个环的大小配置为`RX_RING_SIZE`和`TX_RING_SIZE`。

当***net.c***中的网络栈需要发送数据包时，它会调用`e1000_transmit()`，并使用一个保存要发送的数据包的`mbuf`作为参数。传输代码必须在TX（传输）环的描述符中放置指向数据包数据的指针。`struct tx_desc`描述了描述符的格式。您需要确保每个`mbuf`最终被释放，但只能在E1000完成数据包传输之后（E1000在描述符中设置`E1000_TXD_STAT_DD`位以指示此情况）。

当当E1000从以太网接收到每个包时，它首先将包DMA到下一个RX(接收)环描述符指向的`mbuf`，然后产生一个中断。`e1000_recv()`代码必须扫描RX环，并通过调用`net_rx()`将每个新数据包的`mbuf`发送到网络栈（在***net.c***中）。然后，您需要分配一个新的`mbuf`并将其放入描述符中，以便当E1000再次到达RX环中的该点时，它会找到一个新的缓冲区，以便DMA新数据包。

除了在RAM中读取和写入描述符环外，您的驱动程序还需要通过其内存映射控制寄存器与E1000交互，以检测接收到数据包何时可用，并通知E1000驱动程序已经用要发送的数据包填充了一些TX描述符。全局变量`regs`包含指向E1000第一个控制寄存器的指针；您的驱动程序可以通过将`regs`索引为数组来获取其他寄存器。您需要特别使用索引`E1000_RDT`和`E1000_TDT`。

要测试驱动程序，请在一个窗口中运行`make server`，在另一个窗口中运行`make qemu`，然后在xv6中运行`nettests`。`nettests`中的第一个测试尝试将UDP数据包发送到主机操作系统，地址是`make server`运行的程序。如果您还没有完成实验，E1000驱动程序实际上不会发送数据包，也不会发生什么事情。

完成实验后，E1000驱动程序将发送数据包，qemu将其发送到主机，`make server`将看到它并发送响应数据包，然后E1000驱动程序和`nettests`将看到响应数据包。但是，在主机发送应答之前，它会向xv6发送一个“ARP”请求包，以找出其48位以太网地址，并期望xv6以ARP应答进行响应。一旦您完成了对E1000驱动程序的工作，***kernel/net.c***就会处理这个问题。如果一切顺利，`nettests`将打印`testing ping: OK`，`make server`将打印`a message from xv6!`。

`tcpdump -XXnr packets.pcap`应该生成这样的输出:

```
reading from file packets.pcap, link-type EN10MB (Ethernet)
15:27:40.861988 IP 10.0.2.15.2000 > 10.0.2.2.25603: UDP, length 19
        0x0000:  ffff ffff ffff 5254 0012 3456 0800 4500  ......RT..4V..E.
        0x0010:  002f 0000 0000 6411 3eae 0a00 020f 0a00  ./....d.>.......
        0x0020:  0202 07d0 6403 001b 0000 6120 6d65 7373  ....d.....a.mess
        0x0030:  6167 6520 6672 6f6d 2078 7636 21         age.from.xv6!
15:27:40.862370 ARP, Request who-has 10.0.2.15 tell 10.0.2.2, length 28
        0x0000:  ffff ffff ffff 5255 0a00 0202 0806 0001  ......RU........
        0x0010:  0800 0604 0001 5255 0a00 0202 0a00 0202  ......RU........
        0x0020:  0000 0000 0000 0a00 020f                 ..........
15:27:40.862844 ARP, Reply 10.0.2.15 is-at 52:54:00:12:34:56, length 28
        0x0000:  ffff ffff ffff 5254 0012 3456 0806 0001  ......RT..4V....
        0x0010:  0800 0604 0002 5254 0012 3456 0a00 020f  ......RT..4V....
        0x0020:  5255 0a00 0202 0a00 0202                 RU........
15:27:40.863036 IP 10.0.2.2.25603 > 10.0.2.15.2000: UDP, length 17
        0x0000:  5254 0012 3456 5255 0a00 0202 0800 4500  RT..4VRU......E.
        0x0010:  002d 0000 0000 4011 62b0 0a00 0202 0a00  .-....@.b.......
        0x0020:  020f 6403 07d0 0019 3406 7468 6973 2069  ..d.....4.this.i
        0x0030:  7320 7468 6520 686f 7374 21              s.the.host!
```

您的输出看起来会有些不同，但它应该包含字符串“ARP, Request”，“ARP, Reply”，“UDP”，“a.message.from.xv6”和“this.is.the.host”。

`nettests`执行一些其他测试，最终通过（真实的）互联网将DNS请求发送到谷歌的一个名称服务器。您应该确保您的代码通过所有这些测试，然后您应该看到以下输出：

```bash
$ nettests
nettests running on port 25603
testing ping: OK
testing single-process pings: OK
testing multi-process pings: OK
testing DNS
DNS arecord for pdos.csail.mit.edu. is 128.52.129.126
DNS OK
all tests passed.
```

您应该确保`make grade`同意您的解决方案通过。

# 提示

首先，将打印语句添加到`e1000_transmit()`和`e1000_recv()`，然后运行`make server`和（在xv6中）`nettests`。您应该从打印语句中看到，`nettests`生成对`e1000_transmit`的调用。

**实现`e1000_transmit`的一些提示：**

- 首先，通过读取`E1000_TDT`控制寄存器，向E1000询问等待下一个数据包的TX环索引。
- 然后检查环是否溢出。如果`E1000_TXD_STAT_DD`未在`E1000_TDT`索引的描述符中设置，则E1000尚未完成先前相应的传输请求，因此返回错误。
- 否则，使用`mbuffree()`释放从该描述符传输的最后一个`mbuf`（如果有）。
- 然后填写描述符。`m->head`指向内存中数据包的内容，`m->len`是数据包的长度。设置必要的cmd标志（请参阅E1000手册的第3.3节），并保存指向`mbuf`的指针，以便稍后释放。
- 最后，通过将一加到`E1000_TDT再对TX_RING_SIZE`取模来更新环位置。
- 如果`e1000_transmit()`成功地将`mbuf`添加到环中，则返回0。如果失败（例如，没有可用的描述符来传输`mbuf`），则返回-1，以便调用方知道应该释放`mbuf`。

**实现`e1000_recv`的一些提示：**

- 首先通过提取`E1000_RDT`控制寄存器并加一对`RX_RING_SIZE`取模，向E1000询问下一个等待接收数据包（如果有）所在的环索引。
- 然后通过检查描述符`status`部分中的`E1000_RXD_STAT_DD`位来检查新数据包是否可用。如果不可用，请停止。
- 否则，将`mbuf`的`m->len`更新为描述符中报告的长度。使用`net_rx()`将`mbuf`传送到网络栈。
- 然后使用`mbufalloc()`分配一个新的`mbuf`，以替换刚刚给`net_rx()`的`mbuf`。将其数据指针（`m->head`）编程到描述符中。将描述符的状态位清除为零。
- 最后，将`E1000_RDT`寄存器更新为最后处理的环描述符的索引。
- `e1000_init()`使用mbufs初始化RX环，您需要通过浏览代码来了解它是如何做到这一点的。
- 在某刻，曾经到达的数据包总数将超过环大小（16）；确保你的代码可以处理这个问题。

您将需要锁来应对xv6可能从多个进程使用E1000，或者在中断到达时在内核线程中使用E1000的可能性。

---

这个实验呢，虽然说实验指导里列了一大堆参考资料（E1000 的开发者手册），但是其实跟着下面的提示走，不是很难做的。别被它吓到了。

我们先来看一看这个实验要让我们做些什么。指导书里说，当定义在 net.c 中的网络栈想要发送数据包 (packet) 时，会调用 e1000_transmit() 函数。它的参数是指向要发送的数据包的指针；对应到代码中，就是指向 mbuf 结构的指针。这个 transmit 函数，会首先缓存这个指针，之后根据其中的内容，设置描述符中对应的字段。之后，网络栈会通知硬件开始传输这个描述符。数据包可以由一个描述符组成，也可以由多个描述符组成。

接受数据包的过程也类似。类似的描述符会被缓存到“接收环”中，之后 e1000_recv 函数扫描整个接收环，调用 net_rx 函数把新收到的数据包传送给网络栈。

现在我们来看 e1000.c 中的内容。最值得我们关注的是开头定义的四个全局变量：

```c
#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];
```

第一个 tx_ring 数组中存储的是传输时用的描述符；下面定义的 rx_ring 数组同理。而其中的 tx_mbufs 和 rx_mbufs 则是缓存指针用的。

接下来是用来初始化 e1000 用的 e1000_init 函数。这个函数之后是需要我们实现的 e1000_transmit:

```c
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
```

接下来就是 recv 函数了：

```c
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
```

