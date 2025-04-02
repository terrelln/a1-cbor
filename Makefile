
.PHONY:
test:
	rm -rf build-tests && \
	mkdir build-tests && \
	cd build-tests && \
	A1C_C_FLAGS="${A1C_C_FLAGS}" CFLAGS="${CFLAGS}" CXXFLAGS="${CXXFLAGS}" cmake .. -G Ninja  && \
	ninja && \
	ctest

.PHONY:
asan-test: CFLAGS += -fsanitize=address,undefined
asan-test: CXXFLAGS += -fsanitize=address,undefined
asan-test: test