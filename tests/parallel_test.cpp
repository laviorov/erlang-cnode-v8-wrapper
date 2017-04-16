#include <iostream>
#include <vector>
#include <vector>
#include <random>
#include <omp.h>

#include <json.hpp>

#include "v8runner.h"

using json = nlohmann::json;

typedef std::pair<std::string, std::string> pair;

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
      data.a += 1;
      return data;
    })
  )SCRIPT";

  const std::string bigJSONs = readFile("./memory_tests/big.json");

  fs::path pathToLibs(argv[1]);

  const std::size_t maxExecutionTime = 1000; // milliseconds
  const std::size_t timeCheckerSleepTime = 500; // milliseconds
  const std::size_t maxRAMAvailable = std::stoi(argv[2]);
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

  auto numberOfConvs = std::stoi(argv[3]);
  auto numberOfNodes = std::stoi(argv[4]);

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

  int N = 100000000;

  #pragma omp parallel for num_threads(threadsCount)
  for (int i = 0; i < N; i++) {
    auto pair_index = getRandomIndex(pairs.size() - 1);
    auto pair = pairs[pair_index];

    auto command_index = getRandomIndex(commands.size() - 1);
    auto command = commands[command_index];

    auto threadId = omp_get_num_threads();

    if (command == "check") {
      auto res = v8->checkCode(src.c_str(), bigJSONs.c_str(), threadId);
      CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());
    } else if (command == "compile") {
      auto res = v8->compile(pair.first.c_str(), pair.second.c_str(), src.c_str());
      CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());
    } else if (command == "run") {
      auto res = v8->run(pair.first.c_str(), pair.second.c_str(), bigJSONs.c_str(), threadId);
      CHECK(
        std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR ||
        std::get<0>(res) == pb::V8Runner::STATUS::NOT_FOUND_PAIR_ERR ||
        std::get<0>(res) == pb::V8Runner::STATUS::NOT_FUNCTION_ERR,
        std::get<1>(res).c_str()
      );
    } else {
      auto res = v8->remove(pair.first.c_str(), pair.second.c_str());
      CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());
    }
  }

  return 0;
}
