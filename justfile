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

profile-perf: build-release
    @echo "\n--- Hardware Profiling: Main Engine ---"
    sudo perf stat \
        -r 3 \
        -e cycles,instructions \
        -e branches,branch-misses \
        -e cache-references,cache-misses \
        -e L1-dcache-loads,L1-dcache-load-misses,L1-dcache-stores \
        -e L1-icache-loads,L1-icache-load-misses \
        -e LLC-loads,LLC-load-misses,LLC-stores,LLC-store-misses \
        -e dTLB-loads,dTLB-load-misses,dTLB-stores,dTLB-store-misses \
        -e iTLB-loads,iTLB-load-misses \
        -e cpu-migrations,context-switches,page-faults \
        -e stalled-cycles-frontend,stalled-cycles-backend \
        taskset -c 2 chrt -f 99 \
        ./build/release/micro_exchange

tidy-modernize:
    clang-tidy -p build/debug --header-filter='src/.*' --checks='-*,modernize-*' --fix src/**/*.hpp src/*.cpp test/*.cpp bench/*.cpp
    just test

tidy-cppcoreguidelines:
    clang-tidy -p build/debug --header-filter='src/.*' --checks='-*,cppcoreguidelines-*' --fix src/**/*.hpp src/*.cpp test/*.cpp bench/*.cpp
    just test

tidy-readability:
    clang-tidy -p build/debug --header-filter='src/.*' --checks='-*,readability-*' --fix src/**/*.hpp src/*.cpp test/*.cpp bench/*.cpp
    just test

tidy-performance:
    clang-tidy -p build/debug --header-filter='src/.*' --checks='-*,performance-*' --fix src/**/*.hpp src/*.cpp test/*.cpp bench/*.cpp
    just test

format:
    clang-format -i src/**/*.hpp src/*.cpp test/*.cpp bench/*.cpp
    just test

tidy-all: tidy-modernize tidy-cppcoreguidelines tidy-readability tidy-performance format


