
.PHONY:
test:
	rm -rf build-tests && \
	mkdir build-tests && \
	cd build-tests && \
	CC="${CC}" CXX="${CXX}" A1C_C_FLAGS="${A1C_C_FLAGS}" CFLAGS="${CFLAGS}" CXXFLAGS="${CXXFLAGS}" LDFLAGS="${LDFLAGS}" cmake .. -G Ninja  && \
	ninja && \
	ctest

.PHONY:
asan-test: CC=clang
asan-test: CXX=clang++
asan-test: CFLAGS += -fsanitize=address,undefined -g -O3
asan-test: CXXFLAGS += -fsanitize=address,undefined -g -O3
asan-test: LDFLAGS += -fsanitize=address,undefined -fuse-ld=lld
asan-test: test