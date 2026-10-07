.PHONY: all build test clean

all: build

build:
	cmake -S . -B build
	cmake --build build -j

test: build
	ctest --test-dir build --output-on-failure

clean:
	rm -rf build
