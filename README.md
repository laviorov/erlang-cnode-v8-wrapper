## Deps
  ### 1. You need to have icu-56 lib.

## Compile gtest
  "g++ -I gtest/include/ -I gtest/ -c gtest/src/gtest-all.cc",__
  "ar -rv lib/libgtest.a gtest-all.o",__
  "rm gtest-all.o"__

## Todo
  ### 1. Reimplement threadpool.

## Docs

### Start Erlang shell
  erl -name cnode@localhost.localdomain -setcookie cookie
### Start CNode
  valgrind --leak-check=full --show-leak-kinds=all  ./bin/cserver 16 1 cnode@localhost.localdomain cookie

### get_statistics
  {any, 'c1@localhost'} ! {call, self(), {TIMESTAMP_IN_MILLISECONDS, get_statistics}}.
### get_max_diff_time
  {any, 'c1@localhost'} ! {call, self(), {TIMESTAMP_IN_MILLISECONDS, get_max_diff_time}}.
### set_max_diff_time
  {any, 'c1@localhost'} ! {call, self(), {TIMESTAMP_IN_MILLISECONDS, set_max_diff_time, 9999}}.
### get_max_time_exec_threshold
  {any, 'c1@localhost'} ! {call, self(), {TIMESTAMP_IN_MILLISECONDS, get_max_time_exec_threshold}}.
### set_max_time_exec_threshold
  {any, 'c1@localhost'} ! {call, self(), {TIMESTAMP_IN_MILLISECONDS, set_max_time_exec_threshold, 2000}}.
### get_require_cache_file
  {any, 'c1@localhost'} ! {call, self(), {TIMESTAMP_IN_MILLISECONDS, get_require_cache_file, <<"libs/moment.js">>}}.
### update_require_cache_file
  {any, 'c1@localhost'} ! {call, self(), {TIMESTAMP_IN_MILLISECONDS, update_require_cache_file, <<"libs/moment.js">>}}.
### get_priorities
  {any, 'c1@localhost'} ! {call, self(), {TIMESTAMP_IN_MILLISECONDS, get_priorities}}.
### set_priority
  {any, 'c1@localhost'} ! {call, self(), {1492190122000, set_priority, <<"run">>, 2}}.
### remove_priority
  {any, 'c1@localhost'} ! {call, self(), {1492190122000, remove_priority, <<"run">>}}.

### run
  {any, 'c1@localhost'} ! {call, self(), {TIMESTAMP_IN_MILLISECONDS, run, <<"1">>, <<"test">>, <<"{\"b\": 1}">>}}.
### compile
  {any, 'c1@localhost'} ! {call, self(), {TIMESTAMP_IN_MILLISECONDS, compile, <<"1">>, <<"test">>, <<"(function(data){ while(true); data.a += 1; return data; })">>}}.
### remove
  {any, 'c1@localhost'} ! {call, self(), {TIMESTAMP_IN_MILLISECONDS, remove, <<"1">>, <<"test">>}}.
### check_code
  {any, 'c1@localhost'} ! {call, self(), {TIMESTAMP_IN_MILLISECONDS, check_code, <<"(function(data){ return data; })">>, <<"{\"b\": 1}">>}}.
