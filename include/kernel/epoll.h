#ifndef KERNEL_EPOLL_H
#define KERNEL_EPOLL_H

#include <kernel/types.h>

/* Linux-compatible event bits (subset). */
#define EPOLLIN      0x001u
#define EPOLLOUT     0x004u
#define EPOLLERR     0x008u
#define EPOLLHUP     0x010u
#define EPOLLET      0x80000000u /* accepted; currently level-triggered */

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

typedef union epoll_data {
    void    *ptr;
    int      fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;

typedef struct epoll_event {
    uint32_t     events;
    epoll_data_t data;
} epoll_event_t;

void epoll_init(void);

/* Kernel epoll instance ids (not process fds). */
int  epoll_create_inst(int flags);
int  epoll_ctl_inst(int eid, int op, int user_fd, uint32_t events, epoll_data_t data);
int  epoll_wait_inst(int eid, epoll_event_t *events, int maxevents, int timeout_ms);
int  epoll_close_inst(int eid);

/* Socket layer → epoll: sid became readable/writable/err. */
void epoll_sock_notify(int sid);

#endif
