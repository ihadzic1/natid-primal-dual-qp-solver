# Verification record

Latest verification date: 2026-09-10

## Latest Windows verification

- CMake configured and built `natid_qp`, `natid_qp_tests`, and
  `natid_qp_gui` as C++20 with MSVC 19.42 against the installed production
  natID SDK.
- The native targets linked successfully, including `natid_qp_dtwin`,
  `natGUI`, `modSolver`, and `symbSolvers`.
- The native `ctest` run passed all tests.
- A separate portable-backend MSVC C++20 build also passed all tests.
- The tests now additionally verify that every iteration stores a complete
  primal `x`, that its stored objective is consistent with that `x`, and that
  the final history point matches `Solution::x`.

The checks below record the earlier solver-focused verification retained from
the previous project version.

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

7. dTwin integration checks:
   - generated NLE contains all KKT variable groups and complementarity
     equations;
   - exact, close, partial, mismatch, and unavailable classifications are
     covered by automated tests;
   - the native adapter and GUI compile in syntax-only mode against the actual
     `sc::IModel`, GUI, and matrix headers from the supplied natID SDK.

## Platform notes

The supplied SDK package contains Windows `.dll` and `.lib` files. The latest
verification was performed on Windows with those production headers and
libraries, while the portable backend remains available for isolated solver
tests on machines without the proprietary runtime.
