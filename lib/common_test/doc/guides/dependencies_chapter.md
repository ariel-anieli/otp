<!--
%CopyrightBegin%

SPDX-License-Identifier: Apache-2.0

Copyright Ericsson AB 2023-2025. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

%CopyrightEnd%
-->
# Dependencies between Test Cases and Suites

## General

When creating test suites, it is strongly recommended to not create dependencies
between test cases, that is, letting test cases depend on the result of previous
test cases. There are various reasons for this, such as, the following:

- It makes it impossible to run test cases individually.
- It makes it impossible to run test cases in a different order.
- It makes debugging difficult (as a fault can be the result of a problem in a
  different test case than the one failing).
- There are no good and explicit ways to declare dependencies, so it can be
  difficult to see and understand these in test suite code and in test logs.
- Extending, restructuring, and maintaining test suites with test case
  dependencies is difficult.

There are often sufficient means to work around the need for test case
dependencies. Generally, the problem is related to the state of the System Under
Test (SUT). The action of one test case can change the system state. For some
other test case to run properly, this new state must be known.

Instead of passing data between test cases, it is recommended that the test
cases read the state from the SUT and perform assertions (that is, let the test
case run if the state is as expected, otherwise reset or fail). It is also
recommended to use the state to set variables necessary for the test case to
execute properly. Common actions can often be implemented as library functions
for test cases to call to set the SUT in a required state. (Such common actions
can also be separately tested, if necessary, to ensure that they work as
expected). It is sometimes also possible, but not always desirable, to group
tests together in one test case, that is, let a test case perform a "scenario"
test (a test consisting of subtests).

Consider, for example, a server application under test. The following
functionality is to be tested:

- Starting the server
- Configuring the server
- Connecting a client to the server
- Disconnecting a client from the server
- Stopping the server

There are obvious dependencies between the listed functions. The server cannot
be configured if it has not first been started, a client cannot be connected
until the server is properly configured, and so on. If we want to have one test
case for each function, we might be tempted to try to always run the test cases
in the stated order and carry possible data (identities, handles, and so on)
between the cases and therefore introduce dependencies between them.

To avoid this, we can consider starting and stopping the server for every test.
We can thus implement the start and stop action as common functions to be called
from [`init_per_testcase`](`c:ct_suite:init_per_testcase/2`) and
[`end_per_testcase`](`c:ct_suite:end_per_testcase/2`). (Remember to test the
start and stop functionality separately.) The configuration can also be
implemented as a common function, maybe grouped with the start function.
Finally, the testing of connecting and disconnecting a client can be grouped
into one test case. The resulting suite can look as follows:

```erlang
-module(my_server_SUITE).
-compile(export_all).
-include_lib("ct.hrl").

%%% init and end functions...

suite() -> [{require,my_server_cfg}].

init_per_testcase(start_and_stop, Config) ->
    Config;

init_per_testcase(config, Config) ->
    [{server_pid,start_server()} | Config];

init_per_testcase(_, Config) ->
    ServerPid = start_server(),
    configure_server(),
    [{server_pid,ServerPid} | Config].

end_per_testcase(start_and_stop, _) ->
    ok;

end_per_testcase(_, Config) ->
    ServerPid = proplists:get_value(server_pid, Config),
    stop_server(ServerPid).

%%% test cases...

all() -> [start_and_stop, config, connect_and_disconnect].

%% test that starting and stopping works
start_and_stop(_) ->
    ServerPid = start_server(),
    stop_server(ServerPid).

%% configuration test
config(Config) ->
    ServerPid = proplists:get_value(server_pid, Config),
    configure_server(ServerPid).

%% test connecting and disconnecting client
connect_and_disconnect(Config) ->
    ServerPid = proplists:get_value(server_pid, Config),
    {ok,SessionId} = my_server:connect(ServerPid),
    ok = my_server:disconnect(ServerPid, SessionId).

%%% common functions...

start_server() ->
    {ok,ServerPid} = my_server:start(),
    ServerPid.

stop_server(ServerPid) ->
    ok = my_server:stop(),
    ok.

configure_server(ServerPid) ->
    ServerCfgData = ct:get_config(my_server_cfg),
    ok = my_server:configure(ServerPid, ServerCfgData),
    ok.
```

