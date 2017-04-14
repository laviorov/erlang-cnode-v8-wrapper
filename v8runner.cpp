#include <v8runner.h>

using namespace pb;

fs::path V8Runner::_pathToLibs;
std::shared_mutex V8Runner::_requireCacheMutex;
std::unordered_map<std::string, std::string> V8Runner::_requireCache;

std::vector<std::string> splitString(const std::string& str, const auto& separator);

V8Runner::V8Runner(int argc,
                   char* argv[],
                   const fs::path& pathToLibs,
                   const std::size_t& maxExecutionTime,
                   const std::size_t& maxRAMAvailable,
                   const std::size_t& timeCheckerSleepTime,
                   const std::size_t& threadsCount /* = 1 */):

                   _platform(nullptr),
                   _timing(threadsCount),
                   _timeCheckerWatch(true),
                   _maxExecutionTime(maxExecutionTime),
                   _maxRAMAvailable(maxRAMAvailable),
                   _timeCheckerSleepTime(timeCheckerSleepTime),
                   _threadsCount(threadsCount) {

  V8Runner::_pathToLibs = pathToLibs;

  v8::V8::InitializeICUDefaultLocation(argv[0]);
  v8::V8::InitializeExternalStartupData(argv[0]);
  this->_platform = v8::platform::CreateDefaultPlatform();

  const uint64_t physical_memory = this->_maxRAMAvailable * 1024 * 1024 * 1024;
  const uint64_t virtual_memory_limit = 0;
  this->_create_params.constraints.ConfigureDefaults(physical_memory, virtual_memory_limit);

  this->_create_params.array_buffer_allocator =
    v8::ArrayBuffer::Allocator::NewDefaultAllocator();

  v8::V8::InitializePlatform(this->_platform);
  v8::V8::Initialize();

  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);

  std::cout << "old space " << this->_create_params.constraints.max_old_space_size() << std::endl;
  std::cout << "semi space " << this->_create_params.constraints.max_semi_space_size() << std::endl;
  std::cout << "executable space " << this->_create_params.constraints.max_executable_size() << std::endl;

  this->_timeChecker = std::thread(&pb::V8Runner::_timeCheckerFunc, this);

  // the amount of isolates should be equal to threadsCount
  this->_setIsolates(threadsCount);

  this->loadLibs();

}


V8Runner::~V8Runner() {

  this->_timeCheckerWatch = false;
  this->_timeChecker.join();

  this->cleanData();

  // clean isolate related data
  for (auto& kv: this->_isolatesData) {
    kv.second->clean();
  }

  // clean isolates
  for(auto& isolate: this->_isolates) {
    isolate->Dispose();
  }

  this->_isolatesData.clear();
  this->_isolates.clear();

  v8::V8::Dispose();
  v8::V8::ShutdownPlatform();

  delete this->_platform;
  delete this->_create_params.array_buffer_allocator;
}

void V8Runner::_setIsolates(const std::size_t& N) {
  std::unique_lock<std::shared_mutex> lock(this->_compileMutex);

  for (uint i = 0 ; i < N; i++) {
    auto isolateData = this->makeNewIsolate();

    auto isolate = std::get<0>(isolateData);
    this->_isolatesData[isolate] = std::get<1>(isolateData);

    this->_isolates.push_back(isolate);
  }
}

std::unordered_map<std::string, std::string> V8Runner::loadLibs() {

  std::unique_lock<std::shared_mutex> lock(V8Runner::_requireCacheMutex);

  for (fs::recursive_directory_iterator i(V8Runner::_pathToLibs), end; i != end; ++i) {
    if (!fs::is_directory(i->path()) && i->path().extension() == ".js") {

      auto requireFile = V8Runner::_getRequireFile(i->path().string());

      if (std::get<ERR_CODE>(requireFile) != STATUS::NO_ERR) {

        std::cerr << "[ERROR] [loadLibs] "
                  << "Code: " << std::get<ERR_CODE>(requireFile) << ", "
                  << "Message: " << std::get<DATA>(requireFile)
                  << std::endl;
        continue;
      }

      auto parts = ::splitString(i->path(), fs::path::preferred_separator);
      auto partsLen = parts.size();
      auto libPath = parts[partsLen-2] + fs::path::preferred_separator + parts[partsLen-1];

      V8Runner::_requireCache[libPath] = std::get<DATA>(requireFile);
    }
  }

  return V8Runner::_requireCache;

}


