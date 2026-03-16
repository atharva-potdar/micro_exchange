build-debug:
    cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++
    ln -sf build/debug/compile_commands.json .
    cmake --build build/debug

build-release:
    cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
    cmake --build build/release

run-debug: build-debug
    ./build/debug/micro_exchange

run-release: build-release
    ./build/release/micro_exchange

test: build-debug
    ctest --test-dir build/debug --output-on-failure

bench: build-release
    @for b in build/release/bench_*; do echo "--- $b ---"; "$b"; done

clean:
    rm -rf build
