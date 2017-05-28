#!/usr/bin/env bash
set -xe
$(which yotta) -h :: -p 10000 -g -i /tmp/yotta_update_test.pid
pid=$(cat /tmp/yotta_update_test.pid)
sleep 1
kill -s SIGUSR1 $pid
kill -s SIGQUIT $(cat /tmp/yotta_update_test.pid.old)
curl http://localhost:10000
kill -s SIGQUIT $(cat /tmp/yotta_update_test.pid)
