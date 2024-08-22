#!/bin/bash

run_all_tests() {
  for f in build/src/*_test; do
    if ! ./"$f"; then 
      echo "Tests failed"
      exit 1
    fi
  done
}

cd ..

cp -r build build-old

# Debug / SanitizersOff
rm -rf build/
echo "Run Debug/SanitizersOff"
/usr/bin/cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Debug -DASAN:STRING=OFF -DTSAN:STRING=ON -DUNIT_TESTS:STRING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -Bbuild -G Ninja
/usr/bin/cmake --build build --config Debug --target all "-j 10" --
run_all_tests
# Debug / ASAN 
rm -rf build/
echo "Run Debug/ASAN"
/usr/bin/cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Debug -DASAN:STRING=ON -DTSAN:STRING=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/clang-15 -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++-15 -S/home/hutu/epfl-da/CS451-2023-project/template_cpp -B/home/hutu/epfl-da/CS451-2023-project/build -G Ninja
/usr/bin/cmake --build build --config Debug --target all "-j 10" --
run_all_tests
# Debug / TSAN
rm -rf build/
echo "Run Debug/TSAN"
/usr/bin/cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Debug -DASAN:STRING=OFF -DTSAN:STRING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/clang-15 -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++-15 -S/home/hutu/epfl-da/CS451-2023-project/template_cpp -B/home/hutu/epfl-da/CS451-2023-project/build -G Ninja
/usr/bin/cmake --build build --config Debug --target all "-j 10" --
run_all_tests
# Release / SanitizersOff
rm -rf build/
echo "Run Release/SanitizersOff"
/usr/bin/cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -DASAN:STRING=OFF -DTSAN:STRING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/clang-15 -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++-15 -S/home/hutu/epfl-da/CS451-2023-project/template_cpp -B/home/hutu/epfl-da/CS451-2023-project/build -G Ninja
/usr/bin/cmake --build build --config Release --target all "-j 10" --
run_all_tests
# Release / ASAN 
rm -rf build/
echo "Run Release/ASAN"
/usr/bin/cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -DASAN:STRING=ON -DTSAN:STRING=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/clang-15 -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++-15 -S/home/hutu/epfl-da/CS451-2023-project/template_cpp -B/home/hutu/epfl-da/CS451-2023-project/build -G Ninja
/usr/bin/cmake --build build --config Release --target all "-j 10" --
run_all_tests
# Release / TSAN
rm -rf build/
echo "Run Release/TSAN"
/usr/bin/cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -DASAN:STRING=OFF -DTSAN:STRING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/clang-15 -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++-15 -S/home/hutu/epfl-da/CS451-2023-project/template_cpp -B/home/hutu/epfl-da/CS451-2023-project/build -G Ninja
/usr/bin/cmake --build build --config Release --target all "-j 10" --
run_all_tests

echo "Run complete"

rm -rf build/
mv build-old build
