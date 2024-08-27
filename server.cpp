#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <errno.h>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include "common.h"
#include "hashtable.h"
#include "zset.h"
#include "linked_list.h"
#include "server_out.h"
#include "server_data.h"

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

const size_t k_max_msg = 4096;

static void fd_set_nb(int fd) {
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

enum {
  STATE_REQ = 0,
  STATE_RES = 1,
  STATE_END = 2,
};

struct Conn {
  int fd = -1;
  uint32_t state = 0;
  // buffer for reading
  size_t rbuf_size = 0;
  uint8_t rbuf[4 + k_max_msg];
  // buffer for writing
  size_t wbuf_size = 0;
  size_t wbuf_sent = 0;
  uint8_t wbuf[4 + k_max_msg];
  uint64_t idle_start = 0;
  // timer 
  DList idle_list;
};

static struct {
  // a map of all client connections, keyed by fd
  std::vector<Conn *> fd2conn;
  // timeers for idle connections
  DList idle_list;
} g_data;

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
  if (fd2conn.size() <= (size_t)conn->fd) {
    fd2conn.resize(conn->fd + 1);
  }
  fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(int fd) {
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

static bool try_flush_buffer(Conn *conn) {
  ssize_t rv = 0;
  do {
    size_t remain = conn->wbuf_size - conn->wbuf_sent;
    rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
  } while (rv < 0 && errno == EINTR);

  if (rv < 0 && errno == EAGAIN) {
    return false;
  }
  
  if (rv < 0) {
    msg("write() error");
    conn->state = STATE_END;
    return false;
  }

  conn->wbuf_sent += (size_t)rv;
  assert(conn->wbuf_sent <= conn->wbuf_size);
  if (conn->wbuf_sent == conn->wbuf_size) {
    // response was fully sent
    conn->state = STATE_REQ;
    conn->wbuf_sent = 0;
    conn->wbuf_size = 0;
    return false;
  }
  // still go tsome data in wbuf
  return true;
}

static void state_res(Conn *conn) {
  while (try_flush_buffer(conn)) {}
}

const size_t k_max_args = 1024;

static int32_t parse_req(
  const uint8_t *data, size_t len, std::vector<std::string> &out) 
{
  if (len < 4) {
    return -1;
  }
  uint32_t n = 0;
  memcpy(&n, &data[0], 4);
  if (n > k_max_args) {
    return -1;
  }

  size_t pos = 4;
  while (n--) {
    if (pos + 4 > len) {
      return -1;
    }
    uint32_t sz = 0;
    memcpy(&sz, &data[pos], 4);
    if (pos + 4 + sz > len) {
      return -1;
    }
    out.push_back(std::string((char *)&data[pos + 4], sz));
    pos += 4 + sz;
  }

  if (pos != len) {
    return -1;  // trailing garbage
  }
  return 0;
}

enum {
  RES_OK = 0,
  RES_ERR = 1,
  RES_NX = 2,
};



static bool cmd_is(const std::string &word, const char *cmd) {
  return 0 == strcasecmp(word.c_str(), cmd);
}

static void do_request(std::vector<std::string> cmd, std::string &out) {
  if (cmd.size() == 1 && cmd_is(cmd[0], "keys")) {
    do_keys(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
    do_get(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")) {
    do_set(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "del")) {
    do_del(cmd, out);
  } else if (cmd.size() == 4 && cmd_is(cmd[0], "zadd")) {
    do_zadd(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "zrem")) {
    do_zrem(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "zscore")) {
    do_zscore(cmd, out);
  } else if (cmd.size() == 6 && cmd_is(cmd[0], "zquery")) {
    do_zquery(cmd, out);
  } else {
    // cmd is not recognized
    out_err(out, ERR_UNKNOWN, "Unknown cmd");
  }
}

static bool try_one_request(Conn *conn) {
  // try to parse a request from the buffer
  if (conn->rbuf_size < 4) {
    // not enough data in the buffer. Will retry in the next iteration
    return false;
  }
  uint32_t len = 0;
  memcpy(&len, &conn->rbuf[0], 4);
  if (len > k_max_msg) {
    msg("too long");
    conn->state = STATE_END;
    return false;
  }
  if (4 + len > conn->rbuf_size) {
    // not enough data in the buffer. Will retry in the next iteration
    return false;
  }

  // parse the request
  std::vector<std::string> cmd;
  if (0 != parse_req(&conn->rbuf[4], len, cmd)) {
    msg("bad req");
    conn->state = STATE_END;
    return false;
  }

  // got one request, generate the response.
  std::string out;
  do_request(cmd, out);

  // pack the response into the buffer
  if (4 + out.size() > k_max_msg) {
    out.clear();
    out_err(out, ERR_2BIG, "response is too big");
  }
  uint32_t wlen = (uint32_t)out.size();
  memcpy(&conn->wbuf[0], &wlen, 4);
  memcpy(&conn->wbuf[4], out.data(), out.size());
  conn->wbuf_size = 4 + wlen;

  // remove the request from the buffer.
  // note: frequent memmove is inefficient.
  // note: need better handling for production code.
  size_t remain = conn->rbuf_size - 4 - len;
  if (remain) {
    memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
  }
  conn->rbuf_size = remain;

  // change state
  conn->state = STATE_RES;
  state_res(conn);

  // continue the outer loop if the request was fully processed
  return (conn->state == STATE_REQ);
}


static bool try_fill_buffer(Conn *conn) {
  // try to fill the buffer
  assert(conn->rbuf_size < sizeof(conn->rbuf));
  ssize_t rv = 0;
  do {
    size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
    rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
  } while (rv <0 && errno == EINTR );
  if (rv < 0 && errno == EAGAIN) {
    return false;
  }
  if (rv < 0) {
    msg("read() error");
    conn->state = STATE_END;
    return false;
  }
  if (rv == 0) {
    if (conn->rbuf_size > 0) {
      msg("unexpected EOF");
    } else {
      msg("EOF");
    }
    conn->state = STATE_END;
    return false; 
  }

  conn->rbuf_size += (size_t)rv;
  assert(conn->rbuf_size <= sizeof(conn -> rbuf));

  // try to process requests one by one
  while (try_one_request(conn)) {}
  return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn) {
  while (try_fill_buffer(conn)) {}
}

// get monotonic time in microsecond
static uint64_t get_monotonic_usec() {
  timespec tv = {0, 0};
  clock_gettime(CLOCK_MONOTONIC, &tv);
  return uint64_t(tv.tv_sec) * 1000000 + tv.tv_nsec / 1000;
}

static void connection_io(Conn* conn) {
  // update idle timer
  // by moving conn to the end of the list.
  conn->idle_start = get_monotonic_usec();
  dlist_detach(&conn->idle_list);
  dlist_insert_before(&g_data.idle_list, &conn->idle_list);

  if (conn->state == STATE_REQ) {
    state_req(conn);
  } else if (conn->state == STATE_RES) {
    state_res(conn);
  } else {
    assert(0);
  }
}

const uint64_t k_idle_timeout_ms = 5 * 1000;

// takes the nearest timer from the list and use it to calculate the timeout value of poll.
static uint32_t next_timer_ms() {
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

static void conn_done(Conn *conn) {
  g_data.fd2conn[conn->fd] = NULL;
  (void)close(conn->fd);
  dlist_detach(&conn->idle_list);
  free(conn);
}

static void process_timers() {
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

int main() {
  // some initializaation
  dlist_init(&g_data.idle_list);

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    die("socket()");
  }

  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  // bind
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(0);

  int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));

  if (rv) {
    die("bind()");
  }

  // listen
  rv = listen(fd, SOMAXCONN);
  if (rv) {
    die("listen()");
  }

  // set the listen fd to nonblocking mode
  fd_set_nb(fd);

  // event loop
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
    int timeout_ms  = (int)next_timer_ms();
    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), timeout_ms);
    if (rv < 0) {
      die("poll");
    }

    // process active connections
    for (size_t i = 1; i < poll_args.size(); ++i) {
      if (poll_args[i].revents) {
        Conn *conn = g_data.fd2conn[poll_args[i].fd];
        connection_io(conn);
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

  return 0;
}