# NatIDQP

Primal-dual interior-point solver for convex quadratic programs, implemented
with natID dense and sparse matrix APIs.

**Course:** Numerical Optimization  
**Author:** Irfan Hadzic (index 20013)

The solver handles

\[
\min_x \frac{1}{2}x^\mathsf{T}Qx+c^\mathsf{T}x
\quad\text{subject to}\quad
Ax=b,\qquad Gx\le h,
\]

where \(Q\) is symmetric positive semidefinite. Inequalities are converted to
\(Gx+s=h\), with strictly positive slack \(s\) and inequality dual \(z\).

## Implemented functionality

- Mehrotra predictor-corrector primal-dual IPM.
- Infeasible-start initialization.
- Affine predictor and centering-plus-correction direction.
- Fraction-to-boundary line search that keeps \(s>0\) and \(z>0\).
- Dense natID problem storage:
  `dense::DblMatrix` for \(Q,c,A,b,G,h\).
- `dense::DblDiagMatrix` for the \(S^{-1}Z\) diagonal.
- Sparse augmented KKT assembly through
  `sparse::IMatrix::addTriple()`.
- Symmetric-indefinite solve through `sparse::ISolver`, using:
  - `sparse::SolverType::LDLT`
  - `sparse::Symmetry::SymmetricIndef`
  - `sparse::Pivoting::AlterMatrixIfIndefinite`
- One KKT factorization and two right-hand-side solves per IPM iteration.
- Matrix Market input for coordinate and array formats.
- Per-iteration primal iterate \(x^{(k)}\), objective value, primal residual,
  dual residual, duality gap, barrier value, KKT nonzero count, and timing
  statistics.
- CSV convergence history, text solution reports, and Matrix Market solution
  export.
- Native GUI with synchronized **Objective Function** and **Residuals** tabs,
  shared playback controls, adjustable playback speed, and residual Y-axis
  modes. Two-variable problems use an objective contour plot with the iterate
  path, feasible-set overlay, and optimum marker; larger problems use
  objective value versus iteration.
- Two checked example problems and automated tests.

The implementation does not use Eigen or another production linear algebra
backend.

## Project layout

```text
NatIDQP/
  CMakeLists.txt
  include/natid_qp/        Public project headers
  src/                     Solver, Matrix Market I/O, and CLI
  gui/                     Native synchronized objective/residual dashboard
  data/                    Small verified QP examples
  docs/                    Algorithm and implementation notes
  scripts/                 Build, benchmark generation, and plotting helpers
  tests/                   Solver tests
    portable_natID/        Test-only API compatibility layer
```

## Building with the supplied natID SDK on Windows

The supplied SDK archive contains Windows `.dll` and `.lib` binaries. Extract
it so the SDK has this structure:

```text
C:\Users\<user>\natID.SDK\
  Common\
  DevEnv\
  bin\
```

Then open PowerShell in the `NatIDQP` directory:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DNATID_SDK_ROOT="$env:USERPROFILE/natID.SDK"
cmake --build build --config Release
```

The helper script performs the same steps:

```powershell
.\scripts\build_windows.ps1
```

Before running outside Visual Studio, make the natID DLL directory available:

```powershell
$env:PATH="$env:USERPROFILE\natID.SDK\bin;$env:PATH"
```

The natID `Common.cmake` file controls the final runtime output directory.
The build output prints the exact path to `natid_qp.exe`.

For Linux or macOS, install the matching natID `.so` or `.dylib` package and
point `NATID_SDK_ROOT` to that SDK. The Windows binaries from the supplied
archive cannot be linked on Linux.

## Running

Run the built executable from this project directory.

Built-in problem with an active inequality:

```powershell
natid_qp.exe --demo inequality --csv convergence.csv `
  --output solution.txt --solution-dir solution
```

Built-in problem with one equality and nonnegativity constraints:

```powershell
natid_qp.exe --demo equality
```

Load one of the Matrix Market examples:

```powershell
natid_qp.exe --problem data/inequality_qp --csv convergence.csv
natid_qp.exe --problem data/equality_qp --no-print-kkt
```

Load explicit files:

```powershell
natid_qp.exe --Q Q.mtx --c c.mtx --A A.mtx --b b.mtx `
  --G G.mtx --h h.mtx
