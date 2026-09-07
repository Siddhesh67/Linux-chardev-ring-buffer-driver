#ifndef MYCHARDEV_IOCTL_H
#define MYCHARDEV_IOCTL_H

#include <linux/ioctl.h>

#define MYCHARDEV_MAGIC 'k'

#define MYCHARDEV_GET_COUNT _IOR(MYCHARDEV_MAGIC, 1, int)
#define MYCHARDEV_CLEAR     _IO(MYCHARDEV_MAGIC, 2)

#endif
