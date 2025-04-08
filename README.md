# __A__rena __1__-pass __CBOR__

An arena based single-pass CBOR implementation. The implementation focuses on simplicity and safety.

The full CBOR spec is supported with the following exceptions:

1. Only ints that fit in an `int64_t` are supported for API simplicity.
2. Half data is stored as a 16-bit field, due to the lack of C support.

## Features

1. Arena based allocation means that freeing memory is drastically simplified.
2. Immutable item API for simplicity & safe references.
3. Strong memory limits in the decoder. By default it won't allocate more than `sizeof(A1C_Item) * encoded_size`, and tighter memory limits can be applied.
4. JSON pretty printing (UTF-8 strings not supported).
5. 100% thread-safe.
6. Fuzz tested for:
    a. Decoding safety on untrusted input
    b. Differential fuzzing to ensure we accept and reject exactly the same set of inputs as [`libcbor`](https://github.com/PJK/libcbor) (with the exception of integers that don't fit in an `int64_t`).
    c. Round trip fuzzing
    d. Round trip fuzzing for JSON encoding