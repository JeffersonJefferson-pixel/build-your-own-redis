#pragma once

#include "linked_list.h"

const size_t k_max_msg = 4096;

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

void init_server_conn();
void fd_set_nb(int fd);
void connection_io(Conn* conn, void (* req_func)(Conn *), void (* res_func)(Conn *));
void conn_done(Conn *conn);
int32_t accept_new_conn(int fd);