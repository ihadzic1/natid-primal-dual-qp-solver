#include "natid_qp/QPProblem.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <sstream>

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

bool matrixIsFinite(const dense::DblMatrix& matrix)
{
    const auto rows = matrix.getNoOfRows();
    const auto columns = matrix.getNoOfCols();
    if (rows == 0 || columns == 0)
        return true;

    const auto values = matrix.getManipulator();
    for (unsigned int column = 0; column < columns; ++column)
    {
        for (unsigned int row = 0; row < rows; ++row)
        {
            if (!std::isfinite(values(row, column)))
                return false;
        }
    }
    return true;
}

void fillVector(dense::DblMatrix& vector, const std::initializer_list<double> values)
{
    auto output = vector.getFirstColumnManipulator();
    unsigned int row = 0;
    for (const double value : values)
        output(row++) = value;
}

} // namespace

QPProblem::QPProblem(
    const std::size_t variableCount,
    const std::size_t equalityCount,
    const std::size_t inequalityCount
)
{
    Q.reserve(
        static_cast<unsigned int>(variableCount),
        static_cast<unsigned int>(variableCount),
        nullptr,
        true
    );
    c.reserve(static_cast<unsigned int>(variableCount), 1, nullptr, true);

    if (equalityCount > 0)
    {
        A.reserve(
            static_cast<unsigned int>(equalityCount),
            static_cast<unsigned int>(variableCount),
            nullptr,
            true
        );
        b.reserve(static_cast<unsigned int>(equalityCount), 1, nullptr, true);
    }

    if (inequalityCount > 0)
    {
        G.reserve(
            static_cast<unsigned int>(inequalityCount),
            static_cast<unsigned int>(variableCount),
            nullptr,
            true
        );
        h.reserve(static_cast<unsigned int>(inequalityCount), 1, nullptr, true);
    }
}

std::size_t QPProblem::variables() const
{
    return static_cast<std::size_t>(Q.getNoOfRows());
}

std::size_t QPProblem::equalities() const
{
    return static_cast<std::size_t>(A.getNoOfRows());
}

std::size_t QPProblem::inequalities() const
{
    return static_cast<std::size_t>(G.getNoOfRows());
}

std::string QPProblem::validate(const double symmetryTolerance) const
{
    const auto n = Q.getNoOfRows();
    const auto p = A.getNoOfRows();
    const auto m = G.getNoOfRows();

    if (n == 0)
        return "Q must contain at least one variable.";
    if (Q.getNoOfCols() != n)
        return "Q must be square.";
    if (c.getNoOfRows() != n || c.getNoOfCols() != 1)
        return "c must be an n-by-1 vector.";

    if (p > 0)
    {
        if (A.getNoOfCols() != n)
            return "A must have n columns.";
        if (b.getNoOfRows() != p || b.getNoOfCols() != 1)
            return "b must be a p-by-1 vector.";
    }
    else if (b.getNoOfRows() != 0 || b.getNoOfCols() != 0)
    {
        return "b was supplied without equality constraints A.";
    }

    if (m > 0)
    {
        if (G.getNoOfCols() != n)
            return "G must have n columns.";
        if (h.getNoOfRows() != m || h.getNoOfCols() != 1)
            return "h must be an m-by-1 vector.";
    }
    else if (h.getNoOfRows() != 0 || h.getNoOfCols() != 0)
    {
        return "h was supplied without inequality constraints G.";
    }

    if (!matrixIsFinite(Q) || !matrixIsFinite(c) || !matrixIsFinite(A)
        || !matrixIsFinite(b) || !matrixIsFinite(G) || !matrixIsFinite(h))
    {
        return "All problem data must be finite.";
    }

    const auto q = Q.getManipulator();
    for (unsigned int column = 0; column < n; ++column)
    {
        for (unsigned int row = 0; row < column; ++row)
        {
            const double scale = 1.0 + std::max(
                std::abs(q(row, column)),
                std::abs(q(column, row))
            );
            if (std::abs(q(row, column) - q(column, row))
                > symmetryTolerance * scale)
            {
                std::ostringstream message;
                message << "Q is not symmetric at (" << row << ", " << column << ").";
                return message.str();
            }
        }
    }

    return {};
}

QPProblem makeEqualityDemoProblem()
{
    QPProblem problem(2, 1, 2);

    auto q = problem.Q.getManipulator();
    q(0, 0) = 4.0;
    q(0, 1) = 1.0;
    q(1, 0) = 1.0;
    q(1, 1) = 2.0;
    fillVector(problem.c, {-1.0, -1.0});

    auto a = problem.A.getManipulator();
    a(0, 0) = 1.0;
    a(0, 1) = 1.0;
    fillVector(problem.b, {1.0});

    auto g = problem.G.getManipulator();
    g(0, 0) = -1.0;
    g(1, 1) = -1.0;
    fillVector(problem.h, {0.0, 0.0});

    return problem;
}

QPProblem makeInequalityDemoProblem()
{
    QPProblem problem(2, 0, 3);

    auto q = problem.Q.getManipulator();
    q(0, 0) = 1.0;
    q(1, 1) = 1.0;
    fillVector(problem.c, {-1.5, -1.0});

    auto g = problem.G.getManipulator();
    g(0, 0) = 1.0;
    g(0, 1) = 1.0;
    g(1, 0) = -1.0;
    g(2, 1) = -1.0;
    fillVector(problem.h, {1.0, 0.0, 0.0});

    return problem;
}

} // namespace natid_qp
