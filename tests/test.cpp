#include <memory>
#include <vector>
#include <random>
#include <omp.h>

#include <gtest/gtest.h>
#include <json.hpp>

#include "v8runner.h"
#include "threadpool.h"

using json = nlohmann::json;

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

typedef pb::concurrent::ThreadPool<
  std::function<void(std::size_t)>
> ThreadPool;

namespace pb {

  std::unique_ptr<pb::V8Runner> v8;
  std::unique_ptr<ThreadPool> pool;

  struct V8RunnerTest : public ::testing::Test {

    std::string defaultCode = R"SCRIPT(
      (function(data) {
        require('libs/moment.js');
        for(let i = 0; i < 1000; i++) data.arr.push(new Date());
        data.date = moment().format('MMMM Do YYYY, h:mm:ss a');
        data.a += 1;
        return data;
      })
    )SCRIPT";

    typedef std::pair<std::string, std::string> pair;

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

    void TearDown() override {
      v8->cleanData();
    }
  };

  TEST_F(V8RunnerTest, RunNotCompiledPair) {

    auto res = v8->run("conv", "node", "{\"a\": 1, \"b\": 2, \"arr\": [1, 2, 3]}");

    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NOT_FOUND_PAIR_ERR)
      << std::get<1>(res);
  }

  TEST_F(V8RunnerTest, CorrectCompile) {

    auto res = v8->compile(
      "conv",
      "node",
      R"SCRIPT(
        (function(data) {
          require('libs/moment.js');
          moment();
          data.a += 1;
          return data;
        })
      )SCRIPT"
    );

    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
      << std::get<1>(res);

    res = v8->compile(
      "conv",
      "node1",
      R"SCRIPT(
        (function(data) {
          require('libs/moment.js');
          moment();
          data.b += 1;
          return data;
        })
      )SCRIPT"
    );

    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
      << std::get<1>(res);

    res = v8->run("conv", "node", "{\"a\": 1, \"b\": 2, \"arr\": [1, 2, 3]}");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
      << std::get<1>(res);

    auto j_res = json::parse(std::get<1>(res));
    ASSERT_EQ(j_res["a"], 2) << "testCompile doesn't work";
    ASSERT_EQ(j_res["b"], 2) << "testCompile doesn't work";

    res = v8->run("conv", "node1", "{\"a\": 1, \"b\": 2, \"arr\": [1, 2, 3]}");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
      << std::get<1>(res);

    j_res = json::parse(std::get<1>(res));
    ASSERT_EQ(j_res["a"], 1)
      << "testCompile doesn't work";
    ASSERT_EQ(j_res["b"], 3)
      << "testCompile doesn't work";

  }

  TEST_F(V8RunnerTest, CompileError) {
    auto res = v8->compile(
      "conv",
      "node",
      R"SCRIPT(
        (function(data) {
          require('libs/moment.js');
          for(lettttt i = 0; i < 1000; i++) data.arr.push(new Date());
          data.date = moment().format('MMMM Do YYYY, h:mm:ss a');
          data.a += 1;
          return data;
        })
      )SCRIPT"
    );

    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::COMPILE_ERR)
      << std::get<1>(res);
  }

  TEST_F(V8RunnerTest, RecompilePair) {
    auto res = v8->compile(
      "conv",
      "node",
      R"SCRIPT(
        (function(data) {
          require('libs/moment.js');
          for(let i = 0; i < 1000; i++) data.arr.push(new Date());
          data.date = moment().format('MMMM Do YYYY, h:mm:ss a');
          data.a += 1;
          return data;
        })
      )SCRIPT"
    );

    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
      << std::get<1>(res);

    res = v8->run("conv", "node", "{\"a\": 1, \"b\": 2, \"arr\": [1, 2, 3]}");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
      << std::get<1>(res);

    auto j_res = json::parse(std::get<1>(res));
    ASSERT_EQ(j_res["arr"].size(), 1003)
      << "testRecompilePair arr push doesn't work";
    ASSERT_EQ(j_res["a"], 2)
      << "testRecompilePair increment doesn't work";
    ASSERT_EQ(j_res["b"], 2)
      << "testRecompilePair increment doesn't work";

    // recompile

    res = v8->compile(
      "conv",
      "node",
      R"SCRIPT(
        (function(data) {
          require('libs/moment.js');
          for(let i = 0; i < 2000; i++) data.arr.push(new Date());
          data.date = moment().format('MMMM Do YYYY, h:mm:ss a');
          data.b += 1;
          return data;
        })
      )SCRIPT"
    );
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
      << std::get<1>(res);

    res = v8->run("conv", "node", "{\"a\": 1, \"b\": 2, \"arr\": [1, 2, 3]}");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
      << std::get<1>(res);

    j_res = json::parse(std::get<1>(res));
    ASSERT_EQ(j_res["arr"].size(), 2003)
      << "testRecompilePair arr push doesn't work";
    ASSERT_EQ(j_res["a"], 1)
      << "testRecompilePair increment doesn't work";
    ASSERT_EQ(j_res["b"], 3)
      << "testRecompilePair increment doesn't work";
  }

  TEST_F(V8RunnerTest, RemovePair) {
    v8->compile(
      "conv",
      "node",
      R"SCRIPT(
        (function(data) {
          require('libs/moment.js');
          for(let i = 0; i < 1000; i++) data.arr.push(new Date());
          data.date = moment().format('MMMM Do YYYY, h:mm:ss a');
          data.a += 1;
          return data;
        })
      )SCRIPT"
    );

    v8->compile(
      "conv",
      "node1",
      R"SCRIPT(
        (function(data) {
          require('libs/moment.js');
          for(let i = 0; i < 1000; i++) data.arr.push(new Date());
          data.date = moment().format('MMMM Do YYYY, h:mm:ss a');
          data.a += 1;
          return data;
        })
      )SCRIPT"
    );

    auto res = v8->remove("conv", "node");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::NO_ERR)
      << std::get<1>(res);

    res = v8->run("conv", "node", "{\"a\": 1, \"b\": 2, \"arr\": [1, 2, 3]}");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NOT_FUNCTION_ERR)
      << std::get<1>(res);

    res = v8->run("conv", "node1", "{\"a\": 1, \"b\": 2, \"arr\": [1, 2, 3]}");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
      << std::get<1>(res);
  }

  TEST_F(V8RunnerTest, CheckCode) {
    auto res = v8->checkCode(
      R"SCRIPT(
        (function(data) {
          require('libs/moment.js');
          for(let i = 0; i < 1000; i++) data.arr.push(new Date());
          data.date = moment().format('MMMM Do YYYY, h:mm:ss a');
          data.a += 1;
          return data;
        })
      )SCRIPT",
      "{}"
    );

    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
      << std::get<1>(res);

    res = v8->checkCode(
      R"SCRIPT(
        (function(data) {
          require('libs/moment.js');
          for(letttt i = 0; i < 1000; i++) data.arr.push(new Date());
          data.date = moment().format('MMMM Do YYYY, h:mm:ss a');
          data.a += 1;
          return data;
        })
      )SCRIPT",
      "{}"
    );

    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::COMPILE_ERR)
      << std::get<1>(res);

    // TODO: add runtime check
  }

  TEST_F(V8RunnerTest, BadInput) {
    v8->compile(
      "conv",
      "node",
      R"SCRIPT(
        (function(data) {
          require('libs/moment.js');
          for(let i = 0; i < 1000; i++) data.arr.push(new Date());
          data.date = moment().format('MMMM Do YYYY, h:mm:ss a');
          data.a += 1;
          return data;
        })
      )SCRIPT"
    );

    auto res = v8->run("conv", "node", "{some invalid json}");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::BAD_INPUT_ERR)
      << std::get<1>(res);
  }

  TEST_F(V8RunnerTest, RuntimeError) {
    v8->compile(
      "conv",
      "node",
      R"SCRIPT(
        (function(data) {
          require('libs/somebesteverlib.js');
          for(let i = 0; i < 1000; i++) data.arr.push(new Date());
          data.date = moment().format('MMMM Do YYYY, h:mm:ss a');
          data.a += 1;
          return data;
        })
      )SCRIPT"
    );

    auto res = v8->run("conv", "node", "{}");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::SCRIPT_RUNTIME_ERR)
      << std::get<1>(res);

    v8->compile(
      "conv",
      "node",
      R"SCRIPT(
        (function(data) {
          require('libs/moment.js');
          for(let i = 0; i < 1000; i++) data.arr.push(new Date());
          data.date = mommmmmmmmment().format('MMMM Do YYYY, h:mm:ss a');
          data.a += 1;
          return data;
        })
      )SCRIPT"
    );

    res = v8->run("conv", "node", "{}");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::SCRIPT_RUNTIME_ERR)
      << std::get<1>(res);

    v8->compile(
      "conv",
      "node",
      R"SCRIPT(
        (function(data) {
          data.undefined.undefined = 1;
          return data;
        })
      )SCRIPT"
    );

    res = v8->run("conv", "node", "{}");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::SCRIPT_RUNTIME_ERR)
      << std::get<1>(res);
  }

  TEST_F(V8RunnerTest, ScriptTerminatedError) {
    v8->compile(
      "conv",
      "node",
      R"SCRIPT(
        (function(data) {
          for (;;);
          return data;
        })
      )SCRIPT"
    );

    auto res = v8->run("conv", "node", "{}");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::SCRIPT_TERMINATED_ERR)
      << std::get<1>(res);
  }

  TEST_F(V8RunnerTest, GetRequireCachedFile) {
    auto res = v8->getRequireCachedFile("libs/moment.js");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
      << std::get<1>(res);

    res = v8->getRequireCachedFile("libs/momen.js");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::CACHED_REQUIRE_FILE_ERR)
      << std::get<1>(res);
  }

  TEST_F(V8RunnerTest, UpdateRequireCachedFile) {
    auto res = v8->updateRequireCache("libs/moment.js");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
      << std::get<1>(res);

    res = v8->updateRequireCache("libs/momen.js");
    ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::CACHED_REQUIRE_FILE_ERR)
      << std::get<1>(res);
  }

  TEST_F(V8RunnerTest, NumberOfConvsAndNodes) {
    v8->compile(
      "conv",
      "node",
      R"SCRIPT(
        (function(data) {
          require('libs/somebesteverlib.js');
          for(let i = 0; i < 1000; i++) data.arr.push(new Date());
          data.date = moment().format('MMMM Do YYYY, h:mm:ss a');
          data.a += 1;
          return data;
        })
      )SCRIPT"
    );

    v8->compile(
      "conv",
      "node1",
      R"SCRIPT(
        (function(data) {
          require('libs/somebesteverlib.js');
          for(let i = 0; i < 1000; i++) data.arr.push(new Date());
          data.date = moment().format('MMMM Do YYYY, h:mm:ss a');
          data.a += 1;
          return data;
        })
      )SCRIPT"
    );

    v8->compile(
      "conv1",
      "node",
      R"SCRIPT(
        (function(data) {
          require('libs/somebesteverlib.js');
          for(let i = 0; i < 1000; i++) data.arr.push(new Date());
          data.date = moment().format('MMMM Do YYYY, h:mm:ss a');
          data.a += 1;
          return data;
        })
      )SCRIPT"
    );

    ASSERT_EQ(v8->convs_count(), 2);
    ASSERT_EQ(v8->nodes_count(), 3);
  }

  TEST_F(V8RunnerTest, CompileAndRunBunchOfPairs) {

    const int numberOfIterations = 2;
    const int numberOfConvs = 50;
    const int numberOfNodes = 50;

    std::vector<pair> pairs = generatePairs(numberOfConvs, numberOfNodes);

    for (int i = 0; i < pairs.size(); i++) {
      auto pair = pairs[i];
      auto conv = pair.first;
      auto node = pair.second;

      auto res = v8->compile(
        conv.c_str(),
        node.c_str(),
        ("(function(data) { require('libs/moment.js'); moment(); data.i = " + std::to_string(i) + "; return data; })").c_str()
      );
      ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
        << std::get<1>(res);
    }

    for (int i = 0; i < pairs.size(); i++) {
      auto pair = pairs[i];
      auto conv = pair.first;
      auto node = pair.second;

      for (int j = 0; j < numberOfIterations; j++) {
        auto res = v8->run(conv.c_str(), node.c_str(), "{}");
        ASSERT_EQ(std::get<0>(res), pb::V8Runner::STATUS::NO_ERR)
          << std::get<1>(res);

        auto j_res = json::parse(std::get<1>(res));
        ASSERT_EQ(j_res["i"], i)
          << "testCompileAndRunBunchOfPairs does not work";
      }
    }
  }

  TEST_F(V8RunnerTest, CompileAndRunBunchOfPairsInParallel) {
    const int numberOfConvs = 50;
    const int numberOfNodes = 50;

    std::vector<pair> pairs = generatePairs(numberOfConvs, numberOfNodes);

    #pragma omp parallel for num_threads(v8->isolates_count())
    for (int i = 0; i < pairs.size(); i++) {
      auto pair = pairs[i];
      auto conv = pair.first;
      auto node = pair.second;

      auto res = v8->compile(
        conv.c_str(),
        node.c_str(),
        ("(function(data) { require('libs/moment.js'); moment(); data.i = " + std::to_string(i) + "; return data; })").c_str()
      );

      // OpenMP does not allow to use ASSERT_EQ
      CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());
    }

    #pragma omp parallel for num_threads(v8->isolates_count())
    for (int i = 0; i < pairs.size(); i++) {

      int tid = omp_get_thread_num();

      auto res = v8->run(pairs[i].first.c_str(), pairs[i].second.c_str(), "{}", tid);
      CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());

      auto j_res = json::parse(std::get<1>(res));
      CHECK(j_res["i"] == i, "testCompileAndRunBunchOfPairsParallel does not work");
    }
  }

  TEST_F(V8RunnerTest, ThreadPoolTest) {
    const int numberOfConvs = 100;
    const int numberOfNodes = 100;

    std::vector<pair> pairs = generatePairs(numberOfConvs, numberOfNodes);

    for(const auto& pair: pairs) {
      auto res = v8->compile(
        pair.first.c_str(),
        pair.second.c_str(),
        defaultCode.c_str()
      );
      CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());
    }

    const int numThreads = 4;

    #pragma omp parallel for num_threads(numThreads)
    for (int i = 0; i < pairs.size(); i++) {
      int tid = omp_get_thread_num();
      switch(tid) {
        case 0:
          pool->addJob(1, [pair=pairs[i], this](std::size_t threadNum){
            auto res = v8->compile(
              pair.first.c_str(),
              pair.second.c_str(),
              defaultCode.c_str()
            );
            CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());
          });
          break;
        case 1:
          pool->addJob(0, [pair=pairs[i]](std::size_t threadNum){
            auto res = v8->run(
              pair.first.c_str(),
              pair.second.c_str(),
              "{\"a\": 1, \"b\": 2, \"arr\": [1, 2, 3]}"
            );
            CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());
          });
          break;
        case 2:
          pool->addJob(1, [pair=pairs[i]](std::size_t threadNum){
            for (int i = 0; i < 10000; i++);
            auto res = v8->remove(
              pair.first.c_str(),
              pair.second.c_str()
            );
            CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());
          });
          break;
        case 3:
          pool->addJob(0, [this](std::size_t threadNum){
            auto res = v8->checkCode(
              defaultCode.c_str(),
              "{}"
            );
            CHECK(std::get<0>(res) == pb::V8Runner::STATUS::NO_ERR, std::get<1>(res).c_str());
          });
          break;
      }
    }

    pool->joinAll(true);

    auto amountOfJobs = pool->getAmountOfDoneJobs();
    CHECK(amountOfJobs == pairs.size(), "getAmountOfJobs incorrect");
  }

} // namespace

int main(int argc, char** argv) {

  // absolute path here
  fs::path pathToLibs(argv[1]);

  const std::size_t threadsCount = 4;
  const std::size_t maxExecutionTime = 3000; // milliseconds
  const std::size_t timeCheckerSleepTime = 500; // milliseconds
  const std::size_t maxRAMAvailable = std::stoi(argv[2]);

  pb::v8 = std::make_unique<pb::V8Runner>(
    argc,
    argv,
    pathToLibs,
    maxExecutionTime,
    maxRAMAvailable,
    timeCheckerSleepTime,
    threadsCount
  );

  pb::pool = std::make_unique<ThreadPool>(threadsCount);

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
