#include "natid_qp/DTwinReferenceSolver.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

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

double matrixValue(
    const dense::DblMatrix& matrix,
    const unsigned int row,
    const unsigned int column = 0
)
{
    return matrix.getManipulator()(row, column);
}

double vectorValue(const dense::DblMatrix& vector, const unsigned int row)
{
    return vector.getFirstColumnManipulator()(row);
}

double finiteOr(const double value, const double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

double smoothingValue(const DTwinReferenceOptions& options)
{
    if (std::isfinite(options.complementaritySmoothing)
        && options.complementaritySmoothing > 0.0)
    {
        return options.complementaritySmoothing;
    }

    const double tolerance = std::clamp(
        finiteOr(options.tolerance, 1e-9),
        1e-12,
        1e-3
    );
    return std::clamp(tolerance * tolerance, 1e-14, 1e-8);
}

std::string number(const double value)
{
    if (!std::isfinite(value))
        throw std::invalid_argument("dTwin KKT model contains a non-finite value.");

    std::ostringstream output;
    output << std::scientific << std::setprecision(17)
           << (std::abs(value) < 1e-18 ? 0.0 : value);
    return output.str();
}

void appendTerm(
    std::ostringstream& expression,
    const double coefficient,
    const std::string& variable,
    bool& hasTerm
)
{
    if (std::abs(coefficient) <= 1e-18)
        return;
    if (hasTerm)
        expression << " + ";
    expression << '(' << number(coefficient) << ")*" << variable;
    hasTerm = true;
}

void appendConstant(
    std::ostringstream& expression,
    const double value,
    bool& hasTerm
)
{
    if (std::abs(value) <= 1e-18)
        return;
    if (hasTerm)
        expression << " + ";
    expression << '(' << number(value) << ')';
    hasTerm = true;
}

std::vector<double> initialX(
    const QPProblem& problem,
    const Solution* warmStart
)
{
    std::vector<double> values(problem.variables(), 0.0);
    if (!warmStart || warmStart->x.getNoOfRows() != problem.variables())
        return values;

    for (unsigned int row = 0; row < problem.variables(); ++row)
        values[row] = finiteOr(vectorValue(warmStart->x, row), 0.0);
    return values;
}

std::vector<double> initialEqualityDual(
    const QPProblem& problem,
    const Solution* warmStart
)
{
    std::vector<double> values(problem.equalities(), 0.0);
    if (!warmStart
        || warmStart->equalityDual.getNoOfRows() != problem.equalities())
    {
        return values;
    }

    for (unsigned int row = 0; row < problem.equalities(); ++row)
        values[row] = finiteOr(vectorValue(warmStart->equalityDual, row), 0.0);
    return values;
}

std::vector<double> initialSlack(
    const QPProblem& problem,
    const Solution* warmStart,
    const double smoothing
)
{
    const double floor = std::sqrt(smoothing);
    std::vector<double> values(problem.inequalities(), 1.0);
    if (warmStart && warmStart->slack.getNoOfRows() == problem.inequalities())
    {
        for (unsigned int row = 0; row < problem.inequalities(); ++row)
        {
            values[row] = std::max(
                floor,
                finiteOr(vectorValue(warmStart->slack, row), floor)
            );
        }
        return values;
    }

    for (unsigned int row = 0; row < problem.inequalities(); ++row)
    {
        values[row] = std::max(
            1.0,
            std::abs(vectorValue(problem.h, row))
        );
    }
    return values;
}

std::vector<double> initialInequalityDual(
    const QPProblem& problem,
    const Solution* warmStart,
    const double smoothing
)
{
    const double floor = std::sqrt(smoothing);
    std::vector<double> values(problem.inequalities(), 1.0);
    if (!warmStart
        || warmStart->inequalityDual.getNoOfRows() != problem.inequalities())
    {
        return values;
    }

    for (unsigned int row = 0; row < problem.inequalities(); ++row)
    {
        values[row] = std::max(
            floor,
            finiteOr(vectorValue(warmStart->inequalityDual, row), floor)
        );
    }
    return values;
}

double vectorInfinityNorm(const std::vector<double>& values)
{
    double result = 0.0;
    for (const double value : values)
        result = std::max(result, std::abs(value));
    return result;
}

bool vectorIsFinite(const std::vector<double>& values)
{
    return std::all_of(
        values.begin(),
        values.end(),
        [](const double value) { return std::isfinite(value); }
    );
}

} // namespace

