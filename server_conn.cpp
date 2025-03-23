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
#include "server_common.h"

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

void connection_io(Conn* conn, void (* req_func)(Conn *), void (* res_func)(Conn *)) {
  // update idle timer
  // by moving conn to the end of the list.
  conn->idle_start = get_monotonic_msec();
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

void conn_done(Conn *conn) {
  (void)close(conn->fd);
  g_data.fd2conn[conn->fd] = NULL;
  dlist_detach(&conn->idle_list);
  free(conn);
}

void init_server_conn() {
  dlist_init(&g_data.idle_list);
}