std::tuple<int, std::string> V8Runner::compile(
  const char* conv_id,
  const char* node_id,
  const char* src
) {
  return this->_compile(conv_id, node_id, src);
}


std::tuple<int, std::string> V8Runner::remove(
  const char* conv_id,
  const char* node_id
) {
  return this->_remove(conv_id, node_id);
}


std::tuple<int, std::string> V8Runner::run(
  const char* conv_id,
  const char* node_id,
  const char* data,
  const std::size_t& threadId
) {
  return this->_run(conv_id, node_id, data, threadId);
}

std::tuple<int, std::string> V8Runner::checkCode(
  const char* src,
  const char* data,
  const std::size_t& threadId
) {
  return this->_checkCode(src, data, threadId);
}

void V8Runner::setMaxExecutionTime(
  const std::size_t& maxExecutionTime) {

  std::unique_lock<std::shared_mutex> lock(this->_timeCheckerMutex);
  this->_maxExecutionTime = maxExecutionTime;

}

std::size_t V8Runner::getMaxExecutionTime() {
  std::shared_lock<std::shared_mutex> lock(this->_timeCheckerMutex);
  return this->_maxExecutionTime;
}

void V8Runner::setTimeCheckerSleepTime(const std::size_t& timeCheckerSleepTime) {
  std::unique_lock<std::shared_mutex> lock(this->_timeCheckerMutex);
  this->_timeCheckerSleepTime = timeCheckerSleepTime;
}

std::size_t V8Runner::getTimeCheckerSleepTime() {
  std::shared_lock<std::shared_mutex> lock(this->_timeCheckerMutex);
  return this->_timeCheckerSleepTime;
}

std::size_t V8Runner::isolates_count() {
  std::shared_lock<std::shared_mutex> lock(this->_compileMutex);
  return this->_isolates.size();
}

std::size_t V8Runner::convs_count() {
  std::shared_lock<std::shared_mutex> lock(this->_compileMutex);
  return this->_convs.size();
}

std::size_t V8Runner::nodes_count() {
  std::shared_lock<std::shared_mutex> lock(this->_compileMutex);
  return this->_functions.size();
}

std::tuple<int, std::string> V8Runner::_checkCode(
  const char* src,
  const char* data,
  const std::size_t& threadId
) {

  std::tuple<int, std::string> retValue {
    STATUS::NO_ERR,
    "WE JUST COMPILED THIS CODE!"
  };

  auto isolateData = this->makeNewIsolate();

  auto isolate = std::get<0>(isolateData);
  auto iData = std::get<1>(isolateData);

  {
    std::shared_ptr<v8::Isolate> freeIsolate(
      isolate,
      [&iData](v8::Isolate* ptr) {
        iData->clean(); // clean up context and template
        ptr->Dispose(); // and then clean up isolate
      }
    );

    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope scope(isolate);

    auto context = v8::Local<v8::Context>::New(isolate, iData->getPContext());

    v8::Context::Scope context_scope(context);

    auto script = v8::String::NewFromUtf8(isolate, src);

    v8::TryCatch try_catch(isolate);

    // compile

    v8::Local<v8::Script> compiled_script;
    if (!v8::Script::Compile(context, script).ToLocal(&compiled_script)) {
      std::get<ERR_CODE>(retValue) = STATUS::COMPILE_ERR;
      std::get<DATA>(retValue) = V8Runner::_makeTryCatchError(try_catch);
      return retValue;
    }

    v8::Local<v8::Value> result;
    if (!compiled_script->Run(context).ToLocal(&result)) {
      std::get<ERR_CODE>(retValue) = STATUS::COMPILE_ERR;
      std::get<DATA>(retValue) = V8Runner::_makeTryCatchError(try_catch);
      return retValue;
    }
  }

  return retValue;
}

std::tuple<v8::Isolate*, std::shared_ptr<V8Runner::IsolateRelatedData>>
  V8Runner::makeNewIsolate() {

  auto isolate = v8::Isolate::New(this->_create_params);

  v8::Locker locker(isolate);
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope scope(isolate);

  auto pGlobalTemplate =
    PersistentObjectTemplate(isolate, v8::ObjectTemplate::New(isolate));

  auto global = v8::Local<v8::ObjectTemplate>::New(isolate, pGlobalTemplate);

  global->Set(v8::String::NewFromUtf8(isolate, "print"),
              v8::FunctionTemplate::New(isolate, V8Runner::_Print));
  global->Set(v8::String::NewFromUtf8(isolate, "require"),
              v8::FunctionTemplate::New(isolate, V8Runner::_Require));

  PersistentContext pContext(isolate, v8::Context::New(isolate, nullptr, global));

  return std::make_tuple(
    isolate,
    std::make_shared<IsolateRelatedData>(pGlobalTemplate, pContext)
  );

}