[](){: #save_config }

## Saving Configuration Data

Sometimes it is impossible, or infeasible, to implement independent test cases.
Maybe it is not possible to read the SUT state. Maybe resetting the SUT is
impossible and it takes too long time to restart the system. In situations where
test case dependency is necessary, CT offers a structured way to carry data from
one test case to the next. The same mechanism can also be used to carry data
from one test suite to the next.

The mechanism for passing data is called `save_config`. The idea is that one
test case (or suite) can save the current value of `Config`, or any list of
key-value tuples, so that the next executing test case (or test suite) can read
it. The configuration data is not saved permanently but can only be passed from
one case (or suite) to the next.

To save `Config` data, return tuple `{save_config,ConfigList}` from
`end_per_testcase` or from the main test case function.

To read data saved by a previous test case, use `proplists:get_value` with a
`saved_config` key as follows:

`{Saver,ConfigList} = proplists:get_value(saved_config, Config)`

`Saver` (`t:atom/0`) is the name of the previous test case (where the data was
saved). The `proplists:get_value` function can be used to extract particular
data also from the recalled `ConfigList`. It is strongly recommended that
`Saver` is always matched to the expected name of the saving test case. This
way, problems because of restructuring of the test suite can be avoided. Also,
it makes the dependency more explicit and the test suite easier to read and
maintain.

To pass data from one test suite to another, the same mechanism is used. The
data is to be saved by finction [`end_per_suite`](`c:ct_suite:end_per_suite/1`)
and read by function [`init_per_suite`](`c:ct_suite:init_per_suite/1`) in the
suite that follows. When passing data between suites, `Saver` carries the name
of the test suite.

_Example:_

```erlang
-module(server_b_SUITE).
-compile(export_all).
-include_lib("ct.hrl").

%%% init and end functions...

init_per_suite(Config) ->
    %% read config saved by previous test suite
    {server_a_SUITE,OldConfig} = proplists:get_value(saved_config, Config),
    %% extract server identity (comes from server_a_SUITE)
    ServerId = proplists:get_value(server_id, OldConfig),
    SessionId = connect_to_server(ServerId),
    [{ids,{ServerId,SessionId}} | Config].

end_per_suite(Config) ->
    %% save config for server_c_SUITE (session_id and server_id)
    {save_config,Config}

%%% test cases...

all() -> [allocate, deallocate].

allocate(Config) ->
    {ServerId,SessionId} = proplists:get_value(ids, Config),
    {ok,Handle} = allocate_resource(ServerId, SessionId),
    %% save handle for deallocation test
    NewConfig = [{handle,Handle}],
    {save_config,NewConfig}.

deallocate(Config) ->
    {ServerId,SessionId} = proplists:get_value(ids, Config),
    {allocate,OldConfig} = proplists:get_value(saved_config, Config),
    Handle = proplists:get_value(handle, OldConfig),
    ok = deallocate_resource(ServerId, SessionId, Handle).
```

To save `Config` data from a test case that is to be skipped, return tuple
`{skip_and_save,Reason,ConfigList}`.

The result is that the test case is skipped with `Reason` printed to the log
file (as described earlier) and `ConfigList` is saved for the next test case.
`ConfigList` can be read using `proplists:get_value(saved_config, Config)`, as
described earlier. `skip_and_save` can also be returned from `init_per_suite`.
In this case, the saved data can be read by `init_per_suite` in the suite that
follows.

## Sequences

Sometimes test cases depend on each other so that if one case fails, the
following tests are not to be executed. Typically, if the `save_config` facility
is used and a test case that is expected to save data crashes, the following
case cannot run. `Common Test` offers a way to declare such dependencies, called
sequences.

A sequence of test cases is defined as a test case group with a `sequence`
property. Test case groups are defined through function `groups/0` in the test
suite (for details, see section
[Test Case Groups](write_test_chapter.md#test_case_groups).

For example, to ensure that if `allocate` in `server_b_SUITE` crashes,
`deallocate` is skipped, the following sequence can be defined:

```erlang
groups() -> [{alloc_and_dealloc, [sequence], [alloc,dealloc]}].
```

Assume that the suite contains the test case `get_resource_status` that is
independent of the other two cases, then function `all` can look as follows:

```erlang
all() -> [{group,alloc_and_dealloc}, get_resource_status].
```

If `alloc` succeeds, `dealloc` is also executed. If `alloc` fails however,
`dealloc` is not executed but marked as `SKIPPED` in the HTML log.
`get_resource_status` runs no matter what happens to the `alloc_and_dealloc`
cases.

Test cases in a sequence are executed in order until all succeed or one fails.
If one fails, all following cases in the sequence are skipped. The cases in the
sequence that have succeeded up to that point are reported as successful in the
log. Any number of sequences can be specified.

_Example:_

```erlang
groups() -> [{scenarioA, [sequence], [testA1, testA2]},
             {scenarioB, [sequence], [testB1, testB2, testB3]}].

all() -> [test1,
          test2,
          {group,scenarioA},
          test3,
          {group,scenarioB},
          test4].
```

A sequence group can have subgroups. Such subgroups can have any property, that
is, they are not required to also be sequences. If you want the status of the
subgroup to affect the sequence on the level above, return
`{return_group_result,Status}` from
[`end_per_group/2`](`c:ct_suite:end_per_group/2`), as described in section
[Repeated Groups](write_test_chapter.md#repeated_groups) in Writing Test Suites.
A failed subgroup (`Status == failed`) causes the execution of a sequence to
fail in the same way a test case does.