const char* toString(const DTwinStatus status)
{
    switch (status)
    {
        case DTwinStatus::NotRun:
            return "not run";
        case DTwinStatus::Solved:
            return "solved";
        case DTwinStatus::ModelInitializationFailure:
            return "model initialization failed";
        case DTwinStatus::SolverFailure:
            return "solver failed";
        case DTwinStatus::InvalidResult:
            return "invalid result";
    }
    return "unknown";
}

const char* toString(const MatchLevel level)
{
    switch (level)
    {
        case MatchLevel::NotCompared:
            return "Not compared";
        case MatchLevel::ExactMatch:
            return "Exact match";
        case MatchLevel::CloseMatch:
            return "Close match";
        case MatchLevel::PartialMatch:
            return "Partial match";
        case MatchLevel::Mismatch:
            return "Mismatch";
    }
    return "Unknown";
}

std::string buildDTwinKktModel(
    const QPProblem& problem,
    const Solution* warmStart,
    const DTwinReferenceOptions& options
)
{
    const std::string validation = problem.validate();
    if (!validation.empty())
        throw std::invalid_argument(validation);
    if (options.maxIterations <= 0)
        throw std::invalid_argument("dTwin maxIterations must be positive.");

    const std::size_t n = problem.variables();
    const std::size_t p = problem.equalities();
    const std::size_t m = problem.inequalities();
    const double smoothing = smoothingValue(options);
    const double solverTolerance = std::clamp(
        finiteOr(options.tolerance, 1e-9),
        1e-10,
        1e-4
    );

    const std::vector<double> x0 = initialX(problem, warmStart);
    const std::vector<double> y0 = initialEqualityDual(problem, warmStart);
    const std::vector<double> s0 = initialSlack(problem, warmStart, smoothing);
    const std::vector<double> z0 =
        initialInequalityDual(problem, warmStart, smoothing);

    std::ostringstream model;
    model << "Header:\n"
          << "\tmaxIter=" << options.maxIterations << "\n"
          << "\treport=Solved\n"
          << "\toutToTxt=false\n"
          << "end\n"
          << "Model [type=NL, name=\"NatIDQP_KKT_Reference\", domain=real, "
          << "eps=" << number(solverTolerance) << ", method=NR]:\n"
          << "\tVars [out=true]:\n";

    for (std::size_t index = 0; index < n; ++index)
        model << "\t\tx_" << index + 1 << '=' << number(x0[index]) << "\n";
    for (std::size_t index = 0; index < p; ++index)
        model << "\t\ty_" << index + 1 << '=' << number(y0[index]) << "\n";
    for (std::size_t index = 0; index < m; ++index)
        model << "\t\ts_" << index + 1 << '=' << number(s0[index]) << "\n";
    for (std::size_t index = 0; index < m; ++index)
        model << "\t\tz_" << index + 1 << '=' << number(z0[index]) << "\n";

    model << "\tParams:\n"
          << "\t\tfb_tau=" << number(smoothing) << "\n"
          << "\tNLEs:\n";

    // Stationarity: Qx + c + A^T y + G^T z = 0.
    for (std::size_t column = 0; column < n; ++column)
    {
        bool hasTerm = false;
        model << "\t\t";
        for (std::size_t row = 0; row < n; ++row)
        {
            appendTerm(
                model,
                matrixValue(problem.Q, static_cast<unsigned int>(column),
                            static_cast<unsigned int>(row)),
                "x_" + std::to_string(row + 1),
                hasTerm
            );
        }
        appendConstant(
            model,
            vectorValue(problem.c, static_cast<unsigned int>(column)),
            hasTerm
        );
        for (std::size_t row = 0; row < p; ++row)
        {
            appendTerm(
                model,
                matrixValue(problem.A, static_cast<unsigned int>(row),
                            static_cast<unsigned int>(column)),
                "y_" + std::to_string(row + 1),
                hasTerm
            );
        }
        for (std::size_t row = 0; row < m; ++row)
        {
            appendTerm(
                model,
                matrixValue(problem.G, static_cast<unsigned int>(row),
                            static_cast<unsigned int>(column)),
                "z_" + std::to_string(row + 1),
                hasTerm
            );
        }
        if (!hasTerm)
            model << '0';
        model << "=0\n";
    }

    // Equality feasibility: Ax - b = 0.
    for (std::size_t row = 0; row < p; ++row)
    {
        bool hasTerm = false;
        model << "\t\t";
        for (std::size_t column = 0; column < n; ++column)
        {
            appendTerm(
                model,
                matrixValue(problem.A, static_cast<unsigned int>(row),
                            static_cast<unsigned int>(column)),
                "x_" + std::to_string(column + 1),
                hasTerm
            );
        }
        appendConstant(
            model,
            -vectorValue(problem.b, static_cast<unsigned int>(row)),
            hasTerm
        );
        if (!hasTerm)
            model << '0';
        model << "=0\n";
    }

    // Inequality feasibility: Gx + s - h = 0.
    for (std::size_t row = 0; row < m; ++row)
    {
        bool hasTerm = false;
        model << "\t\t";
        for (std::size_t column = 0; column < n; ++column)
        {
            appendTerm(
                model,
                matrixValue(problem.G, static_cast<unsigned int>(row),
                            static_cast<unsigned int>(column)),
                "x_" + std::to_string(column + 1),
                hasTerm
            );
        }
        appendTerm(model, 1.0, "s_" + std::to_string(row + 1), hasTerm);
        appendConstant(
            model,
            -vectorValue(problem.h, static_cast<unsigned int>(row)),
            hasTerm
        );
        model << "=0\n";
    }

    // Smooth Fischer-Burmeister equations. For fb_tau > 0 these impose
    // positive s/z and s_i*z_i = fb_tau while remaining differentiable.
    for (std::size_t row = 0; row < m; ++row)
    {
        const std::string index = std::to_string(row + 1);
        model << "\t\tsqrt(s_" << index << "^2+z_" << index
              << "^2+2*fb_tau)-s_" << index << "-z_" << index << "=0\n";
    }

    model << "end\n";
    return model.str();
}

