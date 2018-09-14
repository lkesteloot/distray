# Distrend

Distrend is a distributed rendering system. It doesn't do the rendering itself,
but runs any command-line renderer on a set of machines. It can also be used
for non-rendering batch tasks.

# Compiling

Build distrend using CMake:

    % mkdir build
    % cd build
    % cmake ..
    % make

It compiles on MacOS and Linux. You'll need the protobuf compiler
and library:

    % sudo apt-get install protobuf-compiler libprotobuf-dev

# Running

Distrend can be run in one of three modes: worker, proxy, and controller.
You must have one or more workers, zero or more proxies, and exactly
one controller.

Various input and output files can be copied to and from the workers,
but the renderer itself (or any executable) must already be there,
for security reasons. The executed binary must already have its
executable bits set, and this cannot be done through the copying
mechanism.

By default there is no access control, and if anyone knows that you're
running a proxy, then can connect to it and control your workers.
Use a password (specified with the `--password` flag) to limit who
can connect to proxies or to the controller.

## Worker

A worker launches the renderer itself. Run one worker on each machine
you want to use.

    % distrend worker [FLAGS] HOST[:PORT]

The `HOST` is the hostname of either a proxy or a controller.
The `PORT` defaults to 1120.

Flags are:

    --password PASSWORD Password to pass to proxies or the controller.
                        Defaults to an empty string.

## Proxy

A proxy lets both workers and controllers connect to it. It's useful
when neither the workers nor the controller can connect to each other.
For example, you may have some workers on an AWS EC2 machine and the
controller on a home machine, and it's easiest to run a proxy on
some public machine that you have access to.

    % distrend proxy [FLAGS]

Flags are:

    --password PASSWORD Password to expect from workers or the controller.
                        Defaults to an empty string.

## Controller

The controller tells the workers (optionally through the proxy) what
files to render. The controller can take incoming connections from
workers or make outgoing connections to proxies.

    % distrend controller [FLAGS] FRAMES EXEC [PARAMETERS...]

`FRAMES` is a frame range specification: `FIRST[,LAST[,STEP]]`,
where `STEP` defaults to 1 or -1 (depending on order of `FIRST` and
`LAST`) and `LAST` defaults to `FIRST`.

`EXEC` is the executable to run on each worker. The path must be
in the tree rooted at the current working directory of the worker. It
cannot start with a slash or contain two consecutive dots.

`PARAMETERS` are the parameters to pass to the executed binary.
Use `%d` or `%0Nd` for the frame number, where `N` is
a positive decimal integer that specifies field width.

Flags are:

    --proxy HOST        Proxy to connect to. Can be repeated.
    --in LOCAL REMOTE   Copy LOCAL file to REMOTE file. Can be repeated.
    --out REMOTE LOCAL  Copy REMOTE file to LOCAL file. Can be repeated.
    --password PASSWORD Password to expect from workers or to pass
                        to proxies. Defaults to an empty string.

Local files can be anywhere in the file system, but remote files must be
in the tree rooted at the current working directory of the worker. They
cannot start with a slash or contain two consecutive dots.

If either a local or a remote pathname includes the pattern `%d` or `%0Nd`,
where `N` is a positive decimal integer, then the copy will be performed every
frame. Otherwise it will be performed at the beginning or end of the entire
process.

The order of execution is:

1. Perform the "in" copies that don't include a frame number.
2. For each frame:
   1. Perform the "in" copies that include a frame number.
   2. Execute the binary.
   3. Perform the "out" copies that include a frame number.
3. Perform the "out" copies that don't include a frame number.

# Examples

You can run distrend with and without a proxy.

## With a proxy

Generally you will run one proxy on a public machine (say `proxy.example.com`):

    % distrend proxy

One worker per machine (including the proxy machine and the
controller's machine):

    % distrend worker proxy.example.com

And the controller on your home machine:

    % distrend controller \
        --proxy proxy.example.com \
        --in Scene.proto Scene.proto \
        --out anim/out-%03.png anim/out-%03.png \
        0,199 \
        build/ray %d anim/out 1000

## Without a proxy

You can skip the proxy if your controller's machine can be
reached directly from all workers. Tell each worker
the hostname of the controller:

    % distrend worker controller.example.com

And run the controller on that machine:

    % distrend controller \
        --in Scene.proto Scene.proto \
        --out anim/out-%03.png anim/out-%03.png \
        0,199 \
        build/ray %d anim/out 1000

# License

Copyright 2018 Lawrence Kesteloot

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
