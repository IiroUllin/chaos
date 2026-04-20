# LibChaos: a library for (pseudo) Random Number Generation and related goodies

This library utilizes `xoroshiro128+` and a few similar algorithms for random bit generation.
(See comments in the code for details.)
Most of the specific distributions are sampled using the standard methods, e.g., inverse CDF, ziggurat, Fisher shuffle, etc. 
Some extra tricks are used every once in a while to improve performance.


## Usage

Place `chaos.hpp` into your `$(INCLUDE)` directory; `libchaos.a` into your `$(LIB)` directory;
use linker flag `-lchaos` for compilation.
Running `make install` will sync `~/include/chaos.hpp` and `~/lib/libchaos.a` with local versions --- 
do not run it unless you mean it.

Additional headers, `generic.hpp` (and `generic-avx.hpp` for AVX support) from [IiroUllin/generic](https://github.com/IiroUllin/generic) are needed to compile this code.

`chs::RNG` is a random number generator object.
Initialize it by hashing something with `.hash(...)`.


## Distributions

- `.int64()`, `.int32()` -- random 64 or 32 integers bits respectively. If called with an argument, e.g., `int32(N)`, will produce a random integer in [0,N-1]

- `U01()` -- random [0,1) FP


## Multithreading

The `chs::RNG` objects are not thread-safe: do not use them in more than one thread at a time.
Use `RNG::spawn()` to create (hopefully) uncorrelated RNGs for parallel processing.

