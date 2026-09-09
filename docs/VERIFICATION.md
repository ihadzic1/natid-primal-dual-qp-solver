# Verification record

Verification date: 2026-07-27

## Checks completed

1. The production source was compiled in C++20 syntax-only mode against the
   actual headers from the supplied natID SDK.
   - Result: success.
   - Compiler diagnostics were limited to warnings originating in supplied
     natID headers.

2. The same solver sources were compiled and executed with the test-only
   portable backend.
   - Compiler: GCC 13.3.
   - Warning flags: `-Wall -Wextra -Wpedantic`.
   - Result: clean project build and all tests passed.

3. Known-solution numerical tests:
   - inequality QP: \(x=(0.75,0.25)\), \(f=-1.0625\);
   - equality plus nonnegativity QP: \(x=(0.25,0.75)\), \(f=-0.125\);
   - equality-only KKT path: \(x=(1,0)\);
   - Matrix Market versions of both supplied examples;
   - invalid nonsymmetric \(Q\) rejection.

4. AddressSanitizer and UndefinedBehaviorSanitizer:
   - all tests passed;
   - leak detection was disabled because LeakSanitizer is unsupported under
     the runtime's ptrace configuration.

5. Scalable generated example:
   - \(n=20\), \(m=21\);
   - converged in 7 iterations at the default tolerance;
   - final primal residual was zero to printed precision;
   - final dual residual was below \(9\times10^{-10}\);
   - CSV generation and convergence plotting both succeeded.

6. CLI output paths:
   - text report;
   - convergence CSV;
   - Matrix Market `x`, `y`, `s`, and `z` output;
   - built-in and directory-based problem loading.

## Platform boundary

The supplied SDK package contains Windows `.dll` and `.lib` files. The current
verification runtime is Linux, so it cannot perform the final Windows link or
execute the proprietary natID binary implementation. API compatibility was
instead checked directly against the supplied production headers, while
algorithm execution used the isolated test backend.

The final Windows build should be run with the supplied SDK as described in
`README.md`.
