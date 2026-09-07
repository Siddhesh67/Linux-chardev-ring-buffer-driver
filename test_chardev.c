#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define DEVICE_PATH "/dev/mychardev"
#define BUF_SIZE 256

int main(void)
{
    int fd;
    char write_buf[BUF_SIZE];
    char read_buf[BUF_SIZE];
    ssize_t ret;

    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", DEVICE_PATH, strerror(errno));
        return EXIT_FAILURE;
    }
    printf("Opened %s successfully (fd = %d)\n", DEVICE_PATH, fd);

    snprintf(write_buf, BUF_SIZE, "Message from userspace C program");
    ret = write(fd, write_buf, strlen(write_buf));
    if (ret < 0) {
        fprintf(stderr, "Write failed: %s\n", strerror(errno));
        close(fd);
        return EXIT_FAILURE;
    }
    printf("Wrote %zd bytes: \"%s\"\n", ret, write_buf);

    close(fd);

    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to reopen %s: %s\n", DEVICE_PATH, strerror(errno));
        return EXIT_FAILURE;
    }

    memset(read_buf, 0, BUF_SIZE);
    ret = read(fd, read_buf, BUF_SIZE);
    if (ret < 0) {
        fprintf(stderr, "Read failed: %s\n", strerror(errno));
        close(fd);
        return EXIT_FAILURE;
    }
    printf("Read %zd bytes: \"%s\"\n", ret, read_buf);

    ret = read(fd, read_buf, BUF_SIZE);
    printf("Second read returned %zd (should be 0 for EOF)\n", ret);

    close(fd);
    printf("Closed device.\n");

    return EXIT_SUCCESS;
}
