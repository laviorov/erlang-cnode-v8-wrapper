#ifndef CONCURRENT_THREADPOOL_H
#define CONCURRENT_THREADPOOL_H

#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <list>
#include <queue>
#include <functional>
#include <condition_variable>
#include <atomic>
#include <memory>

namespace pb {
namespace concurrent {

  template <typename F>
  class ThreadPool {

    void process(const std::size_t& threadNum) {
      while(!this->stop) {
        auto job = next_job();
        this->busyThreads += 1;
        job(threadNum);
        this->busyThreads -= 1;
        {
          std::shared_lock<std::shared_mutex> lock(this->jobsPerThreadMutex);
          this->jobsPerThread[threadNum] += 1;
        }
        this->jobsLeft -= 1;
        this->jobsDone += 1;
        waitVar.notify_one();
      }
    }

    F next_job() {
      F res;
      std::unique_lock<std::mutex> job_lock(this->jobsMutex);

      jobAvailableVar.wait(job_lock, [this] { return this->jobs.size() || this->stop; });

      if(!this->stop) {
        res = this->jobs.top().second;
        this->jobs.pop();
      }
      else {
        res = [](std::size_t){};
        this->jobsLeft += 1;
        this->jobsDone -= 1;
      }
      return res;
    }

  public:
    ThreadPool(const std::size_t& threadCount, const size_t& _maxQueueSize)
      : jobsPerThread(threadCount)
      , maxQueueSize(_maxQueueSize)
      , jobsLeft(0)
      , jobsDone(0)
      , busyThreads(0)
      , stop(false)
      , finished(false)
    {
      for (std::size_t i = 0; i < threadCount; i++) {
        this->threads.push_back(std::move(
          std::thread([this, i] {
            this->process(i);
          })
        ));
      }
    }

    ~ThreadPool() {
      this->joinAll();
    }

    bool addJob(int priority, F job) {
      // to prevent blow up memory
      if (this->jobsLeft >= std::atomic_int(this->maxQueueSize)) {
        return false;
      }

      std::lock_guard<std::mutex> guard(this->jobsMutex);
      this->jobs.push(std::make_pair(priority, std::bind(job, std::placeholders::_1))); // _1 for thread num
      this->jobsLeft += 1;
      this->jobAvailableVar.notify_one();
      return true;
    }

    int size() const {
      return this->threads.size();
    }

    int getBusyThreads() const {
      return this->busyThreads;
    }

    int getAmountOfDoneJobs() const {
      return this->jobsDone;
    }

    int getJobsLeft() const {
      return this->jobsLeft;
    }

    std::vector<int> getJobsPerThread() {
      std::unique_lock<std::shared_mutex> lock(this->jobsPerThreadMutex);
      return this->jobsPerThread;
    }

    void joinAll(bool waitForAll = true) {
      if(!this->finished) {

        if(waitForAll) {
          this->waitAll();
        }

        this->stop = true;
        this->jobAvailableVar.notify_all();

        for(auto &thread : this->threads) {
          if(thread.joinable()) {
            thread.join();
          }
        }

        this->finished = true;
        this->threads.clear();
      }
    }

    void waitAll() {
      if(this->jobsLeft > 0) {
        std::unique_lock<std::mutex> lock(this->waitMutex);
        this->waitVar.wait(lock, [this]{ return this->jobsLeft == 0; });
      }
    }

  private:

    typedef std::pair<int, F> queueItem;

    struct QueueItemCompare {
      bool operator()(const queueItem& n1, const queueItem& n2) {
        return n1.first < n2.first;
      }
    };

    std::vector<std::thread> threads;
    std::priority_queue<
      queueItem,
      std::deque<queueItem>,
      QueueItemCompare
    > jobs;

    std::vector<int> jobsPerThread;
    const std::size_t maxQueueSize;

    std::atomic_int jobsLeft;
    std::atomic_int jobsDone;
    std::atomic_int busyThreads;
    std::atomic_bool stop;
    std::atomic_bool finished;

    std::condition_variable jobAvailableVar;
    std::condition_variable waitVar;

    std::mutex waitMutex;
    std::mutex jobsMutex;
    std::shared_mutex jobsPerThreadMutex;
  };

} // namespace concurrent
} // namespace pb

#endif //CONCURRENT_THREADPOOL_H
