# Changelog

NumberTextThree follows semantic versioning within its generation. The "Three" is the generation and isn't part of the version; the README explains how the naming works.

## 1.0.0

This is the first release of NumberTextThree. It comes as `NumberTextThree.h`, with the `ntt_` prefix, `NUMBERTEXTTHREE_` macros and the `NumberTextThree::NumberTextThree` CMake target.

- Reading. `ntt_parse_json_f64` reads numbers in strict JSON grammar, and `ntt_decimal_to_f64` and `ntt_decimal_to_f32` convert digits that another parser has already collected. Eisel–Lemire settles almost every number from up to 19 significant digits, which are read eight at a time where the text allows it. Long inputs, extreme exponents, subnormals and the rare ambiguous products go to an exact big-integer conversion.
- Writing. `ntt_write_f64` and `ntt_write_f32` produce the shortest digits that read back to the same value, which are the same digits `std::to_chars` gives. They use Dragonbox, with SIMD digit writers for SSE2 and NEON and portable code everywhere else.
- Integers. `ntt_write_u32`, `ntt_write_u64`, `ntt_write_i32` and `ntt_write_i64`.
- The library uses integer arithmetic only, so its results don't depend on the floating-point mode. It builds as C99 and as C++ with MSVC, gcc and clang, for 32-bit and 64-bit targets.
- The repository also has examples, a benchmark suite that compares it with other libraries, and CI on Windows, Linux and macOS.
- You can use it under CC0 1.0 or 0BSD, whichever you prefer.
