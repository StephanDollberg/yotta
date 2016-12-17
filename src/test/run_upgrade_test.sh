#!/usr/bin/env bash
set -e
$(which yotta) "::" 10000 /tmp/yotta_update_test.pid &
pid=$!
sleep 1
kill -s SIGUSR1 $pid
kill -s SIGQUIT $(cat /tmp/yotta_update_test.pid.old)
curl http://localhost:10000
kill -s SIGQUIT $(cat /tmp/yotta_update_test.pid)
