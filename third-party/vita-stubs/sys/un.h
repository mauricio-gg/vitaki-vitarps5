/*
 * Vita stub for <sys/un.h>
 * Provides struct sockaddr_un needed by miniupnpc's minissdpc.c for
 * MiniSSDPd daemon connection. This code path is never reached on Vita
 * (no MiniSSDPd daemon exists), so only the type definition is needed.
 */
#ifndef VITA_STUB_SYS_UN_H
#define VITA_STUB_SYS_UN_H

struct sockaddr_un {
    unsigned short sun_family;
    char           sun_path[108];
};

#endif /* VITA_STUB_SYS_UN_H */
