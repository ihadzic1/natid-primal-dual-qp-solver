#pragma once

#include <dense/Matrix.h>

#include <cstddef>
#include <string>

namespace natid_qp
{

class QPProblem
{
public:
    dense::DblMatrix Q;
    dense::DblMatrix c;
    dense::DblMatrix A;
    dense::DblMatrix b;
    dense::DblMatrix G;
    dense::DblMatrix h;

    QPProblem() = default;
    QPProblem(std::size_t variables, std::size_t equalities, std::size_t inequalities);

    [[nodiscard]] std::size_t variables() const;
    [[nodiscard]] std::size_t equalities() const;
    [[nodiscard]] std::size_t inequalities() const;

    // Returns an empty string when the dimensions and values are valid.
    [[nodiscard]] std::string validate(double symmetryTolerance = 1e-10) const;
};

// min 0.5*x'Q*x + c'x, x1+x2=1, x>=0; optimum (0.25, 0.75).
QPProblem makeEqualityDemoProblem();

// Projection-like QP with an active inequality; optimum (0.75, 0.25).
QPProblem makeInequalityDemoProblem();

} // namespace natid_qp
