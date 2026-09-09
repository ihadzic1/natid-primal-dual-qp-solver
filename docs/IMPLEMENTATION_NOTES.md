# Implementation notes

## Design decisions

### Sparse matrix and solver are both populated

The public natID `sparse::IMatrix` interface provides incremental COO-style
construction, serialization, and nonzero statistics. The public
`sparse::ISolver` interface owns factorization and the solution vector, but it
does not expose a general operation that imports an already assembled
`IMatrix`.

For that reason, `AugmentedKktSystem` sends every unique upper-triangular
triple to both objects:

- `IMatrix` preserves the original KKT matrix for inspection and statistics.
- `ISolver` factorizes the identical numerical system.

The insertion happens in one helper, so the two representations cannot drift.

### Factorization reuse

The affine and corrector equations have the same augmented KKT matrix during
one IPM iteration. Only their right-hand sides differ. The code therefore:

1. assembles once,
2. factorizes once,
3. calls `solve()` for the affine RHS,
4. replaces the RHS,
5. calls `solve()` for the corrector RHS.

### Dense problem data, sparse KKT

The course proposal explicitly asks for both dense and sparse natID types.
Dense problem matrices make residual calculations and validation transparent.
Only values above `sparseZeroTolerance` are sent to the sparse KKT matrix,
except diagonal entries, which are always structurally present.

### Equality-only special case

When \(m=0\), complementarity does not exist. The program solves the standard
equality-constrained KKT system directly:

\[
\begin{bmatrix}Q&A^\mathsf{T}\\A&0\end{bmatrix}
\begin{bmatrix}x\\y\end{bmatrix}
=
\begin{bmatrix}-c\\b\end{bmatrix}.
\]

### Validation boundary

The loader validates:

- dimensions,
- finite values,
- symmetry of \(Q\),
- paired presence of \(A,b\).

It deliberately does not compute an eigenvalue decomposition to prove
\(Q\succeq0\). Convexity is an input contract. A numerical factorization
failure is returned as `numerical_failure`.

## Portable test backend

The supplied natID archive contains Windows binaries, while automated
verification may run on another operating system. The portable backend:

- is disabled by default,
- uses the same header names and API calls as the production build,
- executes the same project source files,
- replaces only the unavailable binary implementation,
- uses dense partial-pivoting elimination solely for test execution.

It is not an alternative implementation for submission and is not suitable
for timing or solver comparisons.

## Current scope

The solver is intended for convex educational QPs with a regular KKT system.
A production-grade extension could add:

- homogeneous self-dual embedding,
- infeasibility and unboundedness certificates,
- equilibration and iterative refinement,
- sparse input retention for \(Q,A,G\),
- symbolic factorization reuse when only numeric values change,
- presolve and redundant-constraint detection.
