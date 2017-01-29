#!/usr/bin/env bash
$1 "::" 10000 &
pid=$!
nosetests integration_test.py
status=$?
kill $pid
exit $status