```

`A.mtx` and `b.mtx` are optional, but they must either both be present or both
be omitted. `Q.mtx`, `c.mtx`, `G.mtx`, and `h.mtx` are required for an
inequality-constrained problem.

Use `natid_qp.exe --help` for all solver and output options.

## Native GUI

Run `natid_qp_gui` to open the dashboard. **Choose QP Folder**, the tolerance
input, **Run Again**, playback buttons, and the animation-speed slider apply to
the loaded solver history. The **Objective Function** and **Residuals** tabs
share one current iteration, so switching tabs never restarts or rewinds the
animation. **Play Again** rewinds the existing history to iteration zero and
does not run the solver again. The **Y-axis mode** selector affects the
residual chart only.

Both plots support interactive navigation. Place the pointer over a plot and
use **Ctrl+mouse wheel** to zoom around the data point below the pointer. A
plain mouse wheel pans vertically like **W/S**, while **Shift+mouse wheel** pans
horizontally like **A/D**. Click a plot to give it keyboard focus, then use
**Ctrl++** or **Ctrl+-** to zoom around its center and the arrow keys or
**W/A/S/D** to pan. **Ctrl+0** restores the full data range. Zooming, panning,
and switching tabs do not change the current animation iteration; loading or
running a problem again resets the viewport. Deep zoom is supported up to
`1e12`, and axis labels automatically add decimal precision or switch to
scientific notation as the visible range becomes smaller. In the two-variable
objective plot, contour levels are fixed for the loaded solution (including
one unique objective level for every recorded solver point), so zooming does
not replace an iteration's iso-line with a different objective level. Fainter,
evenly spaced background contours fill otherwise empty objective ranges, but
a background level is omitted whenever its objective value is closer than 35%
of the regular contour spacing to a solver level, avoiding visual duplicates.
The objective legend distinguishes the single green optimum marker from
orange dashed inequality constraints and crimson dash-dot equality constraints;
the translucent green area remains the feasible region.
In the residual chart, vertical grid lines are drawn for every visible
iteration whenever the available pixel spacing permits it. Grid density and
iteration-label density are calculated independently, so labels can be thinned
without making the corresponding iteration grid disappear.

## Matrix Market convention

- Matrices may use `coordinate` or `array` storage.
- `real`, `integer`, and coordinate `pattern` fields are supported.
- `general`, `symmetric`, `hermitian` (real-valued), and `skew-symmetric`
  declarations are recognized.
- Matrix Market indices are one-based, as required by the format.
- Vectors can be stored as \(n\times1\) or \(1\times n\); row vectors are
  converted to columns.
- Duplicate coordinate entries are accumulated.

A directory-based problem uses these names:

```text
Q.mtx
c.mtx
A.mtx   optional
b.mtx   optional
G.mtx
h.mtx
```

## Verified examples

### Inequality example

\[
Q=I,\quad c=(-1.5,-1),\quad
x_1+x_2\le1,\quad x_1\ge0,\quad x_2\ge0.
\]

Expected solution:

\[
x^\star=(0.75,0.25),\qquad f(x^\star)=-1.0625.
\]

### Equality example

\[
Q=\begin{bmatrix}4&1\\1&2\end{bmatrix},\quad
c=(-1,-1),\quad x_1+x_2=1,\quad x\ge0.
\]

Expected solution:

\[
x^\star=(0.25,0.75),\qquad f(x^\star)=-0.125.
\]

## Tests without platform natID binaries

`tests/portable_natID` is a deliberately small, test-only implementation of
the exact natID API subset used by this project. It lets CI or a non-Windows
machine execute the same solver source when the actual binary SDK is
unavailable. It is not used in a normal build and must not be used for
performance comparisons.

```bash
cmake -S . -B build-portable \
  -DNATID_QP_PORTABLE_TEST_BACKEND=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-portable
ctest --test-dir build-portable --output-on-failure
```

## Plotting convergence

After creating a CSV with `--csv`:

```bash
python scripts/plot_convergence.py convergence.csv -o convergence.png
```

The plot contains primal residual, dual residual, and complementarity on a
logarithmic scale.

## Numerical assumptions and limitations

- The caller is responsible for supplying a convex QP: \(Q\succeq0\).
- Constraint qualifications and a nonsingular reduced KKT system are assumed.
- A small configurable primal diagonal regularization is added for numerical
  stability.
- This is an educational infeasible-start IPM, not a homogeneous
  self-dual method. It does not produce formal infeasibility or unboundedness
  certificates.
- Problem matrices are stored densely for transparent residual validation.
  The augmented KKT matrix is assembled sparsely, but \(G^\mathsf{T}S^{-1}ZG\)
  can become dense.

See [docs/ALGORITHM.md](docs/ALGORITHM.md) for the derivation and direct mapping
from each mathematical step to the natID implementation.

## References

- J. Nocedal and S. J. Wright, *Numerical Optimization*, 2nd edition,
  Chapter 16, Springer, 2006.
- Y. Yan, [cppipm](https://github.com/YimingYAN/cppipm), an educational
  interior-point implementation used as an algorithmic reference.
- natID SDK headers and MatrixTests examples supplied with the project
  materials.
