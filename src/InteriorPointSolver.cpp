#include "natid_qp/InteriorPointSolver.h"

#include <dense/DiagMatrix.h>
#include <sparse/IMatrix.h>
#include <sparse/ISolver.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace natid_qp
{
namespace
{

using Clock = std::chrono::steady_clock;

double elapsedMilliseconds(const Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

dense::DblMatrix makeVector(const std::size_t length, const double value = 0.0)
{
    dense::DblMatrix vector(
        static_cast<unsigned int>(length),
        1,
        nullptr,
        true
    );
    auto output = vector.getFirstColumnManipulator();
    for (unsigned int row = 0; row < length; ++row)
        output(row) = value;
    return vector;
}

double vectorInfinityNorm(const dense::DblMatrix& vector)
{
    if (vector.getNoOfRows() == 0)
        return 0.0;

    const auto values = vector.getFirstColumnManipulator();
    double result = 0.0;
    for (unsigned int row = 0; row < vector.getNoOfRows(); ++row)
        result = std::max(result, std::abs(values(row)));
    return result;
}

double vectorDot(const dense::DblMatrix& left, const dense::DblMatrix& right)
{
    const auto length = left.getNoOfRows();
    if (length != right.getNoOfRows())
        throw std::logic_error("Internal vector length mismatch.");
    if (length == 0)
        return 0.0;

    const auto a = left.getFirstColumnManipulator();
    const auto b = right.getFirstColumnManipulator();
    double result = 0.0;
    for (unsigned int row = 0; row < length; ++row)
        result += a(row) * b(row);
    return result;
}

double objectiveValue(const QPProblem& problem, const dense::DblMatrix& x)
{
    dense::DblMatrix qx = makeVector(problem.variables());
    problem.Q.gemv(x, qx);
    return 0.5 * vectorDot(x, qx) + vectorDot(problem.c, x);
}

struct Residuals
{
    dense::DblMatrix dual;
    dense::DblMatrix equality;
    dense::DblMatrix inequality;

    Residuals(const std::size_t n, const std::size_t p, const std::size_t m)
        : dual(makeVector(n))
    {
        if (p > 0)
            equality = makeVector(p);
        if (m > 0)
            inequality = makeVector(m);
    }
};

Residuals computeResiduals(
    const QPProblem& problem,
    const dense::DblMatrix& x,
    const dense::DblMatrix& equalityDual,
    const dense::DblMatrix& slack,
    const dense::DblMatrix& inequalityDual
)
{
    const auto n = problem.variables();
    const auto p = problem.equalities();
    const auto m = problem.inequalities();
    Residuals residuals(n, p, m);

    problem.Q.gemv(x, residuals.dual);
    auto rd = residuals.dual.getFirstColumnManipulator();
    const auto c = problem.c.getFirstColumnManipulator();
    for (unsigned int column = 0; column < n; ++column)
        rd(column) += c(column);

    if (p > 0)
    {
        const auto a = problem.A.getManipulator();
        const auto y = equalityDual.getFirstColumnManipulator();
        for (unsigned int column = 0; column < n; ++column)
        {
            for (unsigned int row = 0; row < p; ++row)
                rd(column) += a(row, column) * y(row);
        }

        problem.A.gemv(x, residuals.equality);
        auto rp = residuals.equality.getFirstColumnManipulator();
        const auto b = problem.b.getFirstColumnManipulator();
        for (unsigned int row = 0; row < p; ++row)
            rp(row) -= b(row);
    }

    if (m > 0)
    {
        const auto g = problem.G.getManipulator();
        const auto z = inequalityDual.getFirstColumnManipulator();
        for (unsigned int column = 0; column < n; ++column)
        {
            for (unsigned int row = 0; row < m; ++row)
                rd(column) += g(row, column) * z(row);
        }

        problem.G.gemv(x, residuals.inequality);
        auto rg = residuals.inequality.getFirstColumnManipulator();
        const auto s = slack.getFirstColumnManipulator();
        const auto h = problem.h.getFirstColumnManipulator();
        for (unsigned int row = 0; row < m; ++row)
            rg(row) += s(row) - h(row);
    }

    return residuals;
}

double primalDataScale(const QPProblem& problem)
{
    return 1.0 + std::max(
        vectorInfinityNorm(problem.b),
        vectorInfinityNorm(problem.h)
    );
}

double dualDataScale(const QPProblem& problem)
{
    return 1.0 + vectorInfinityNorm(problem.c);
}

IterationStats makeStats(
    const int iteration,
    const QPProblem& problem,
    const dense::DblMatrix& x,
    const dense::DblMatrix& slack,
    const dense::DblMatrix& inequalityDual,
    const Residuals& residuals
)
{
    IterationStats stats;
    stats.iteration = iteration;
    stats.objective = objectiveValue(problem, x);
    stats.primalResidual = std::max(
        vectorInfinityNorm(residuals.equality),
        vectorInfinityNorm(residuals.inequality)
    );
    stats.dualResidual = vectorInfinityNorm(residuals.dual);
    stats.dualityGap = problem.inequalities() > 0
        ? vectorDot(slack, inequalityDual)
        : 0.0;
    stats.mu = problem.inequalities() > 0
        ? stats.dualityGap / static_cast<double>(problem.inequalities())
        : 0.0;
    stats.scaledPrimalResidual = stats.primalResidual / primalDataScale(problem);
    stats.scaledDualResidual = stats.dualResidual / dualDataScale(problem);
    stats.scaledComplementarity = stats.mu / (1.0 + std::abs(stats.objective));
    return stats;
}

bool hasConverged(const IterationStats& stats, const double tolerance)
{
    return std::max(
        {stats.scaledPrimalResidual,
         stats.scaledDualResidual,
         stats.scaledComplementarity}
    ) <= tolerance;
}

bool statsAreFinite(const IterationStats& stats)
{
    return std::isfinite(stats.objective)
        && std::isfinite(stats.primalResidual)
        && std::isfinite(stats.dualResidual)
        && std::isfinite(stats.dualityGap)
        && std::isfinite(stats.mu)
        && std::isfinite(stats.scaledPrimalResidual)
        && std::isfinite(stats.scaledDualResidual)
        && std::isfinite(stats.scaledComplementarity);
}

dense::DblMatrix buildAugmentedHessian(
    const QPProblem& problem,
    const dense::DblMatrix& slack,
    const dense::DblMatrix& inequalityDual,
    const double regularization
)
{
    const auto n = problem.variables();
    const auto m = problem.inequalities();
    dense::DblMatrix hessian(
        static_cast<unsigned int>(n),
        static_cast<unsigned int>(n),
        nullptr,
        true
    );

    const auto q = problem.Q.getManipulator();
    auto h = hessian.getManipulator();
    for (unsigned int column = 0; column < n; ++column)
    {
        for (unsigned int row = 0; row < n; ++row)
            h(row, column) = 0.5 * (q(row, column) + q(column, row));
    }

    if (m > 0)
    {
        dense::DblDiagMatrix weights(static_cast<int>(m));
        auto w = weights.getManipulator();
        const auto s = slack.getFirstColumnManipulator();
        const auto z = inequalityDual.getFirstColumnManipulator();
        for (unsigned int row = 0; row < m; ++row)
        {
            if (!(s(row) > 0.0) || !(z(row) > 0.0))
                throw std::runtime_error("Interior-point iterate lost strict positivity.");
            w(row) = z(row) / s(row);
        }

        const auto g = problem.G.getManipulator();
        for (unsigned int column = 0; column < n; ++column)
        {
            for (unsigned int row = 0; row <= column; ++row)
            {
                double gain = 0.0;
                for (unsigned int inequality = 0; inequality < m; ++inequality)
                {
                    gain += g(inequality, row) * w(inequality)
                          * g(inequality, column);
                }
                h(row, column) += gain;
                if (row != column)
                    h(column, row) += gain;
            }
        }
    }

    for (unsigned int diagonal = 0; diagonal < n; ++diagonal)
        h(diagonal, diagonal) += regularization;
    return hessian;
}

std::size_t countKktNonZeros(
    const dense::DblMatrix& hessian,
    const dense::DblMatrix& equalityMatrix,
    const double zeroTolerance
)
{
    const auto n = hessian.getNoOfRows();
    const auto p = equalityMatrix.getNoOfRows();
    const auto h = hessian.getManipulator();
    std::size_t result = p; // Structural zero diagonals in the equality block.

    for (unsigned int column = 0; column < n; ++column)
    {
        for (unsigned int row = 0; row <= column; ++row)
        {
            if (row == column || std::abs(h(row, column)) > zeroTolerance)
                ++result;
        }
    }

    if (p > 0)
    {
        const auto a = equalityMatrix.getManipulator();
        for (unsigned int equality = 0; equality < p; ++equality)
        {
            for (unsigned int variable = 0; variable < n; ++variable)
            {
                if (std::abs(a(equality, variable)) > zeroTolerance)
                    ++result;
            }
        }
    }
    return std::max<std::size_t>(result, 1);
}

class AugmentedKktSystem
{
public:
    AugmentedKktSystem(
        const dense::DblMatrix& hessian,
        const dense::DblMatrix& equalityMatrix,
        const double zeroTolerance,
        const bool printKkt
    )
        : _variables(hessian.getNoOfRows()),
          _equalities(equalityMatrix.getNoOfRows()),
          _dimension(_variables + _equalities),
          _estimatedNonZeros(countKktNonZeros(
              hessian,
              equalityMatrix,
              zeroTolerance
          )),
          _matrix(sparse::createDblMatrix(
              static_cast<int>(_dimension),
              static_cast<int>(_dimension),
              static_cast<int>(_estimatedNonZeros),
              sparse::Symmetry::SymmetricIndef
          )),
          _solver(sparse::createDblSolver(
              static_cast<int>(_dimension),
              static_cast<int>(_estimatedNonZeros),
              sparse::Symmetry::SymmetricIndef,
              sparse::SolverType::LDLT,
              sparse::Pivoting::AlterMatrixIfIndefinite,
              sparse::Ordering::Own
          ))
    {
        const Clock::time_point assemblyStart = Clock::now();
        if (!_matrix.ptr() || !_solver.ptr())
            throw std::runtime_error("natID could not allocate the sparse KKT system.");

        const auto h = hessian.getManipulator();
        for (unsigned int column = 0; column < _variables; ++column)
        {
            for (unsigned int row = 0; row <= column; ++row)
            {
                const double value = h(row, column);
                if (row == column || std::abs(value) > zeroTolerance)
                    addUpper(row, column, value);
            }
        }

        if (_equalities > 0)
        {
            const auto a = equalityMatrix.getManipulator();
            for (unsigned int equality = 0; equality < _equalities; ++equality)
            {
                for (unsigned int variable = 0; variable < _variables; ++variable)
                {
                    const double value = a(equality, variable);
                    if (std::abs(value) > zeroTolerance)
                        addUpper(variable, _variables + equality, value);
                }
            }
        }

        _solver->populateDiagonals(0.0);
        _assemblyMilliseconds = elapsedMilliseconds(assemblyStart);
        _nonZeros = _matrix->getNoOfNonZero();

        if (printKkt)
        {
            std::cout << "\nKKT matrix before factorization:\n";
            _matrix->serialize("KKT", std::cout, sparse::Format::Matlab);
        }

        const Clock::time_point factorizationStart = Clock::now();
        if (!_solver->factorize())
        {
            const char* error = _solver->getLastError();
            throw std::runtime_error(
                std::string("natID LDLT factorization failed")
                + (error && *error ? ": " + std::string(error) : ".")
            );
        }
        _factorizationMilliseconds = elapsedMilliseconds(factorizationStart);

        if (printKkt)
        {
            std::cout << "KKT matrix after natID LDLT factorization:\n";
            _solver->serialize("KKT_factorized", std::cout, sparse::Format::Matlab);
            std::cout << '\n';
        }
    }

    void solve(
        const dense::DblMatrix& variableRhs,
        const dense::DblMatrix& equalityRhs,
        dense::DblMatrix& variableDirection,
        dense::DblMatrix& equalityDirection
    )
    {
        _solver->clearRHS();
        const auto rx = variableRhs.getFirstColumnManipulator();
        for (unsigned int row = 0; row < _variables; ++row)
            _solver->setRHS(static_cast<int>(row), rx(row));

        if (_equalities > 0)
        {
            const auto ry = equalityRhs.getFirstColumnManipulator();
            for (unsigned int row = 0; row < _equalities; ++row)
            {
                _solver->setRHS(
                    static_cast<int>(_variables + row),
                    ry(row)
                );
            }
        }

        const Clock::time_point solveStart = Clock::now();
        if (!_solver->solve())
        {
            const char* error = _solver->getLastError();
            throw std::runtime_error(
                std::string("natID triangular solve failed")
                + (error && *error ? ": " + std::string(error) : ".")
            );
        }
        _solveMilliseconds += elapsedMilliseconds(solveStart);

        auto dx = variableDirection.getFirstColumnManipulator();
        for (unsigned int row = 0; row < _variables; ++row)
            dx(row) = _solver->x(static_cast<int>(row));

        if (_equalities > 0)
        {
            auto dy = equalityDirection.getFirstColumnManipulator();
            for (unsigned int row = 0; row < _equalities; ++row)
                dy(row) = _solver->x(static_cast<int>(_variables + row));
        }
    }

    [[nodiscard]] std::size_t nonZeros() const
    {
        return _nonZeros;
    }

    [[nodiscard]] double assemblyMilliseconds() const
    {
        return _assemblyMilliseconds;
    }

    [[nodiscard]] double factorizationMilliseconds() const
    {
        return _factorizationMilliseconds;
    }

    [[nodiscard]] double solveMilliseconds() const
    {
        return _solveMilliseconds;
    }

private:
    void addUpper(
        const unsigned int row,
        const unsigned int column,
        const double value
    )
    {
        _matrix->addTriple(
            static_cast<int>(row),
            static_cast<int>(column),
            value
        );
        _solver->addTriple(
            static_cast<int>(row),
            static_cast<int>(column),
            value
        );
    }

    unsigned int _variables = 0;
    unsigned int _equalities = 0;
    unsigned int _dimension = 0;
    std::size_t _estimatedNonZeros = 0;
    sparse::DblMatrixReleaser _matrix;
    sparse::DblSolverReleaser _solver;
    std::size_t _nonZeros = 0;
    double _assemblyMilliseconds = 0.0;
    double _factorizationMilliseconds = 0.0;
    double _solveMilliseconds = 0.0;
};

struct Direction
{
    dense::DblMatrix x;
    dense::DblMatrix equalityDual;
    dense::DblMatrix slack;
    dense::DblMatrix inequalityDual;

    Direction(const std::size_t n, const std::size_t p, const std::size_t m)
        : x(makeVector(n)),
          slack(makeVector(m)),
          inequalityDual(makeVector(m))
    {
        if (p > 0)
            equalityDual = makeVector(p);
    }
};

Direction computeDirection(
    const QPProblem& problem,
    const dense::DblMatrix& slack,
    const dense::DblMatrix& inequalityDual,
    const Residuals& residuals,
    const dense::DblMatrix& complementarityRhs,
    AugmentedKktSystem& kkt
)
{
    const auto n = problem.variables();
    const auto p = problem.equalities();
    const auto m = problem.inequalities();
    dense::DblMatrix rhsX = makeVector(n);
    dense::DblMatrix rhsY;
    if (p > 0)
        rhsY = makeVector(p);

    const auto rd = residuals.dual.getFirstColumnManipulator();
    auto bx = rhsX.getFirstColumnManipulator();
    for (unsigned int variable = 0; variable < n; ++variable)
        bx(variable) = -rd(variable);

    const auto s = slack.getFirstColumnManipulator();
    const auto z = inequalityDual.getFirstColumnManipulator();
    const auto rg = residuals.inequality.getFirstColumnManipulator();
    const auto rc = complementarityRhs.getFirstColumnManipulator();
    const auto g = problem.G.getManipulator();

    for (unsigned int variable = 0; variable < n; ++variable)
    {
        for (unsigned int inequality = 0; inequality < m; ++inequality)
        {
            bx(variable) += g(inequality, variable)
                * (rc(inequality) - z(inequality) * rg(inequality))
                / s(inequality);
        }
    }

    if (p > 0)
    {
        auto by = rhsY.getFirstColumnManipulator();
        const auto rp = residuals.equality.getFirstColumnManipulator();
        for (unsigned int equality = 0; equality < p; ++equality)
            by(equality) = -rp(equality);
    }

    Direction direction(n, p, m);
    kkt.solve(rhsX, rhsY, direction.x, direction.equalityDual);

    dense::DblMatrix gdx = makeVector(m);
    problem.G.gemv(direction.x, gdx);
    const dense::DblMatrix& constantGdx = gdx;
    const auto gdxValues = constantGdx.getFirstColumnManipulator();
    auto ds = direction.slack.getFirstColumnManipulator();
    auto dz = direction.inequalityDual.getFirstColumnManipulator();
    for (unsigned int inequality = 0; inequality < m; ++inequality)
    {
        ds(inequality) = -rg(inequality) - gdxValues(inequality);
        dz(inequality) = (
            -rc(inequality)
            + z(inequality) * rg(inequality)
            + z(inequality) * gdxValues(inequality)
        ) / s(inequality);
    }

    return direction;
}

double maximumPositiveStep(
    const dense::DblMatrix& value,
    const dense::DblMatrix& direction
)
{
    const auto length = value.getNoOfRows();
    const auto current = value.getFirstColumnManipulator();
    const auto delta = direction.getFirstColumnManipulator();
    double step = std::numeric_limits<double>::infinity();
    for (unsigned int row = 0; row < length; ++row)
    {
        if (delta(row) < 0.0)
            step = std::min(step, -current(row) / delta(row));
    }
    return std::max(0.0, step);
}

dense::DblMatrix affineComplementarity(
    const dense::DblMatrix& slack,
    const dense::DblMatrix& inequalityDual
)
{
    dense::DblMatrix result = makeVector(slack.getNoOfRows());
    const auto s = slack.getFirstColumnManipulator();
    const auto z = inequalityDual.getFirstColumnManipulator();
    auto rc = result.getFirstColumnManipulator();
    for (unsigned int row = 0; row < slack.getNoOfRows(); ++row)
        rc(row) = s(row) * z(row);
    return result;
}

double affineMu(
    const dense::DblMatrix& slack,
    const dense::DblMatrix& inequalityDual,
    const Direction& affine,
    const double alphaPrimal,
    const double alphaDual
)
{
    const auto m = slack.getNoOfRows();
    const auto s = slack.getFirstColumnManipulator();
    const auto z = inequalityDual.getFirstColumnManipulator();
    const auto ds = affine.slack.getFirstColumnManipulator();
    const auto dz = affine.inequalityDual.getFirstColumnManipulator();
    double product = 0.0;
    for (unsigned int row = 0; row < m; ++row)
    {
        product += (s(row) + alphaPrimal * ds(row))
                 * (z(row) + alphaDual * dz(row));
    }
    return product / static_cast<double>(m);
}

dense::DblMatrix correctorComplementarity(
    const dense::DblMatrix& slack,
    const dense::DblMatrix& inequalityDual,
    const Direction& affine,
    const double sigma,
    const double mu
)
{
    dense::DblMatrix result = makeVector(slack.getNoOfRows());
    const auto s = slack.getFirstColumnManipulator();
    const auto z = inequalityDual.getFirstColumnManipulator();
    const auto ds = affine.slack.getFirstColumnManipulator();
    const auto dz = affine.inequalityDual.getFirstColumnManipulator();
    auto rc = result.getFirstColumnManipulator();
    for (unsigned int row = 0; row < slack.getNoOfRows(); ++row)
    {
        rc(row) = s(row) * z(row)
                + ds(row) * dz(row)
                - sigma * mu;
    }
    return result;
}

void addScaled(
    dense::DblMatrix& value,
    const dense::DblMatrix& direction,
    const double step
)
{
    if (value.getNoOfRows() == 0)
        return;
    auto current = value.getFirstColumnManipulator();
    const auto delta = direction.getFirstColumnManipulator();
    for (unsigned int row = 0; row < value.getNoOfRows(); ++row)
        current(row) += step * delta(row);
}

void ensureStrictlyPositive(dense::DblMatrix& vector)
{
    auto values = vector.getFirstColumnManipulator();
    const double floor = 100.0 * std::numeric_limits<double>::epsilon();
    for (unsigned int row = 0; row < vector.getNoOfRows(); ++row)
        values(row) = std::max(values(row), floor);
}

void printIterationHeader()
{
    std::cout
        << "\n iter        objective       primal_res        dual_res"
        << "              mu     a_pri    a_dual      nnz\n";
    std::cout
        << "----------------------------------------------------------------------------"
        << "----------------\n";
}

void printIteration(const IterationStats& stats)
{
    std::cout << std::scientific << std::setprecision(5)
              << std::setw(5) << stats.iteration
              << std::setw(17) << stats.objective
              << std::setw(17) << stats.primalResidual
              << std::setw(17) << stats.dualResidual
              << std::setw(17) << stats.mu
              << std::fixed << std::setprecision(4)
              << std::setw(10) << stats.alphaPrimal
              << std::setw(10) << stats.alphaDual
              << std::setw(9) << stats.kktNonZeros
              << '\n';
}

Solution solveEqualityOnly(
    const QPProblem& problem,
    const SolverOptions& options
)
{
    Solution solution;
    const auto n = problem.variables();
    const auto p = problem.equalities();

    dense::DblMatrix hessian(
        static_cast<unsigned int>(n),
        static_cast<unsigned int>(n),
        nullptr,
        true
    );
    const auto q = problem.Q.getManipulator();
    auto h = hessian.getManipulator();
    for (unsigned int column = 0; column < n; ++column)
    {
        for (unsigned int row = 0; row < n; ++row)
            h(row, column) = 0.5 * (q(row, column) + q(column, row));
        h(column, column) += options.regularization;
    }

    const bool printKkt = options.printKkt
        && static_cast<int>(n + p) <= options.printKktMaxDimension;
    AugmentedKktSystem kkt(
        hessian,
        problem.A,
        options.sparseZeroTolerance,
        printKkt
    );

    dense::DblMatrix rhsX = makeVector(n);
    auto bx = rhsX.getFirstColumnManipulator();
    const auto c = problem.c.getFirstColumnManipulator();
    for (unsigned int row = 0; row < n; ++row)
        bx(row) = -c(row);

    dense::DblMatrix rhsY;
    dense::DblMatrix equalityDual;
    if (p > 0)
    {
        rhsY = makeVector(p);
        equalityDual = makeVector(p);
        auto by = rhsY.getFirstColumnManipulator();
        const auto b = problem.b.getFirstColumnManipulator();
        for (unsigned int row = 0; row < p; ++row)
            by(row) = b(row);
    }

    dense::DblMatrix x = makeVector(n);
    kkt.solve(rhsX, rhsY, x, equalityDual);

    dense::DblMatrix emptySlack;
    dense::DblMatrix emptyDual;
    const Residuals residuals = computeResiduals(
        problem,
        x,
        equalityDual,
        emptySlack,
        emptyDual
    );
    IterationStats stats = makeStats(
        0,
        problem,
        x,
        emptySlack,
        emptyDual,
        residuals
    );
    stats.kktNonZeros = kkt.nonZeros();
    stats.assemblyMilliseconds = kkt.assemblyMilliseconds();
    stats.factorizationMilliseconds = kkt.factorizationMilliseconds();
    stats.solveMilliseconds = kkt.solveMilliseconds();
    solution.history.push_back(stats);

    solution.x = x;
    solution.equalityDual = equalityDual;
    solution.objective = stats.objective;
    solution.iterations = 1;
    if (hasConverged(stats, options.tolerance * 10.0))
    {
        solution.status = SolverStatus::Converged;
        solution.message = "Equality-constrained QP solved by one natID LDLT KKT solve.";
    }
    else
    {
        solution.status = SolverStatus::NumericalFailure;
        solution.message = "Equality-constrained KKT solve returned large residuals.";
    }

    if (options.verbose)
    {
        printIterationHeader();
        printIteration(stats);
    }
    return solution;
}

} // namespace

const char* toString(const SolverStatus status)
{
    switch (status)
    {
        case SolverStatus::Converged:
            return "converged";
        case SolverStatus::MaximumIterations:
            return "maximum_iterations";
        case SolverStatus::InvalidProblem:
            return "invalid_problem";
        case SolverStatus::NumericalFailure:
            return "numerical_failure";
    }
    return "unknown";
}

InteriorPointSolver::InteriorPointSolver(SolverOptions options)
    : _options(std::move(options))
{
    if (_options.maxIterations <= 0)
        throw std::invalid_argument("maxIterations must be positive.");
    if (!(_options.tolerance > 0.0))
        throw std::invalid_argument("tolerance must be positive.");
    if (!(_options.fractionToBoundary > 0.0)
        || !(_options.fractionToBoundary < 1.0))
    {
        throw std::invalid_argument("fractionToBoundary must be between 0 and 1.");
    }
    if (_options.regularization < 0.0)
        throw std::invalid_argument("regularization must be nonnegative.");
    if (_options.sparseZeroTolerance < 0.0)
        throw std::invalid_argument("sparseZeroTolerance must be nonnegative.");
}

const SolverOptions& InteriorPointSolver::options() const
{
    return _options;
}

Solution InteriorPointSolver::solve(const QPProblem& problem) const
{
    Solution solution;
    const std::string validationError = problem.validate();
    if (!validationError.empty())
    {
        solution.status = SolverStatus::InvalidProblem;
        solution.message = validationError;
        return solution;
    }

    try
    {
        if (problem.inequalities() == 0)
            return solveEqualityOnly(problem, _options);

        const auto n = problem.variables();
        const auto p = problem.equalities();
        const auto m = problem.inequalities();

        dense::DblMatrix x = makeVector(n);
        dense::DblMatrix equalityDual;
        if (p > 0)
            equalityDual = makeVector(p);

        // Infeasible-start initialization: x=y=0, shift h-Gx to a strict interior.
        dense::DblMatrix slack = makeVector(m);
        const auto h = problem.h.getFirstColumnManipulator();
        auto s = slack.getFirstColumnManipulator();
        double minimumSlack = std::numeric_limits<double>::infinity();
        for (unsigned int row = 0; row < m; ++row)
        {
            s(row) = h(row);
            minimumSlack = std::min(minimumSlack, s(row));
        }
        const double slackShift = minimumSlack <= 0.0 ? 1.0 - minimumSlack : 0.0;
        for (unsigned int row = 0; row < m; ++row)
            s(row) += slackShift;

        dense::DblMatrix inequalityDual = makeVector(m, 1.0);

        if (_options.verbose)
            printIterationHeader();

        for (int iteration = 0; iteration < _options.maxIterations; ++iteration)
        {
            const Residuals residuals = computeResiduals(
                problem,
                x,
                equalityDual,
                slack,
                inequalityDual
            );
            IterationStats stats = makeStats(
                iteration,
                problem,
                x,
                slack,
                inequalityDual,
                residuals
            );
            if (!statsAreFinite(stats))
                throw std::runtime_error("A non-finite KKT residual was produced.");

            if (hasConverged(stats, _options.tolerance))
            {
                solution.history.push_back(stats);
                if (_options.verbose)
                    printIteration(stats);
                solution.status = SolverStatus::Converged;
                solution.message = "All scaled KKT residuals satisfy the requested tolerance.";
                solution.iterations = iteration;
                solution.objective = stats.objective;
                solution.x = x;
                solution.equalityDual = equalityDual;
                solution.slack = slack;
                solution.inequalityDual = inequalityDual;
                return solution;
            }

            const Clock::time_point hessianStart = Clock::now();
            dense::DblMatrix hessian = buildAugmentedHessian(
                problem,
                slack,
                inequalityDual,
                _options.regularization
            );
            const double hessianMilliseconds = elapsedMilliseconds(hessianStart);

            const bool printKkt = _options.printKkt
                && static_cast<int>(n + p) <= _options.printKktMaxDimension
                && (_options.printKktEveryIteration || iteration == 0);
            AugmentedKktSystem kkt(
                hessian,
                problem.A,
                _options.sparseZeroTolerance,
                printKkt
            );

            const dense::DblMatrix affineRc = affineComplementarity(
                slack,
                inequalityDual
            );
            const Direction affine = computeDirection(
                problem,
                slack,
                inequalityDual,
                residuals,
                affineRc,
                kkt
            );

            const double affineAlphaPrimal = std::min(
                1.0,
                maximumPositiveStep(slack, affine.slack)
            );
            const double affineAlphaDual = std::min(
                1.0,
                maximumPositiveStep(inequalityDual, affine.inequalityDual)
            );
            const double muAffine = std::max(
                0.0,
                affineMu(
                    slack,
                    inequalityDual,
                    affine,
                    affineAlphaPrimal,
                    affineAlphaDual
                )
            );
            stats.sigma = stats.mu > 0.0
                ? std::clamp(std::pow(muAffine / stats.mu, 3.0), 0.0, 1.0)
                : 0.0;

            const dense::DblMatrix correctorRc = correctorComplementarity(
                slack,
                inequalityDual,
                affine,
                stats.sigma,
                stats.mu
            );
            const Direction direction = computeDirection(
                problem,
                slack,
                inequalityDual,
                residuals,
                correctorRc,
                kkt
            );

            stats.alphaPrimal = std::min(
                1.0,
                _options.fractionToBoundary
                    * maximumPositiveStep(slack, direction.slack)
            );
            stats.alphaDual = std::min(
                1.0,
                _options.fractionToBoundary
                    * maximumPositiveStep(inequalityDual, direction.inequalityDual)
            );
            stats.kktNonZeros = kkt.nonZeros();
            stats.assemblyMilliseconds = hessianMilliseconds
                                       + kkt.assemblyMilliseconds();
            stats.factorizationMilliseconds = kkt.factorizationMilliseconds();
            stats.solveMilliseconds = kkt.solveMilliseconds();
            solution.history.push_back(stats);

            if (_options.verbose)
                printIteration(stats);

            addScaled(x, direction.x, stats.alphaPrimal);
            addScaled(slack, direction.slack, stats.alphaPrimal);
            addScaled(equalityDual, direction.equalityDual, stats.alphaDual);
            addScaled(
                inequalityDual,
                direction.inequalityDual,
                stats.alphaDual
            );
            ensureStrictlyPositive(slack);
            ensureStrictlyPositive(inequalityDual);
        }

        const Residuals finalResiduals = computeResiduals(
            problem,
            x,
            equalityDual,
            slack,
            inequalityDual
        );
        IterationStats finalStats = makeStats(
            _options.maxIterations,
            problem,
            x,
            slack,
            inequalityDual,
            finalResiduals
        );
        if (!statsAreFinite(finalStats))
            throw std::runtime_error("A non-finite final KKT residual was produced.");
        solution.history.push_back(finalStats);
        if (_options.verbose)
            printIteration(finalStats);

        solution.status = hasConverged(finalStats, _options.tolerance)
            ? SolverStatus::Converged
            : SolverStatus::MaximumIterations;
        solution.message = solution.status == SolverStatus::Converged
            ? "All scaled KKT residuals satisfy the requested tolerance."
            : "The iteration limit was reached before convergence.";
        solution.iterations = _options.maxIterations;
        solution.objective = finalStats.objective;
        solution.x = x;
        solution.equalityDual = equalityDual;
        solution.slack = slack;
        solution.inequalityDual = inequalityDual;
    }
    catch (const std::exception& error)
    {
        solution.status = SolverStatus::NumericalFailure;
        solution.message = error.what();
    }
    return solution;
}

void writeConvergenceCsv(const Solution& solution, const std::string& fileName)
{
    std::ofstream output(fileName);
    if (!output)
        throw std::runtime_error("Cannot create convergence CSV: " + fileName);

    output
        << "iteration,objective,primal_residual,dual_residual,duality_gap,mu,"
        << "scaled_primal_residual,scaled_dual_residual,"
        << "scaled_complementarity,alpha_primal,alpha_dual,sigma,kkt_nnz,"
        << "assembly_ms,factorization_ms,solve_ms\n";
    output << std::setprecision(17);
    for (const IterationStats& stats : solution.history)
    {
        output
            << stats.iteration << ','
            << stats.objective << ','
            << stats.primalResidual << ','
            << stats.dualResidual << ','
            << stats.dualityGap << ','
            << stats.mu << ','
            << stats.scaledPrimalResidual << ','
            << stats.scaledDualResidual << ','
            << stats.scaledComplementarity << ','
            << stats.alphaPrimal << ','
            << stats.alphaDual << ','
            << stats.sigma << ','
            << stats.kktNonZeros << ','
            << stats.assemblyMilliseconds << ','
            << stats.factorizationMilliseconds << ','
            << stats.solveMilliseconds << '\n';
    }
}

void writeSolutionReport(
    const QPProblem& problem,
    const Solution& solution,
    std::ostream& output
)
{
    output << "\nSolver status: " << toString(solution.status) << '\n';
    output << "Message: " << solution.message << '\n';
    output << "Iterations: " << solution.iterations << '\n';
    output << std::scientific << std::setprecision(12);
    output << "Objective: " << solution.objective << '\n';

    const auto writeVector = [&output](
        const char* name,
        const dense::DblMatrix& vector
    )
    {
        output << name << " = [";
        if (vector.getNoOfRows() > 0)
        {
            const auto values = vector.getFirstColumnManipulator();
            for (unsigned int row = 0; row < vector.getNoOfRows(); ++row)
            {
                if (row > 0)
                    output << "; ";
                output << values(row);
            }
        }
        output << "]\n";
    };

    writeVector("x", solution.x);
    if (problem.equalities() > 0)
        writeVector("y (equality dual)", solution.equalityDual);
    if (problem.inequalities() > 0)
    {
        writeVector("s (slack)", solution.slack);
        writeVector("z (inequality dual)", solution.inequalityDual);
    }

    if (!solution.history.empty())
    {
        const IterationStats& final = solution.history.back();
        output << "Final primal residual (inf): " << final.primalResidual << '\n';
        output << "Final dual residual (inf): " << final.dualResidual << '\n';
        output << "Final duality gap: " << final.dualityGap << '\n';
        output << "Final complementarity mu: " << final.mu << '\n';
    }
}

} // namespace natid_qp
