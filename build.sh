$(which conan) install . --output-folder=build --build=missing --profile=debug
cd build && cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug && cd ..
cmake --build build
