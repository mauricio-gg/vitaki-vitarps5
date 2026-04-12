/*
 * Vita stub for <sys/ioctl.h>
 * Provides ioctl() declaration and SIOCGIFADDR needed by miniupnpc.
 * The ioctl(SIOCGIFADDR) call in minissdpc.c is only reached when a
 * non-NULL interface name is passed to upnpDiscover(); we always pass NULL.
 */
#ifndef VITA_STUB_SYS_IOCTL_H
#define VITA_STUB_SYS_IOCTL_H

#define SIOCGIFADDR 0x8915

static inline int ioctl(int fd, unsigned long request, ...)
{
    (void)fd;
    (void)request;
    return -1;
}

#endif /* VITA_STUB_SYS_IOCTL_H */
