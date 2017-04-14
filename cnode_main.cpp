#include <stdio.h>
#include <string.h>

#include "cnode.h"

#define BUFSIZE 10000

int my_listen(int);

void freeErlMessage(ErlMessage*& msg);

int main(int argc, char **argv) {

  fs::path pathToLibs(argv[1]); // absolute path here

  const std::size_t maxExecutionTime = 1000; // milliseconds
  const std::size_t timeCheckerSleepTime = 500; // milliseconds
  const std::size_t maxRAMAvailable = std::stoi(argv[2]);
  const std::size_t threadsCount = 4;

  auto v8 = std::make_shared<pb::V8Runner>(
    argc,
    argv,
    pathToLibs,
    maxExecutionTime,
    maxRAMAvailable,
    timeCheckerSleepTime,
    threadsCount
  );

  const std::size_t maxDiffTime = 1000000; // milliseconds

  auto cnode = std::make_shared<CNode>(v8, maxDiffTime, threadsCount);

  struct in_addr addr;                     /* 32-bit IP number of host */
  int port;                                /* Listen port number */
  int listen;                              /* Listen socket */
  int fd;                                  /* fd to Erlang node */

  int loop = 1;                            /* Loop flag */
  unsigned char buf[BUFSIZE];              /* Buffer for incoming message */

  auto id = atoi(argv[3]);
  auto parent_node_name = argv[4];
  auto cookie = argv[5];

  erl_init(NULL, 0);

  if (erl_connect_init(id, cookie, 0) == -1)
    erl_err_quit("erl_connect_init");

  if ((fd = erl_connect(parent_node_name)) < 0)
    erl_err_quit("Could not connect to node");

  while (loop) {

    ErlMessage* emsg = new ErlMessage();
    int got = erl_receive_msg(fd, buf, BUFSIZE, emsg);

    if (got == ERL_ERROR) {
      loop = 0;
    } else if (got == ERL_MSG) {
      if (emsg->type == ERL_REG_SEND) {
        cnode->process(fd, emsg);
      }
    }

    freeErlMessage(emsg);

  }

}

int my_listen(int port) {
  int listen_fd;
  struct sockaddr_in addr;
  int on = 1;

  if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    return (-1);

  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  memset((void*) &addr, 0, (size_t) sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(listen_fd, (struct sockaddr*) &addr, sizeof(addr)) < 0)
    return (-1);

  listen(listen_fd, 5);
  return listen_fd;
}

void freeErlMessage(ErlMessage*& msg) {
  erl_free_term(msg->from);
  erl_free_term(msg->msg);
  delete msg;
  msg = nullptr;
}
