#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>

int main(void)
{
    pid_t pid = fork();

    if (pid == 0) {
        int fd = open("/dev/mychardev", O_RDONLY);
        char buf[256] = {0};
        printf("[child] opening device and calling read() now, should block...\n");
        fflush(stdout);
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        printf("[child] read() returned! Got %zd bytes: \"%s\"\n", n, buf);
        close(fd);
        _exit(0);
    } else {
        sleep(2);
        printf("[parent] writing to device now...\n");
        fflush(stdout);
        int fd = open("/dev/mychardev", O_WRONLY);
        write(fd, "unblocked!", 10);
        close(fd);
        wait(NULL);
        printf("[parent] child has exited, test complete.\n");
    }

    return 0;
}
