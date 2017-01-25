#!/bin/bash -xe

pid_path=$1
pid=$(cat $pid_path)
kill -s SIGUSR1 $pid
sleep 2
kill -s SIGQUIT $(cat "$pid_path.old")
