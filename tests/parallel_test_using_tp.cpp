#include <iostream>
#include <vector>
#include <map>
#include <random>
#include <omp.h>

#include <json.hpp>

#include "v8runner.h"
#include "threadpool.h"

using json = nlohmann::json;

typedef std::pair<std::string, std::string> pair;

typedef pb::concurrent::ThreadPool<
  std::function<void(std::size_t)>
> ThreadPool;


#define CHECK(Expr, Msg) __CHECK(#Expr, Expr, __FILE__, __LINE__, Msg)

void __CHECK(const char* expr_str, bool expr, const char* file, int line, const char* msg)
{
    if (!expr)
    {
        std::cerr << "Assert failed:\t" << msg << "\n"
            << "Expected:\t" << expr_str << "\n"
            << "Source:\t\t" << file << ", line " << line << "\n";
        abort();
    }
}

std::vector<pair> generatePairs(const int& numberOfConvs, const int& numberOfNodes) {
  std::vector<pair> pairs;

  for (int i = 0; i < numberOfConvs; i++) {
    auto conv = "conv" + std::to_string(i);

    for (int j = 0; j < numberOfNodes; j++) {
      pairs.push_back(std::make_pair(conv, "node" + std::to_string(j)));
    }
  }

  return pairs;
}

int getRandomIndex(int maxSize) {
  std::random_device rd;     // only used once to initialise (seed) engine
  std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
  std::uniform_int_distribution<int> uni(0, maxSize); // guaranteed unbiased
  return uni(rng);
}

std::string readFile(const std::string& fileName) {
  std::ifstream file(fileName);
  if (file) {
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  } else {
    throw std::runtime_error("Error opening file: " + fileName);
  }
}

int main(int argc, char* argv[]) {

  const std::string src = R"SCRIPT(
    (function(data) {
      require('libs/moment.js');
      moment();
      data.forEach(item => item.a = 10);
      return data;
    })
  )SCRIPT";

  const std::string bigJSON = readFile("./data/intermediate.json");

  fs::path pathToLibs(argv[1]);

  const std::size_t maxExecutionTime = 1000; // milliseconds
  const std::size_t timeCheckerSleepTime = 500; // milliseconds
  const std::size_t maxRAMAvailable = std::stoi(argv[2]);
  const std::size_t maxThreadpoolQueueSize = std::stoi(argv[3]);
  const std::size_t threadsCount = 4;

  auto v8 = std::make_unique<pb::V8Runner>(
    argc,
    argv,
    pathToLibs,
    maxExecutionTime,
    maxRAMAvailable,
    timeCheckerSleepTime,
    threadsCount
  );

  auto pool = std::make_unique<ThreadPool>(threadsCount, maxThreadpoolQueueSize);

  auto numberOfConvs = std::stoi(argv[4]);
  auto numberOfNodes = std::stoi(argv[5]);

  std::array<std::string, 4> commands = {
    "check",
    "compile",
    "run",
    "remove"
  };

  auto pairs = generatePairs(numberOfConvs, numberOfNodes);

  for (const auto& pair: pairs) {
    auto res = v8->compile(pair.first.c_str(), pair.second.c_str(), src.c_str());
    CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());
  }

  std::cerr << "compiled " << pairs.size() << " pairs." << std::endl;

  int N = std::stoi(argv[6]);

  #pragma omp parallel for num_threads(threadsCount)
  for (int i = 0; i < N; i++) {
    auto pair_index = getRandomIndex(pairs.size() - 1);
    auto pair = pairs[pair_index];

    auto command_index = getRandomIndex(commands.size() - 1);
    auto command = commands[command_index];

    if (command == "compile") {

      while(
        !pool->addJob(1, [pair, src, &v8](std::size_t threadNum){
          auto res = v8->compile(
            pair.first.c_str(),
            pair.second.c_str(),
            src.c_str()
          );
          CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());
        })
      );

    } else if (command == "run") {

      while(
        !pool->addJob(0, [bigJSON, pair, &v8](std::size_t threadNum){
          auto res = v8->run(
            pair.first.c_str(),
            pair.second.c_str(),
            bigJSON.c_str()
          );
          CHECK(
            std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR ||
            std::get<0>(res) == pb::V8Runner::STATUS::NOT_FUNCTION_ERR,
            std::get<1>(res).c_str()
          );
        })
      );

    } else if (command == "remove") {

      while(
        !pool->addJob(1, [pair, &v8](std::size_t threadNum){
          auto res = v8->remove(
            pair.first.c_str(),
            pair.second.c_str()
          );
          CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());
        })
      );

    } else if (command == "check") {

      while(
        !pool->addJob(0, [src, &v8](std::size_t threadNum){
          auto res = v8->checkCode(
            src.c_str(),
            "{}"
          );
          CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());
        })
      );
    }

  } // end of for loop

  pool->joinAll();

  auto amountOfJobs = pool->getAmountOfDoneJobs();
  CHECK(amountOfJobs == N, "getAmountOfJobs incorrect");

  return 0;
}