v8::Isolate* V8Runner::getIsolate() {
  static std::size_t nextIsolate = 0;
  if (nextIsolate == this->_isolates.size()) {
    nextIsolate = 0;
  }
  return this->_isolates[nextIsolate++];
}


std::tuple<int, std::string> V8Runner::_compile(
  const char* conv_id,
  const char* node_id,
  const char* src) {

  std::tuple<int, std::string> retValue;

  const Conv conv = std::string(conv_id);
  const Node node = std::string(node_id);

  std::unique_lock<std::shared_mutex> lock(this->_compileMutex);

  {
    // clean the function - if we already compiled this pair of conv and node
    auto it = this->_functions.find(std::make_pair(conv, node));
    if (it != this->_functions.end()) {
      it->second.Reset();
    }
  }

  v8::Isolate* isolate = nullptr;

  {
    // compile the same conv withing the same isolate
    // if this conv does not have isolate, use next isolate
    auto isolateItr = this->_convs.find(conv);
    if (isolateItr == this->_convs.end()) {
      isolate = this->getIsolate();
      this->_convs[conv] = isolate;
    } else {
      isolate = isolateItr->second;
    }
  }

  {
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope scope(isolate);

    auto isolateData = this->_isolatesData[isolate];

    auto context = v8::Local<v8::Context>::New(isolate, isolateData->getPContext());

    v8::Context::Scope context_scope(context);

    auto script = v8::String::NewFromUtf8(isolate, src);

    v8::TryCatch try_catch(isolate);

    // compile

    v8::Local<v8::Script> compiled_script;
    if (!v8::Script::Compile(context, script).ToLocal(&compiled_script)) {

      std::get<ERR_CODE>(retValue) = STATUS::COMPILE_ERR;
      std::get<DATA>(retValue) = V8Runner::_makeTryCatchError(try_catch);

      return retValue;
    }

    v8::Local<v8::Value> result;
    if (!compiled_script->Run(context).ToLocal(&result)) {

      std::get<ERR_CODE>(retValue) = STATUS::COMPILE_ERR;
      std::get<DATA>(retValue) = V8Runner::_makeTryCatchError(try_catch);

      return retValue;
    }

    this->_functions[std::make_pair(conv, node)] = PersistentFunction(isolate, result.As<v8::Function>());

  }

  std::get<ERR_CODE>(retValue) = STATUS::NO_ERR;

  return retValue;
}


std::tuple<int, std::string> V8Runner::_remove(
  const char* conv,
  const char* node) {

  std::tuple<int, std::string> retValue;

  auto key = std::make_pair(Conv(conv), Node(node));

  std::unique_lock<std::shared_mutex> lock(this->_compileMutex);

  auto it = this->_functions.find(key);
  if (it != this->_functions.end()) {
    it->second.Reset();
  }

  std::get<ERR_CODE>(retValue) = STATUS::NO_ERR;

  return retValue;
}


