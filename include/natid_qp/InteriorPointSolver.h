#pragma once

#include "natid_qp/QPProblem.h"

#include <dense/Matrix.h>

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace natid_qp
{

enum class SolverStatus
{
    Converged,
    MaximumIterations,
    InvalidProblem,
    NumericalFailure
};

const char* toString(SolverStatus status);

struct SolverOptions
{
    int maxIterations = 100;
    double tolerance = 1e-8;
    double fractionToBoundary = 0.995;
    double regularization = 1e-10;
    double sparseZeroTolerance = 1e-14;
    bool verbose = true;
    bool printKkt = true;
    bool printKktEveryIteration = false;
    int printKktMaxDimension = 12;
};

struct IterationStats
{
    int iteration = 0;
    double objective = 0.0;
    double primalResidual = 0.0;
    double dualResidual = 0.0;
    double dualityGap = 0.0;
    double mu = 0.0;
    double scaledPrimalResidual = 0.0;
    double scaledDualResidual = 0.0;
    double scaledComplementarity = 0.0;
    double alphaPrimal = 0.0;
    double alphaDual = 0.0;
    double sigma = 0.0;
    std::size_t kktNonZeros = 0;
    double assemblyMilliseconds = 0.0;
    double factorizationMilliseconds = 0.0;
    double solveMilliseconds = 0.0;
};

struct Solution
{
    SolverStatus status = SolverStatus::InvalidProblem;
    std::string message;
    int iterations = 0;
    double objective = 0.0;

    dense::DblMatrix x;
    dense::DblMatrix equalityDual;
    dense::DblMatrix slack;
    dense::DblMatrix inequalityDual;

    std::vector<IterationStats> history;

    [[nodiscard]] bool converged() const
    {
        return status == SolverStatus::Converged;
    }
};

class InteriorPointSolver
{
public:
    explicit InteriorPointSolver(SolverOptions options = {});

    [[nodiscard]] const SolverOptions& options() const;
    Solution solve(const QPProblem& problem) const;

private:
    SolverOptions _options;
};

void writeConvergenceCsv(const Solution& solution, const std::string& fileName);
void writeSolutionReport(
    const QPProblem& problem,
    const Solution& solution,
    std::ostream& output
);

} // namespace natid_qp