void evaluateDTwinReference(
    const QPProblem& problem,
    DTwinReferenceResult& result
)
{
    const std::size_t n = problem.variables();
    const std::size_t p = problem.equalities();
    const std::size_t m = problem.inequalities();
    if (result.x.size() != n || result.equalityDual.size() != p
        || result.slack.size() != m || result.inequalityDual.size() != m
        || !vectorIsFinite(result.x) || !vectorIsFinite(result.equalityDual)
        || !vectorIsFinite(result.slack)
        || !vectorIsFinite(result.inequalityDual))
    {
        result.status = DTwinStatus::InvalidResult;
        result.message = "dTwin returned missing or non-finite KKT variables.";
        return;
    }

    double objective = 0.0;
    double stationarity = 0.0;
    for (std::size_t row = 0; row < n; ++row)
    {
        double qx = 0.0;
        for (std::size_t column = 0; column < n; ++column)
        {
            qx += matrixValue(
                problem.Q,
                static_cast<unsigned int>(row),
                static_cast<unsigned int>(column)
            ) * result.x[column];
        }

        objective += 0.5 * result.x[row] * qx
            + vectorValue(problem.c, static_cast<unsigned int>(row)) * result.x[row];

        double residual = qx
            + vectorValue(problem.c, static_cast<unsigned int>(row));
        for (std::size_t equality = 0; equality < p; ++equality)
        {
            residual += matrixValue(
                problem.A,
                static_cast<unsigned int>(equality),
                static_cast<unsigned int>(row)
            ) * result.equalityDual[equality];
        }
        for (std::size_t inequality = 0; inequality < m; ++inequality)
        {
            residual += matrixValue(
                problem.G,
                static_cast<unsigned int>(inequality),
                static_cast<unsigned int>(row)
            ) * result.inequalityDual[inequality];
        }
        stationarity = std::max(stationarity, std::abs(residual));
    }

    double equalityResidual = 0.0;
    for (std::size_t row = 0; row < p; ++row)
    {
        double residual = -vectorValue(problem.b, static_cast<unsigned int>(row));
        for (std::size_t column = 0; column < n; ++column)
        {
            residual += matrixValue(
                problem.A,
                static_cast<unsigned int>(row),
                static_cast<unsigned int>(column)
            ) * result.x[column];
        }
        equalityResidual = std::max(equalityResidual, std::abs(residual));
    }

    double inequalityEquationResidual = 0.0;
    double inequalityViolation = 0.0;
    double complementarity = 0.0;
    double nonnegativity = 0.0;
    for (std::size_t row = 0; row < m; ++row)
    {
        double gxMinusH = -vectorValue(problem.h, static_cast<unsigned int>(row));
        for (std::size_t column = 0; column < n; ++column)
        {
            gxMinusH += matrixValue(
                problem.G,
                static_cast<unsigned int>(row),
                static_cast<unsigned int>(column)
            ) * result.x[column];
        }
        inequalityViolation = std::max(inequalityViolation, gxMinusH);
        inequalityEquationResidual = std::max(
            inequalityEquationResidual,
            std::abs(gxMinusH + result.slack[row])
        );
        complementarity = std::max(
            complementarity,
            std::abs(result.slack[row] * result.inequalityDual[row])
        );
        nonnegativity = std::max(
            nonnegativity,
            std::max(-result.slack[row], -result.inequalityDual[row])
        );
    }

    result.objective = objective;
    result.primalResidual = std::max(
        equalityResidual,
        std::max(0.0, inequalityViolation)
    );
    result.stationarityResidual = stationarity;
    result.complementarityResidual = complementarity;
    result.nonnegativityViolation = std::max(0.0, nonnegativity);
    result.kktResidual = std::max({
        result.primalResidual,
        inequalityEquationResidual,
        result.stationarityResidual,
        result.complementarityResidual,
        result.nonnegativityViolation
    });
}

