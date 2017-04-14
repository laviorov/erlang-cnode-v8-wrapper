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

namespace pb {
namespace concurrent {

template <typename F>
class ThreadPool {

  typedef std::pair<int, std::function<void(int)>> queueItem;

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
  > queue;
  std::vector<std::atomic_int> jobsPerThread;

  std::atomic_int         busyThreads;
  std::atomic_int         jobs_left;
  std::atomic_bool        bailout;
  std::atomic_bool        finished;
  std::condition_variable job_available_var;
  std::condition_variable wait_var;
  std::mutex              wait_mutex;
  std::mutex              queue_mutex;

  /**
   *  Take the next job in the queue and run it.
   *  Notify the main thread that a job has completed.
   */
  void Task(int threadNum) {
      while( !bailout ) {
        auto job = next_job();
        this->busyThreads += 1;
        job(threadNum);
        --jobs_left;
        this->jobsPerThread[threadNum] += 1;
        this->busyThreads -= 1;
        wait_var.notify_one();
      }
  }

  /**
   *  Get the next job; pop the first item in the queue,
   *  otherwise wait for a signal from the main thread.
   */
  std::function<void(std::size_t)> next_job() {
      std::function<void(std::size_t)> res;
      std::unique_lock<std::mutex> job_lock( queue_mutex );

      // Wait for a job if we don't have any.
      job_available_var.wait( job_lock, [this]() ->bool { return queue.size() || bailout; } );

      // Get job from the queue
      if( !bailout ) {
          res = queue.top().second;
          queue.pop();
      }
      else { // If we're bailing out, 'inject' a job into the queue to keep jobs_left accurate.
          res = [](int){};
          ++jobs_left;
      }
      return res;
  }

  void _createThreads(const std::size_t&  threadCount) {
    for (std::size_t i = 0; i < threadCount; i++) {
      this->threads.push_back(std::move(std::thread( [this, i] {
        this->Task(i);
      })));
    }
  }

  void _resizeJobsPerThread(const std::size_t& threadCount) {
    this->jobsPerThread.clear();
    this->jobsPerThread.resize(threadCount);
  }

public:
  ThreadPool(const std::size_t& threadCount = 4)
      : jobsPerThread(threadCount)
      , busyThreads( 0 )
      , jobs_left( 0 )
      , bailout( false )
      , finished( false )
  {
    this->_createThreads(threadCount);
  }

  ~ThreadPool() {
      JoinAll();
  }

  inline std::size_t numberOfBusyThreads() const {
    return this->busyThreads;
  }

  inline unsigned Size() const {
    return this->threads.size();
  }

  void SetSize(const std::size_t& threadCount) {

    // this->JoinAll();

    if( !finished ) {
      bailout = true;
      job_available_var.notify_all();

      for( auto &x : this->threads ) {
        if( x.joinable() ) {
          x.join();
        }
      }
      finished = true;
      this->threads.clear();
    }

    this->busyThreads = 0;
    this->bailout = false;
    this->finished = false;
    this->_resizeJobsPerThread(threadCount);
    this->_createThreads(threadCount);
  }

  inline unsigned JobsRemaining() {
    std::lock_guard<std::mutex> guard( queue_mutex );
    return queue.size();
  }

  inline std::vector<std::atomic_int> getJobsPerThread() {
    return this->jobsPerThread;
  }


  template<typename... Args>
  void AddJob(int priority, F job) {
      std::lock_guard<std::mutex> guard( queue_mutex );
      this->queue.push(std::make_pair( priority, std::bind(job, std::placeholders::_1) )); // _1 for thread num
      // queue.emplace_back( std::bind(job, std::forward<Args>(args)..., std::placeholders::_1) );
      ++jobs_left;
      job_available_var.notify_one();
  }

  /**
   *  Join with all threads. Block until all threads have completed.
   *  Params: WaitForAll: If true, will wait for the queue to empty
   *          before joining with threads. If false, will complete
   *          current jobs, then inform the threads to exit.
   *  The queue will be empty after this call, and the threads will
   *  be done. After invoking `ThreadPool::JoinAll`, the pool can no
   *  longer be used. If you need the pool to exist past completion
   *  of jobs, look to use `ThreadPool::WaitAll`.
   */
  void JoinAll( bool WaitForAll = true ) {
      if( !finished ) {
          if( WaitForAll ) {
              WaitAll();
          }

          // note that we're done, and wake up any thread that's
          // waiting for a new job
          bailout = true;
          job_available_var.notify_all();

          for( auto &x : this->threads ) {
            if( x.joinable() ) {
              x.join();
            }
          }
          finished = true;
          this->threads.clear();
      }
  }

  /**
   *  Wait for the pool to empty before continuing.
   *  This does not call `std::thread::join`, it only waits until
   *  all jobs have finshed executing.
   */
  void WaitAll() {
      if( jobs_left > 0 ) {
          std::unique_lock<std::mutex> lk( wait_mutex );
          wait_var.wait( lk, [this]{ return this->jobs_left == 0; } );
          lk.unlock();
      }
  }
};

} // namespace concurrent
} // namespace pb

#endif //CONCURRENT_THREADPOOL_H
