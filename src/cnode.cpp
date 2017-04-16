#include "cnode.h"

CNode::CNode(
  const std::shared_ptr<pb::V8Runner>& v8,
  const std::size_t& maxDiffTime,
  const std::size_t& threadsCount
):_v8(v8), _maxDiffTime(maxDiffTime), _pool(threadsCount) {}

void CNode::process(int fd, ErlMessage*& emsg) {

  ETERMptr fromp(erl_element(2, emsg->msg), ErlFreeTerm);
  ETERMptr tuplep(erl_element(3, emsg->msg), ErlFreeTerm);

  ETERMptr timestampTerm(erl_element(1, tuplep.get()), ErlFreeTerm);
  ETERMptr func(erl_element(2, tuplep.get()), ErlFreeTerm);

  const auto timestamp = ERL_LL_UVALUE(timestampTerm);

  const auto now = std::chrono::system_clock::now();
  const auto timeNow =
    std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

  std::size_t timeDiff = abs(static_cast<long long>(timeNow.count()) - static_cast<long long>(timestamp));

  if (timeDiff > this->_maxDiffTime) {
    auto resp = ETERMptr(erl_format("{cnode, ~i, ~b}", CNode::STATUS::SOCKET_TIMEOUT, "Socket queue timeout."), ErlFreeTerm);
    erl_send(fd, fromp.get(), resp.get());
    return;
  }

  if (strcmp(ERL_ATOM_PTR(func.get()), "get_statistics") == 0) {
    int poolThreadsCount = this->_pool.size();
    int threadsBusy = this->_pool.getBusyThreads();
    int jobsLeft = this->_pool.getJobsLeft();
    std::size_t isolates_count = this->_v8->isolates_count();

    auto jobsPerThread = this->_pool.getJobsPerThread();
    auto jobsPerThreadSize = jobsPerThread.size();

    std::shared_ptr<ETERM*> jobsPerThread_e = make_shared_array<ETERM*>(jobsPerThreadSize);
    auto arr = jobsPerThread_e.get();

    for (std::size_t i = 0; i < jobsPerThreadSize; ++i) {
      // for implicit conversion from atomicwrapper to integer type
      std::size_t numOfThreads = jobsPerThread[i];
      arr[i] = erl_format("{~i, ~i}", i, numOfThreads);
    }

    ETERMptr jobsPerThreadTerm(erl_mk_list(arr, jobsPerThread.size()), ErlFreeTerm);

    for (std::size_t i = 0; i < jobsPerThreadSize; ++i) {
      erl_free_term(arr[i]);
    }

    ETERMptr resp = ETERMptr(
      erl_format("{cnode, ~i,"
                 "["
                   "{pool_threads_count, ~i},"
                   "{isolates_count, ~i},"
                   "{theads_busy, ~i},"
                   "{jobs_left, ~i},"
                   "{jobs_per_threads, ~w}"
                 "]"
                 "}",
                  CNode::STATUS::OK,
                  poolThreadsCount,
                  isolates_count,
                  threadsBusy,
                  jobsLeft,
                  jobsPerThreadTerm.get()),
      ErlFreeTerm);

    erl_send(fd, fromp.get(), resp.get());

  } else if (strcmp(ERL_ATOM_PTR(func.get()), "get_max_diff_time") == 0) {
    auto resp = ETERMptr(erl_format("{cnode, ~i, ~i}", CNode::STATUS::OK, this->_maxDiffTime), ErlFreeTerm);
    erl_send(fd, fromp.get(), resp.get());
  } else if (strcmp(ERL_ATOM_PTR(func.get()), "set_max_diff_time") == 0) {

    ETERMptr maxDiffTimeTerm(erl_element(3, tuplep.get()), ErlFreeTerm);
    this->_maxDiffTime = ERL_INT_UVALUE(maxDiffTimeTerm);

    auto resp = ETERMptr(erl_format("{cnode, ~i, ~i}", CNode::STATUS::OK, this->_maxDiffTime), ErlFreeTerm);
    erl_send(fd, fromp.get(), resp.get());
  } else if (strcmp(ERL_ATOM_PTR(func.get()), "set_max_time_exec_threshold") == 0) {

    ETERMptr time_e(erl_element(3, tuplep.get()), ErlFreeTerm);

    const std::size_t execTime = ERL_INT_UVALUE(time_e);
    this->_v8->setMaxExecutionTime(execTime);

    assert(this->_v8->getMaxExecutionTime() == execTime);

    auto resp = ETERMptr(erl_format("{cnode, ~i, ~i}", CNode::STATUS::OK, execTime), ErlFreeTerm);
    erl_send(fd, fromp.get(), resp.get());
  } else if (strcmp(ERL_ATOM_PTR(func), "get_max_time_exec_threshold") == 0) {

    auto resp = ETERMptr(erl_format("{cnode, ~i, ~i}", CNode::STATUS::OK, this->_v8->getMaxExecutionTime()), ErlFreeTerm);
    erl_send(fd, fromp.get(), resp.get());
  } else if (strcmp(ERL_ATOM_PTR(func.get()), "get_require_cache_file") == 0) {

      ETERMptr fileNameTerm(erl_element(3, tuplep.get()), ErlFreeTerm);
      CharPtr fileName_c = CharPtr(erl_iolist_to_string(fileNameTerm.get()), ErlFree);

      std::string fileName_s = std::string(fileName_c.get());

      std::tuple<int, std::string> res = this->_v8->getRequireCachedFile(fileName_s);

      auto resp = ETERMptr(
        erl_format("{cnode, ~i, ~b}",
                   std::get<ERR_CODE>(res),
                   std::get<DATA>(res).c_str()),
                   ErlFreeTerm);

      erl_send(fd, fromp.get(), resp.get());

  } else if (strcmp(ERL_ATOM_PTR(func.get()), "update_require_cache_file") == 0) {

      ETERMptr fileNameTerm(erl_element(3, tuplep.get()), ErlFreeTerm);
      CharPtr fileName_c = CharPtr(erl_iolist_to_string(fileNameTerm.get()), ErlFree);
      std::string fileName_s = std::string(fileName_c.get());

      std::tuple<int, std::string> res = this->_v8->updateRequireCache(fileName_s);

      auto resp = ETERMptr(
        erl_format("{cnode, ~i, ~b}",
                   std::get<ERR_CODE>(res),
                   std::get<DATA>(res).c_str()),
        ErlFreeTerm);

      erl_send(fd, fromp.get(), resp.get());
  } else if (strcmp(ERL_ATOM_PTR(func.get()), "get_priorities") == 0) {

    auto priorityMapSize = this->_priorityMap.size();
    std::shared_ptr<ETERM*> priorityMap_e = make_shared_array<ETERM*>(priorityMapSize);
    auto arr = priorityMap_e.get();

    {
      int i = 0;
      for(const auto& priority: this->_priorityMap) {
        arr[i++] = erl_format("{~b, ~i}", priority.first.c_str(), priority.second);
      }
    }

    ETERMptr priorityMapTerm(erl_mk_list(arr, priorityMapSize), ErlFreeTerm);

    for (std::size_t i = 0; i < priorityMapSize; ++i) {
      erl_free_term(arr[i]);
    }

    ETERMptr resp = ETERMptr(
      erl_format("{cnode, ~i, {priorities, ~w}}",
                  CNode::STATUS::OK,
                  priorityMapTerm.get()),
      ErlFreeTerm);

    erl_send(fd, fromp.get(), resp.get());

  } else if (strcmp(ERL_ATOM_PTR(func.get()), "set_priority") == 0) {

    ETERMptr commandTerm(erl_element(3, tuplep.get()), ErlFreeTerm);
    CharPtr command = CharPtr(erl_iolist_to_string(commandTerm.get()), ErlFree);
    ETERMptr priorityTerm(erl_element(4, tuplep.get()), ErlFreeTerm);

    const int priority = ERL_INT_VALUE(priorityTerm);

    this->_priorityMap[command.get()] = priority;

    auto resp = ETERMptr(
      erl_format("{cnode, ~i, {command, ~b}, {priority, ~i}}", CNode::STATUS::OK, command.get(), priority),
      ErlFreeTerm
    );

    erl_send(fd, fromp.get(), resp.get());
  } else if (strcmp(ERL_ATOM_PTR(func.get()), "remove_priority") == 0) {

    ETERMptr commandTerm(erl_element(3, tuplep.get()), ErlFreeTerm);
    CharPtr command = CharPtr(erl_iolist_to_string(commandTerm.get()), ErlFree);

    auto resp = ETERMptr(
      erl_format("{cnode, ~i, ~i}", CNode::STATUS::OK, this->_priorityMap.erase(command.get())),
      ErlFreeTerm
    );

    erl_send(fd, fromp.get(), resp.get());
  } else {

    const auto now = std::chrono::system_clock::now();
    const auto timeWhenTaskHasBeenAdded =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

    auto job = std::bind(
      &CNode::processV8,
      this,
      fd,
      fromp,
      tuplep,
      func,
      timeWhenTaskHasBeenAdded,
      std::placeholders::_1
    ); // _1 for thread num
    int priority = this->_priorityMap[ERL_ATOM_PTR(func.get())];

    this->_pool.addJob(priority, job);
  }

}

