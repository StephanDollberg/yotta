### Integration Test
This folder contains general integration tests and tests for the upgrade procedure. 

```
./run_integration_test.sh
./run_upgrade_test.sh
```

Both require yotta to be in the path. If you only have a local build you can do something like:

```
PATH=$PATH:../../build ./run_integration_test.sh
PATH=$PATH:../../build ./run_upgrade_test.sh
```
