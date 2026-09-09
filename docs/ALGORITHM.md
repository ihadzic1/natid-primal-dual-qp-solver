# Algorithm and natID mapping

## 1. KKT conditions

For

\[
\min_x \frac12 x^\mathsf{T}Qx+c^\mathsf{T}x,\qquad
Ax=b,\qquad Gx\le h,
\]

introduce \(s=h-Gx\) and dual variables \(y\) and \(z\). The KKT conditions
are

\[
\begin{aligned}
Qx+c+A^\mathsf{T}y+G^\mathsf{T}z &= 0,\\
Ax-b &= 0,\\
Gx+s-h &= 0,\\
Sz &= 0,\\
s,z &\ge 0.
\end{aligned}
\]

The implementation keeps \(s,z>0\) until convergence.

The residuals used in the code are

\[
r_d=Qx+c+A^\mathsf{T}y+G^\mathsf{T}z,\quad
r_p=Ax-b,\quad
r_g=Gx+s-h.
\]

The complementarity measure is

\[
\mu=\frac{s^\mathsf{T}z}{m}.
\]

## 2. Reduced augmented system

The Newton equations for a chosen complementarity residual \(r_c\) are

\[
\begin{bmatrix}
Q&A^\mathsf{T}&G^\mathsf{T}&0\\
A&0&0&0\\
G&0&0&I\\
0&0&S&Z
\end{bmatrix}
\begin{bmatrix}
\Delta x\\\Delta y\\\Delta z\\\Delta s
\end{bmatrix}
=-
\begin{bmatrix}
r_d\\r_p\\r_g\\r_c
\end{bmatrix}.
\]

Eliminating \(\Delta s\) and \(\Delta z\) gives

\[
\begin{bmatrix}
Q+G^\mathsf{T}S^{-1}ZG&A^\mathsf{T}\\
A&0
\end{bmatrix}
\begin{bmatrix}
\Delta x\\\Delta y
\end{bmatrix}
=
\begin{bmatrix}
-r_d+G^\mathsf{T}S^{-1}(r_c-Zr_g)\\
-r_p
\end{bmatrix}.
\]

After solving,

\[
\Delta s=-r_g-G\Delta x,
\]

\[
\Delta z=S^{-1}(-r_c+Zr_g+ZG\Delta x).
\]

Only the upper triangle of the symmetric augmented matrix is inserted into
the natID sparse solver.

## 3. Mehrotra predictor-corrector

At every iteration:

1. Build and factorize the augmented KKT matrix once.
2. Predictor solve:

   \[
   r_c^{\mathrm{aff}}=Sz.
   \]

3. Compute maximum affine primal and dual steps and

   \[
   \mu_{\mathrm{aff}}
   =\frac{(s+\alpha_p^{\mathrm{aff}}\Delta s^{\mathrm{aff}})^\mathsf{T}
   (z+\alpha_d^{\mathrm{aff}}\Delta z^{\mathrm{aff}})}{m}.
   \]

4. Choose Mehrotra's centering parameter

   \[
   \sigma=
   \operatorname{clip}\left[
   \left(\frac{\mu_{\mathrm{aff}}}{\mu}\right)^3,0,1\right].
   \]

5. Corrector solve with the same factorization:

   \[
   r_c=Sz+
   \Delta S^{\mathrm{aff}}\Delta z^{\mathrm{aff}}
   -\sigma\mu e.
   \]

The second term is the second-order correction.

## 4. Fraction-to-boundary step

For a positive vector \(v\) and direction \(\Delta v\),

\[
\alpha_{\max}(v,\Delta v)
=\min_{\Delta v_i<0}\left(-\frac{v_i}{\Delta v_i}\right).
\]

The accepted steps are

\[
\alpha_p=\min(1,\tau\alpha_{\max}(s,\Delta s)),\qquad
\alpha_d=\min(1,\tau\alpha_{\max}(z,\Delta z)),
\]

with \(\tau=0.995\) by default.

The updates are

\[
x^+=x+\alpha_p\Delta x,\quad
s^+=s+\alpha_p\Delta s,
\]

\[
y^+=y+\alpha_d\Delta y,\quad
z^+=z+\alpha_d\Delta z.
\]

## 5. Initialization

The method uses an infeasible start:

\[
x=0,\qquad y=0,\qquad z=e.
\]

Initially \(s=h-Gx=h\). If any component is nonpositive, every component is
shifted by

\[
1-\min_i s_i,
\]

which gives a strictly positive starting slack without requiring a feasible
starting point.

## 6. Convergence

The raw values printed per iteration are:

- \(\max(\lVert r_p\rVert_\infty,\lVert r_g\rVert_\infty)\),
- \(\lVert r_d\rVert_\infty\),
- \(s^\mathsf{T}z\),
- \(\mu=s^\mathsf{T}z/m\).

Stopping uses scaled values:

\[
\frac{\max(\lVert r_p\rVert_\infty,\lVert r_g\rVert_\infty)}
{1+\max(\lVert b\rVert_\infty,\lVert h\rVert_\infty)},
\]

\[
\frac{\lVert r_d\rVert_\infty}{1+\lVert c\rVert_\infty},
\qquad
\frac{\mu}{1+|f(x)|}.
\]

All three must be no larger than the requested tolerance.

## 7. natID type mapping

| Mathematical object or operation | natID implementation |
| --- | --- |
| \(Q,c,A,b,G,h,x,y,s,z\) | `dense::DblMatrix` |
| \(S^{-1}Z\) | `dense::DblDiagMatrix` |
| Original sparse KKT representation | `sparse::IDblMatrix` |
| COO insertion | `sparse::IMatrix::addTriple()` |
| Factorized linear system | `sparse::DblSolver` / `sparse::ISolver` |
| KKT symmetry | `sparse::Symmetry::SymmetricIndef` |
| Factorization | `sparse::SolverType::LDLT` |
| Indefinite pivot handling | `sparse::Pivoting::AlterMatrixIfIndefinite` |
| Ownership | `sparse::DblMatrixReleaser`, `sparse::DblSolverReleaser` |
| Dense matrix-vector products | `dense::Matrix::gemv()` |

The code assembles both an `IMatrix` and an `ISolver` from the same unique
upper-triangular triples. `IMatrix` preserves and serializes the unfactorized
KKT matrix and reports sparsity; `ISolver` owns factorization and repeated
right-hand-side solves.

## 8. Source guide

- `src/QPProblem.cpp`: dimensions, finite-value checks, symmetry validation,
  and built-in examples.
- `src/MatrixMarket.cpp`: Matrix Market coordinate/array parsing and output.
- `src/InteriorPointSolver.cpp`: residuals, KKT reduction, sparse assembly,
  predictor-corrector, line search, convergence, and reports.
- `src/main.cpp`: CLI and file routing.
- `tests/test_solver.cpp`: known-solution and I/O checks.

## 9. Interpretation of timing

Each CSV row records:

- dense augmented-Hessian formation plus sparse COO assembly,
- natID LDLT factorization,
- total time of the affine and corrector triangular solves.

This split makes it possible to identify whether a problem is dominated by
assembly, factorization, or repeated solves.
