#!/bin/bash

run_all_tests() {
  for f in build/*_test; do
    if ! ./"$f"; then 
      echo "Tests failed"
      exit 1
    fi
  done
}

cd ..

# Debug / SanitizersOff
rm -rf build/
echo "Run Debug/SanitizersOff"
cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Debug -DASAN:STRING=OFF -DTSAN:STRING=ON -DUNIT_TESTS:STRING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -Bbuild
cmake --build build --config Debug --target all "-j 10" --
run_all_tests
# Debug / ASAN 
rm -rf build/
echo "Run Debug/ASAN"
cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Debug -DASAN:STRING=ON -DTSAN:STRING=OFF -DUNIT_TESTS:STRING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -Bbuild
cmake --build build --config Debug --target all "-j 10" --
run_all_tests
# Debug / TSAN
rm -rf build/
echo "Run Debug/TSAN"
cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Debug -DASAN:STRING=OFF -DTSAN:STRING=ON -DUNIT_TESTS:STRING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -Bbuild
cmake --build build --config Debug --target all "-j 10" --
run_all_tests
# Release / SanitizersOff
rm -rf build/
echo "Run Release/SanitizersOff"
cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -DASAN:STRING=OFF -DTSAN:STRING=ON -DUNIT_TESTS:STRING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -Bbuild
cmake --build build --config Release --target all "-j 10" --
run_all_tests
# Release / ASAN 
rm -rf build/
echo "Run Release/ASAN"
cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -DASAN:STRING=ON -DTSAN:STRING=OFF -DUNIT_TESTS:STRING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -Bbuild
cmake --build build --config Release --target all "-j 10" --
run_all_tests
# Release / TSAN
rm -rf build/
echo "Run Release/TSAN"
cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -DASAN:STRING=OFF -DTSAN:STRING=ON -DUNIT_TESTS:STRING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -Bbuild
cmake --build build --config Release --target all "-j 10" --
run_all_tests

echo "Run complete"

rm -rf build/