SolutionComparison compareWithDTwin(
    const Solution& natidSolution,
    const DTwinReferenceResult& dtwinSolution,
    const double requestedTolerance
)
{
    SolutionComparison comparison;
    const double tolerance = std::clamp(
        finiteOr(requestedTolerance, 1e-9),
        1e-12,
        1.0
    );
    comparison.exactThreshold = std::max(
        1e-8,
        std::min(1e-6, 10.0 * tolerance)
    );
    comparison.closeThreshold = std::max(
        1e-6,
        std::min(1e-3, 100.0 * tolerance)
    );
    comparison.partialThreshold = std::max(
        1e-3,
        std::min(5e-2, 1000.0 * tolerance)
    );

    if (!natidSolution.converged())
    {
        comparison.message = "NatIDQP did not converge, so no reference comparison was made.";
        return comparison;
    }
    if (!dtwinSolution.solved())
    {
        comparison.message = "dTwin reference solve was unavailable or failed.";
        return comparison;
    }
    if (natidSolution.x.getNoOfRows() != dtwinSolution.x.size()
        || !std::isfinite(natidSolution.objective)
        || !std::isfinite(dtwinSolution.objective))
    {
        comparison.level = MatchLevel::Mismatch;
        comparison.message = "The two solvers returned incompatible result dimensions.";
        return comparison;
    }

    std::vector<double> natidX(dtwinSolution.x.size(), 0.0);
    const auto primaryX = natidSolution.x.getFirstColumnManipulator();
    for (std::size_t index = 0; index < dtwinSolution.x.size(); ++index)
    {
        natidX[index] = primaryX(static_cast<unsigned int>(index));
        comparison.maxAbsoluteXDifference = std::max(
            comparison.maxAbsoluteXDifference,
            std::abs(natidX[index] - dtwinSolution.x[index])
        );
    }

    const double xScale = std::max({
        1.0,
        vectorInfinityNorm(natidX),
        vectorInfinityNorm(dtwinSolution.x)
    });
    comparison.relativeXDifference = comparison.maxAbsoluteXDifference / xScale;
    comparison.absoluteObjectiveDifference = std::abs(
        natidSolution.objective - dtwinSolution.objective
    );
    const double objectiveScale = std::max({
        1.0,
        std::abs(natidSolution.objective),
        std::abs(dtwinSolution.objective)
    });
    comparison.relativeObjectiveDifference =
        comparison.absoluteObjectiveDifference / objectiveScale;

    const double exactKktLimit = std::max(1e-7, 10.0 * comparison.exactThreshold);
    const double closeKktLimit = std::max(1e-5, 10.0 * comparison.closeThreshold);
    const double partialKktLimit = std::max(1e-3, 10.0 * comparison.partialThreshold);

    if (comparison.relativeXDifference <= comparison.exactThreshold
        && comparison.relativeObjectiveDifference <= comparison.exactThreshold
        && dtwinSolution.kktResidual <= exactKktLimit)
    {
        comparison.level = MatchLevel::ExactMatch;
        comparison.message = "The solutions agree to the strict numerical threshold.";
    }
    else if (comparison.relativeXDifference <= comparison.closeThreshold
             && comparison.relativeObjectiveDifference <= comparison.closeThreshold
             && dtwinSolution.kktResidual <= closeKktLimit)
    {
        comparison.level = MatchLevel::CloseMatch;
        comparison.message = "The solutions agree within normal floating-point tolerance.";
    }
    else if (comparison.relativeObjectiveDifference <= comparison.partialThreshold
             && dtwinSolution.kktResidual <= partialKktLimit)
    {
        comparison.level = MatchLevel::PartialMatch;
        comparison.message =
            "The objective agrees, but x differs or the dTwin KKT residual is looser.";
    }
    else
    {
        comparison.level = MatchLevel::Mismatch;
        comparison.message = "The objective or KKT solution differs materially.";
    }

    return comparison;
}

} // namespace natid_qp