std::tuple<int, std::string> V8Runner::_run(
  const char* conv_id,
  const char* node_id,
  const char* data,
  const std::size_t& threadId
) {

  std::tuple<int, std::string> retValue;

  const Conv conv = std::string(conv_id);
  const Node node = std::string(node_id);

  std::shared_lock<std::shared_mutex> lock(this->_compileMutex);

  auto it = this->_functions.find(std::make_pair(conv, node));

  if (it == this->_functions.end()) {
    std::get<ERR_CODE>(retValue) = STATUS::NOT_FOUND_PAIR_ERR;
    std::get<DATA>(retValue) = "Not found pair (" + conv + ", " + node + ")";
    return retValue;
  }

  auto isolate = this->_convs[conv];

  {
    v8::Locker locker(isolate);

    // work with functions only after isolate has been locked
    auto pFunc = it->second;
    if (pFunc.IsEmpty()) {
      std::get<ERR_CODE>(retValue) = STATUS::NOT_FUNCTION_ERR;
      std::get<DATA>(retValue) = "Pair (conv, node) does not contain compiled function.";
      return retValue;
    }

    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope scope(isolate);

    v8::TryCatch try_catch(isolate);

    auto context =
      v8::Local<v8::Context>::New(isolate, this->_isolatesData[isolate]->getPContext());

    v8::Context::Scope context_scope(context);

    v8::MaybeLocal<v8::Value> jsonData =
      v8::JSON::Parse(isolate, v8::String::NewFromUtf8(isolate, data));

    if (jsonData.IsEmpty()) {
      std::get<ERR_CODE>(retValue) = STATUS::BAD_INPUT_ERR;
      std::get<DATA>(retValue) = "Error during parse JSON.";
      return retValue;
    }

    v8::Local<v8::Object> obj = jsonData.ToLocalChecked()->ToObject();
    v8::Local<v8::Value> args[] = { obj };

    auto func =
      v8::Local<v8::Function>::New(isolate, this->_functions[std::make_pair(conv, node)]);

    const auto now = std::chrono::system_clock::now();
    const auto currentTime =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

    // shared lock to be able to access _timing from different threads
    // without any delay.
    this->_timeCheckerMutex.lock_shared();
      this->_timing[threadId] =
        std::make_shared<V8Runner::ScriptWorkTime>(true, isolate, currentTime);
    this->_timeCheckerMutex.unlock_shared();

    v8::Local<v8::Value> res = func->Call(context->Global(), 1, args);

    _timing[threadId]->isWorking = false;

    if (try_catch.HasCaught()) {
      if (try_catch.HasTerminated()) {
        std::get<ERR_CODE>(retValue) = STATUS::SCRIPT_TERMINATED_ERR;
        std::get<DATA>(retValue) = "Script has been terminated.";
      } else {
        std::get<ERR_CODE>(retValue) = STATUS::SCRIPT_RUNTIME_ERR;
        std::get<DATA>(retValue) = V8Runner::_makeTryCatchError(try_catch);
      }
      return retValue;
    }

    std::get<ERR_CODE>(retValue) = STATUS::NO_ERR;
    std::get<DATA>(retValue) = V8Runner::_jsonStr(isolate, res);
  }

  return retValue;
}


void V8Runner::_timeCheckerFunc() {

  using namespace std::chrono;

  auto now = system_clock::now();
  auto currentTime = duration_cast<milliseconds>(now.time_since_epoch());

  while (this->_timeCheckerWatch) {

    // here we lock to write
    // cuz we really need to iterate over all isolates
    this->_timeCheckerMutex.lock();

    for (auto &item: this->_timing) {
      if (item && item->isWorking) {
        auto threadLaunchTime = item->started;
        auto timeExecution = std::size_t((currentTime - threadLaunchTime).count());

        if (timeExecution > this->_maxExecutionTime) {
          if (item->isolate->IsInUse()) {
            item->isolate->TerminateExecution();
            item->isolate->DiscardThreadSpecificMetadata();
          }
        }
      }
    }

    // access _timeCheckerSleepTime under the mutex
    auto timeSpeelFor = this->_timeCheckerSleepTime;

    this->_timeCheckerMutex.unlock();

    std::this_thread::sleep_for (milliseconds(timeSpeelFor));

    now = system_clock::now();
    currentTime = duration_cast<milliseconds>(now.time_since_epoch());
  }

}

void V8Runner::cleanData() {

  std::unique_lock<std::shared_mutex> lock(this->_compileMutex);

  // clean compiled functions
  for (auto& kv: this->_functions) {
    kv.second.Reset();
  }

  this->_functions.clear();
  this->_convs.clear();

}

void V8Runner::_Print(const v8::FunctionCallbackInfo<v8::Value>& args) {
  auto isolate = args.GetIsolate();

  bool first = true;
  for (int i = 0; i < args.Length(); i++) {
    if (first)
        first = false;
    else
        std::cout << " ";

    if (args[i]->IsObject()) {
      std::cout << V8Runner::_jsonStr(isolate, args[i]);
    } else {
      v8::String::Utf8Value str(args[i]);
      std::cout << *str;
    }
  }

  std::cout << std::endl;
}


