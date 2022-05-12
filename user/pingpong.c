#include "kernel/types.h"
#include "user/user.h"

#define RD 0  // pipe的read端
#define WR 1  // pipe的write端

int main() {
    char buf = 'P';  // 用于传输的字节

    int fd_p2c[2];  // 父进程 -> 子进程
    int fd_c2p[2];  // 子进程 -> 父进程
    pipe(fd_p2c);
    pipe(fd_c2p);

    int pid = fork();
    int exit_status = 0;

    if (pid < 0) {
        fprintf(2, "fork() error!\n");
        close(fd_p2c[RD]);
        close(fd_p2c[WR]);
        close(fd_c2p[RD]);
        close(fd_c2p[WR]);
        exit(1);
    } else if (pid > 0) {
        close(fd_p2c[RD]);
        close(fd_c2p[WR]);

        if (write(fd_p2c[WR], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "parent write() error!\n");
            exit_status = 1;
        }

        if (read(fd_c2p[RD], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "parent write() error!\n");
            exit_status = 1;
        } else {
            fprintf(1, "%d: received pong\n", getpid());
        }
        close(fd_p2c[WR]);
        close(fd_c2p[RD]);
        exit(exit_status);
    } else if (pid == 0) {  // child
        close(fd_p2c[WR]);
        close(fd_c2p[RD]);

        if (read(fd_p2c[RD], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "child read() error!\n");
            exit_status = 1;
        } else {
            fprintf(1, "%d: received ping\n", getpid());
        }

        if (write(fd_c2p[WR], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "child write() error!\n");
            exit_status = 1;
        }

        close(fd_p2c[RD]);
        close(fd_c2p[WR]);
        exit(exit_status);
    }
    return 0;
}
