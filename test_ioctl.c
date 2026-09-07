#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "mychardev_ioctl.h"

int main(void)
{
    int fd = open("/dev/mychardev", O_RDWR);
    int count = 0;

    if (fd < 0) {
        perror("open");
        return 1;
    }

    write(fd, "AAAABBBBCC", 10);

    if (ioctl(fd, MYCHARDEV_GET_COUNT, &count) < 0) {
        perror("ioctl GET_COUNT");
        return 1;
    }
    printf("Buffer holds %d bytes after write\n", count);

    if (ioctl(fd, MYCHARDEV_CLEAR) < 0) {
        perror("ioctl CLEAR");
        return 1;
    }
    printf("Sent CLEAR command\n");

    if (ioctl(fd, MYCHARDEV_GET_COUNT, &count) < 0) {
        perror("ioctl GET_COUNT");
        return 1;
    }
    printf("Buffer holds %d bytes after clear\n", count);

    close(fd);
    return 0;
}