void V8Runner::_Require(const v8::FunctionCallbackInfo<v8::Value>& args) {

  v8::Isolate* isolate = args.GetIsolate();

  v8::Locker locker(isolate);
  v8::TryCatch try_catch(isolate);

  v8::String::Utf8Value str(args[0]);
  const std::string fileName(*str);

  {
    std::shared_lock<std::shared_mutex> lock(V8Runner::_requireCacheMutex);

    auto file = V8Runner::_requireCache.find(fileName);

    if (file == V8Runner::_requireCache.end()) {
      auto error = "Error opening file: " + fileName;
      isolate->ThrowException(v8::String::NewFromUtf8(isolate, error.c_str()));
      try_catch.ReThrow();
      return;
    }

    const std::string& libContent = file->second;

    auto script = v8::String::NewFromUtf8(isolate, libContent.c_str());

    v8::Local<v8::Script> compiled_script;
    if (!v8::Script::Compile(isolate->GetCurrentContext(), script).ToLocal(&compiled_script)) {
      try_catch.ReThrow();
      return;
    }

    v8::Local<v8::Value> result;
    if (!compiled_script->Run(isolate->GetCurrentContext()).ToLocal(&result)) {
      try_catch.ReThrow();
      return;
    }
  }

}


std::tuple<int, std::string> V8Runner::getRequireCachedFile(const std::string& fileName) {

  std::tuple<int, std::string> retValue;

  std::shared_lock<std::shared_mutex> lock(V8Runner::_requireCacheMutex);

  auto file = V8Runner::_requireCache.find(fileName);

  if (file == V8Runner::_requireCache.end()) {
    std::get<ERR_CODE>(retValue) = STATUS::CACHED_REQUIRE_FILE_ERR;
    std::get<DATA>(retValue) = "Don't have cache for " + fileName;
  } else {
    std::get<ERR_CODE>(retValue) = STATUS::NO_ERR;
    std::get<DATA>(retValue) = file->second;
  }

  return retValue;

}


std::tuple<int, std::string> V8Runner::updateRequireCache(const std::string& fileName) {
  std::tuple<int, std::string> retValue;

  auto requireFile = V8Runner::_getRequireFile(V8Runner::_pathToLibs / fileName);

  if (std::get<ERR_CODE>(requireFile) != STATUS::NO_ERR) {
    std::cerr << "[ERROR] [updateRequireCache] "
              << "Code: " << std::get<ERR_CODE>(requireFile) << ", "
              << "Message: " << std::get<DATA>(requireFile)
              << std::endl;
    return requireFile;
  }

  {
    std::unique_lock<std::shared_mutex> lock(V8Runner::_requireCacheMutex);
    V8Runner::_requireCache[fileName] = std::get<DATA>(requireFile);
  }

  return retValue;
}

std::tuple<int, std::string> V8Runner::_getRequireFile(const std::string& fileName) {
  std::tuple<int, std::string> retValue;
  try {
    std::string libContent = V8Runner::_readFile(fileName);
    std::get<DATA>(retValue) = STATUS::NO_ERR;
    std::get<DATA>(retValue) = libContent;
  } catch(const std::exception& ex) {
    std::get<ERR_CODE>(retValue) = STATUS::CACHED_REQUIRE_FILE_ERR;
    std::get<DATA>(retValue) = ex.what();
  }
  return retValue;
}


// maybe switch to boost in a future
std::string V8Runner::_readFile(const std::string& fileName) {
  std::ifstream file(fileName);
  if (file) {
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  } else {
    throw std::runtime_error("Error opening file: " + fileName);
  }
}

// Stringify V8 value to JSON
// return empty string for empty value
std::string V8Runner::_jsonStr(
  v8::Isolate* isolate,
  v8::Handle<v8::Value> value) {

  if (value.IsEmpty())
  {
      return std::string();
  }

  v8::Local<v8::Object> json = isolate->GetCurrentContext()->
      Global()->Get(v8::String::NewFromUtf8(isolate, "JSON"))->ToObject();
  v8::Local<v8::Function> stringify =
    json->Get(v8::String::NewFromUtf8(isolate, "stringify")).As<v8::Function>();

  v8::Local<v8::Value> result = stringify->Call(json, 1, &value);
  const v8::String::Utf8Value str(result);

  return std::string(*str, str.length());
}

std::string V8Runner::_makeTryCatchError(const v8::TryCatch& try_catch) {

  v8::String::Utf8Value err(try_catch.Exception());
  v8::String::Utf8Value errLine(try_catch.Message()->GetSourceLine());
  const int errColumn = try_catch.Message()->GetStartColumn();

  std::stringstream error;

  error << "[" << *err << "] " << *errLine << ": " << std::to_string(errColumn);

  return error.str();

}

std::vector<std::string> splitString(const std::string& str, const auto& separator) {
  std::vector<std::string> parts;
  std::istringstream f(str);
  std::string s;
  while (getline(f, s, separator)) {
      parts.push_back(s);
  }
  return parts;
}
