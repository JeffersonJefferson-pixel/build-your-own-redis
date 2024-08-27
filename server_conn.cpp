#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <time.h>
#include <poll.h>

#include "common.h"
#include "server_conn.h"
#include "linked_list.h"

static struct {
  // a map of all client connections, keyed by fd
  std::vector<Conn *> fd2conn;
  // timeers for idle connections
  DList idle_list;
} g_data;

void fd_set_nb(int fd) {
  errno = 0;
  int flags = fcntl(fd, F_GETFL, 0);
  if (errno) {
    die("fcntl error");
    return;
  }

  flags |= O_NONBLOCK;

  errno = 0;
  (void)fcntl(fd, F_SETFL, flags);
  if (errno) {
    die("fcntl error");
  }
}

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
  if (fd2conn.size() <= (size_t)conn->fd) {
    fd2conn.resize(conn->fd + 1);
  }
  fd2conn[conn->fd] = conn;
}

int32_t accept_new_conn(int fd) {
  // accept
  struct sockaddr_in client_addr = {};
  socklen_t socklen  = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    msg("accept() error");
    return -1;
  }

  // set the new connection fd to nonblocking mode
  fd_set_nb(connfd);

  // create Conn
  struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
  if (!conn) {
    close(connfd);
    return -1;
  }
  conn->fd = connfd;
  conn->state = STATE_REQ;
  conn->rbuf_size = 0;
  conn->wbuf_size = 0;
  conn->wbuf_sent = 0;
  dlist_insert_before(&g_data.idle_list, &conn->idle_list);
  conn_put(g_data.fd2conn, conn);
  return 0;
}

// get monotonic time in microsecond
static uint64_t get_monotonic_usec() {
  timespec tv = {0, 0};
  clock_gettime(CLOCK_MONOTONIC, &tv);
  return uint64_t(tv.tv_sec) * 1000000 + tv.tv_nsec / 1000;
}

void connection_io(Conn* conn, void (* req_func)(Conn *), void (* res_func)(Conn *)) {
  // update idle timer
  // by moving conn to the end of the list.
  conn->idle_start = get_monotonic_usec();
  dlist_detach(&conn->idle_list);
  dlist_insert_before(&g_data.idle_list, &conn->idle_list);

  if (conn->state == STATE_REQ) {
    req_func(conn);
  } else if (conn->state == STATE_RES) {
    res_func(conn);
  } else {
    assert(0);
  }
}

const uint64_t k_idle_timeout_ms = 5 * 1000;

// takes the nearest timer from the list and use it to calculate the timeout value of poll.
uint32_t next_timer_ms() {
  if (dlist_empty(&g_data.idle_list)) {
    return 10000;
  }

  uint64_t now_us = get_monotonic_usec();
  Conn *next = container_of(g_data.idle_list.next, Conn, idle_list);
  uint64_t next_us = next->idle_start + k_idle_timeout_ms * 1000;
  if (next_us <= now_us) {
    return 0;
  }

  return (uint32_t)((next_us - now_us) / 1000);
}

void conn_done(Conn *conn) {
  g_data.fd2conn[conn->fd] = NULL;
  (void)close(conn->fd);
  dlist_detach(&conn->idle_list);
  free(conn);
}

void process_timers() {
  uint64_t now_us = get_monotonic_usec();
  while (!dlist_empty(&g_data.idle_list)) {
    Conn *next = container_of(g_data.idle_list.next, Conn, idle_list);
    uint64_t next_us = next->idle_start + k_idle_timeout_ms * 1000;
    if (next_us >= now_us + 1000) {
      break;
    }

    printf("removing idle connection: %d\n", next->fd);
    conn_done(next);
  }
}

void init_server_conn() {
  dlist_init(&g_data.idle_list);
}

void run_event_loop(int fd,  void (* req_func)(Conn *), void (* res_func)(Conn *)) {
  std::vector<struct pollfd> poll_args;
  while (true) {
    poll_args.clear();
    struct pollfd pfd = {fd, POLLIN, 0};
    poll_args.push_back(pfd);
    // connection fds
    for (Conn *conn : g_data.fd2conn) {
      if (!conn) {
        continue;
      }
      struct pollfd pfd = {};
      pfd.fd = conn->fd;
      pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
      pfd.events = pfd.events | POLLERR;
      poll_args.push_back(pfd);
    }

    // poll for active fds
    int timeout_ms = (int)next_timer_ms();
    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), timeout_ms);
    if (rv < 0) {
      die("poll");
    }

    // process active connections
    for (size_t i = 1; i < poll_args.size(); ++i) {
      if (poll_args[i].revents) {
        Conn *conn = g_data.fd2conn[poll_args[i].fd];
        connection_io(conn, req_func, res_func);
        if (conn->state == STATE_END) {
          // client closed
          // destroy connection
          conn_done(conn);
        }
      }
    }

    // handle timers
    process_timers();

    // try to accept a new connection
    if (poll_args[0].revents) {
      (void)accept_new_conn(fd);
    }
  }
}