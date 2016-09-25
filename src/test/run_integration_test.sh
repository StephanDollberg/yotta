#!/usr/bin/env bash
yotta "::" 10000 &
pid=$!
python integration_test.py
kill $pid
