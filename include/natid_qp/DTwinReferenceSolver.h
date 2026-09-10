#pragma once

#include "natid_qp/InteriorPointSolver.h"
#include "natid_qp/QPProblem.h"

#include <string>
#include <vector>

namespace natid_qp
{

enum class DTwinStatus
{
    NotRun,
    Solved,
    ModelInitializationFailure,
    SolverFailure,
    InvalidResult
};

const char* toString(DTwinStatus status);

struct DTwinReferenceOptions
{
    double tolerance = 1e-9;
    double complementaritySmoothing = 0.0;
    int maxIterations = 200;
    bool retryWithNatIDWarmStart = true;
};

struct DTwinReferenceResult
{
    DTwinStatus status = DTwinStatus::NotRun;
    std::string message;
    std::vector<double> x;
    std::vector<double> equalityDual;
    std::vector<double> slack;
    std::vector<double> inequalityDual;
    double objective = 0.0;
    double primalResidual = 0.0;
    double stationarityResidual = 0.0;
    double complementarityResidual = 0.0;
    double nonnegativityViolation = 0.0;
    double kktResidual = 0.0;
    bool usedWarmStart = false;

    [[nodiscard]] bool solved() const
    {
        return status == DTwinStatus::Solved;
    }
};

enum class MatchLevel
{
    NotCompared,
    ExactMatch,
    CloseMatch,
    PartialMatch,
    Mismatch
};

const char* toString(MatchLevel level);

struct SolutionComparison
{
    MatchLevel level = MatchLevel::NotCompared;
    std::string message;
    double maxAbsoluteXDifference = 0.0;
    double relativeXDifference = 0.0;
    double absoluteObjectiveDifference = 0.0;
    double relativeObjectiveDifference = 0.0;
    double exactThreshold = 0.0;
    double closeThreshold = 0.0;
    double partialThreshold = 0.0;
};

// Creates an in-memory dTwin NLE model for the QP KKT equations. A null
// warmStart produces a deterministic neutral initial point.
std::string buildDTwinKktModel(
    const QPProblem& problem,
    const Solution* warmStart,
    const DTwinReferenceOptions& options = {}
);

// Evaluates objective, feasibility, stationarity, complementarity and sign
// conditions for values returned by dTwin.
void evaluateDTwinReference(
    const QPProblem& problem,
    DTwinReferenceResult& result
);

// Implemented by the optional natid_qp_dtwin target and backed by
// sc::IModel/modSolver from the natID SDK.
DTwinReferenceResult solveWithDTwin(
    const QPProblem& problem,
    const Solution& natidSolution,
    const DTwinReferenceOptions& options = {}
);

SolutionComparison compareWithDTwin(
    const Solution& natidSolution,
    const DTwinReferenceResult& dtwinSolution,
    double requestedTolerance
);

} // namespace natid_qp
