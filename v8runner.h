#ifndef V8_RUNNER_H
#define V8_RUNNER_H

#include <string>
#include <memory>
#include <map>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <atomic>
#include <thread>
#include <shared_mutex>
#include <tuple>
#include <iostream>
#include <sstream>
#include <functional>
#include <queue>
#include <algorithm>
#include <set>

#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

#include <libplatform/libplatform.h>
#include <v8.h>

#define ERR_CODE 0
#define DATA 1

namespace pb {

  class V8Runner {

  public:

    enum STATUS {
      NO_ERR = 0,
      ERR = 1,
      COMPILE_ERR = 2,
      NOT_FOUND_PAIR_ERR = 3,
      NOT_FUNCTION_ERR = 4,
      BAD_INPUT_ERR = 5,
      SCRIPT_RUNTIME_ERR = 6,
      SCRIPT_TERMINATED_ERR = 7,
      CACHED_REQUIRE_FILE_ERR = 8
    };

    typedef std::string Conv;
    typedef std::string Node;
    typedef std::pair<Conv, Node> ConvNodePair;

    struct IsolateHeapStatistics {
      double totalMemConsumptionInMb;
      double heapSizeMb;
      double mallocedMemMb;
    };

    V8Runner(int argc,
             char* argv[],
             const fs::path& pathToLibs,
             const std::size_t& maxExecutionTime,
             const std::size_t& maxRAMAvailable,
             const std::size_t& timeCheckerSleepTime,
             const std::size_t& threadsCount = 1);
    ~V8Runner();

    std::unordered_map<std::string, std::string> loadLibs();

    std::tuple<int, std::string> checkCode(
      const char* src,
      const char* data,
      const std::size_t& threadId = 0);

    std::tuple<int, std::string> compile(
      const char* conv_id,
      const char* node_id,
      const char* src);

    std::tuple<int, std::string> remove(
      const char* conv_id,
      const char* node_id);

    std::tuple<int, std::string> run(
      const char* conv_id,
      const char* node_id,
      const char* data,
      const std::size_t& threadId = 0);

    void cleanData();

    void setMaxExecutionTime(const std::size_t& maxExecutionTime);
    std::size_t getMaxExecutionTime();

    void setTimeCheckerSleepTime(const std::size_t& watchDogSleepTime);
    std::size_t getTimeCheckerSleepTime();

    std::size_t isolates_count();
    std::size_t convs_count();
    std::size_t nodes_count();

    static std::tuple<int, std::string> updateRequireCache(const std::string& fileName);
    static std::tuple<int, std::string> getRequireCachedFile(const std::string& fileName);

  private:

    static fs::path _pathToLibs;
    static std::shared_mutex _requireCacheMutex;

    static std::unordered_map<std::string, std::string> _requireCache;

    v8::Platform *_platform;
    v8::Isolate::CreateParams _create_params;

    typedef v8::Persistent<v8::ObjectTemplate, v8::CopyablePersistentTraits<v8::ObjectTemplate>> PersistentObjectTemplate;
    typedef v8::Persistent<v8::Context, v8::CopyablePersistentTraits<v8::Context>> PersistentContext;
    typedef v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function>> PersistentFunction;

    typedef std::tuple<v8::Isolate*,
                       PersistentObjectTemplate,
                       PersistentContext,
                       PersistentFunction
                      > V8Instance;

    class IsolateRelatedData {
    public:
      IsolateRelatedData(const PersistentObjectTemplate& template_, const PersistentContext& context):
        _template(template_), _context(context) {}
    public:
      PersistentContext getPContext() const { return _context; }
      void clean() {
        _template.Reset();
        _context.Reset();
      }
    private:
      PersistentObjectTemplate _template;
      PersistentContext _context;
    };

    template <typename Key>
    struct Hash {
      std::size_t operator()( const Key& k ) const {
          // Compute individual hash values for first, second and third
          // http://stackoverflow.com/a/1646913/126995
          std::size_t res = 17;
          res = res * 31 + std::hash<std::string>()( k.first );
          res = res * 31 + std::hash<std::string>()( k.second );
          return res;
      }
    };

    struct ScriptWorkTime {
      std::atomic<bool> isWorking;
      v8::Isolate* isolate;
      std::chrono::milliseconds started;

      ScriptWorkTime(
        bool isWorking,
        v8::Isolate* isolate,
        const std::chrono::milliseconds& started
      ) {
        this->isWorking = isWorking;
        this->isolate = isolate;
        this->started = started;
      }
    };

    std::tuple<v8::Isolate*, std::shared_ptr<IsolateRelatedData>> makeNewIsolate();
    v8::Isolate* getIsolate();

    std::vector<v8::Isolate*> _isolates;

    std::map<
      v8::Isolate*,
      std::shared_ptr<IsolateRelatedData>
    > _isolatesData;

    std::unordered_map<
      Conv,
      v8::Isolate*
    > _convs;

    std::unordered_map<
      ConvNodePair,
      PersistentFunction,
      Hash<ConvNodePair>
    > _functions;

    // to kill long running script
    std::vector<std::shared_ptr<ScriptWorkTime>> _timing;

    std::tuple<int, std::string> _checkCode(
      const char* src,
      const char* data,
      const std::size_t& threadId);

    std::tuple<int, std::string> _compile(
      const char* conv_id,
      const char* node_id,
      const char* src);

    std::tuple<int, std::string> _remove(
      const char* conv_id,
      const char* node_id);

    std::tuple<int, std::string> _run(
      const char* conv_id,
      const char* node_id,
      const char* data,
      const std::size_t& threadId);

    void _setIsolates(const std::size_t& N);

    void _timeCheckerFunc();

    static void _Print(const v8::FunctionCallbackInfo<v8::Value>& args);

    static void _Require(const v8::FunctionCallbackInfo<v8::Value>& args);

    static std::string _makeTryCatchError(const v8::TryCatch& try_catch);

    static std::tuple<int, std::string> _getRequireFile(const std::string& fileName);

    // Stringify V8 value to JSON
    // return empty string for empty value
    static std::string _jsonStr(v8::Isolate* isolate, v8::Handle<v8::Value> value);

    static std::string _readFile(const std::string& filename); // maybe boost ?

  private:

    std::shared_mutex _compileMutex;
    std::shared_mutex _timeCheckerMutex;

    std::thread _timeChecker;

    bool _timeCheckerWatch;

    std::size_t _maxExecutionTime;
    std::size_t _maxRAMAvailable;
    std::size_t _timeCheckerSleepTime;
    std::size_t _threadsCount;


  };

}

#endif
