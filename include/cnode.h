#ifndef C_NODE_H
#define C_NODE_H

#include <memory>
#include <unordered_map>
#include <functional>
#include <string.h>
#include <assert.h>

#include "erl_interface.h"
#include "ei.h"

#include "v8runner.h"
#include "threadpool.h"

typedef std::shared_ptr<ETERM> ETERMptr;
typedef std::shared_ptr<char> CharPtr;

template<typename T>
std::shared_ptr<T> make_shared_array(const std::size_t& size) {
   return std::shared_ptr<T>(new T[size], std::default_delete<T[]>());
}

auto ErlMessageDeleter = [](ErlMessage* emsg) {
  erl_free_term(emsg->from);
  erl_free_term(emsg->msg);
  delete emsg;
};

auto ErlFreeTerm = [](ETERM* term) {
  erl_free_term(term);
};

auto ErlFree = [](void* obj) {
  free(obj);
};

typedef pb::concurrent::ThreadPool<
  std::function<void(int)>
> ThreadPool;

class CNode {
public:
  enum STATUS {
    OK = 0,
    ERR = 1,
    SOCKET_TIMEOUT = 100,
    THREAD_POOL_TIMEOUT = 101,
    THREAD_POOL_EXHAUSTED = 102
  };

public:
  CNode(
    const std::shared_ptr<pb::V8Runner>& v8,
    const std::size_t& maxDiffTime,
    ThreadPool& pool
  );

  void process(int fd, ErlMessage*& emsg);
  void processV8(
    int fd,
    ETERMptr fromp,
    ETERMptr tuplep,
    ETERMptr func,
    std::chrono::milliseconds timeWhenTaskHasBeenAdded,
    int threadNum
  );

private:
  std::shared_ptr<pb::V8Runner> _v8;
  std::size_t _maxDiffTime;
  ThreadPool& _pool;

  std::unordered_map<std::string, int> _priorityMap {
    {"check_code", 0},
    {"run", 0},
    {"compile", 1},
    {"remove", 1},
  };
};

#endif