void CNode::processV8(
  int fd,
  ETERMptr fromp,
  ETERMptr tuplep,
  ETERMptr func,
  std::chrono::milliseconds timeWhenTaskHasBeenAdded,
  int threadNum)
{

  ETERMptr resp;

  const auto now = std::chrono::system_clock::now();
  const auto timeNow =
    std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

  auto timeDiff = std::size_t((timeNow - timeWhenTaskHasBeenAdded).count());

  if (timeDiff > this->_maxDiffTime) {
    resp = ETERMptr(erl_format("{cnode, ~i, ~b}", CNode::STATUS::THREAD_POOL_TIMEOUT, "Threadpool queue timeout."), ErlFreeTerm);
  } else if (strcmp(ERL_ATOM_PTR(func.get()), "check_code") == 0) {

    ETERMptr src_term(erl_element(3, tuplep.get()), ErlFreeTerm);
    ETERMptr data_term(erl_element(4, tuplep.get()), ErlFreeTerm);

    CharPtr src = CharPtr(erl_iolist_to_string(src_term.get()), ErlFree);
    CharPtr data = CharPtr(erl_iolist_to_string(data_term.get()), ErlFree);

    std::tuple<int, std::string> res = this->_v8->checkCode(src.get(), data.get(), threadNum);

    resp = ETERMptr(
      erl_format("{cnode, ~i, ~b}",
                 std::get<ERR_CODE>(res),
                 std::get<DATA>(res).c_str()),
      ErlFreeTerm);

  } else if (strcmp(ERL_ATOM_PTR(func.get()), "compile") == 0) {

    ETERMptr conv_id_term(erl_element(3, tuplep.get()), ErlFreeTerm);
    ETERMptr node_id_term(erl_element(4, tuplep.get()), ErlFreeTerm);
    ETERMptr data_term(erl_element(5, tuplep.get()), ErlFreeTerm);

    CharPtr conv_id_c = CharPtr(erl_iolist_to_string(conv_id_term.get()), ErlFree);
    CharPtr node_id_c = CharPtr(erl_iolist_to_string(node_id_term.get()), ErlFree);
    // if compile - data is a source code
    // if run - data is a json
    CharPtr data = CharPtr(erl_iolist_to_string(data_term.get()), ErlFree);

    std::tuple<int, std::string> res = this->_v8->compile(conv_id_c.get(), node_id_c.get(), data.get());

    resp = ETERMptr(
      erl_format("{cnode, ~i, ~b}",
                 std::get<ERR_CODE>(res),
                 std::get<DATA>(res).c_str()),
      ErlFreeTerm);

  } else if (strcmp(ERL_ATOM_PTR(func.get()), "remove") == 0) {

    ETERMptr conv_id_term(erl_element(3, tuplep.get()), ErlFreeTerm);
    ETERMptr node_id_term(erl_element(4, tuplep.get()), ErlFreeTerm);

    CharPtr conv_id_c = CharPtr(erl_iolist_to_string(conv_id_term.get()), ErlFree);
    CharPtr node_id_c = CharPtr(erl_iolist_to_string(node_id_term.get()), ErlFree);

    std::tuple<int, std::string> res = this->_v8->remove(conv_id_c.get(), node_id_c.get());

    resp = ETERMptr(
      erl_format("{cnode, ~i, ~b}",
                 std::get<ERR_CODE>(res),
                 std::get<DATA>(res).c_str()),
      ErlFreeTerm);

  } else if (strcmp(ERL_ATOM_PTR(func.get()), "run") == 0) {

    ETERMptr conv_id_term(erl_element(3, tuplep.get()), ErlFreeTerm);
    ETERMptr node_id_term(erl_element(4, tuplep.get()), ErlFreeTerm);
    ETERMptr data_term(erl_element(5, tuplep.get()), ErlFreeTerm);

    CharPtr conv_id_c = CharPtr(erl_iolist_to_string(conv_id_term.get()), ErlFree);
    CharPtr node_id_c = CharPtr(erl_iolist_to_string(node_id_term.get()), ErlFree);
    // if compile - data is a source code
    // if run - data is a json
    CharPtr data = CharPtr(erl_iolist_to_string(data_term.get()), ErlFree);

    std::tuple<int, std::string> res = this->_v8->run(conv_id_c.get(), node_id_c.get(), data.get(), threadNum);

    resp = ETERMptr(
      erl_format("{cnode, ~i, ~b}",
                 std::get<ERR_CODE>(res),
                 std::get<DATA>(res).c_str()),
      ErlFreeTerm);

  } else {
    resp = ETERMptr(erl_format("{cnode, ~i, ~b}", CNode::STATUS::ERR, "Unsupported command."), ErlFreeTerm);
  }

  erl_send(fd, fromp.get(), resp.get());
}
