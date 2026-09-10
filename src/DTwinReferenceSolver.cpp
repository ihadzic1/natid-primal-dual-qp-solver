#include "natid_qp/DTwinReferenceSolver.h"

#include <sc/IModel.h>
#include <sc/ISolver.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace natid_qp
{
namespace
{

class ModelGuard final
{
private:
    sc::IRealStaticModel* _model = nullptr;

public:
    explicit ModelGuard(sc::IRealStaticModel* model)
        : _model(model)
    {
    }

    ~ModelGuard()
    {
        if (_model)
            _model->release();
    }

    ModelGuard(const ModelGuard&) = delete;
    ModelGuard& operator=(const ModelGuard&) = delete;
};

bool copyNamedVariables(
    sc::IRealStaticModel& model,
    const char* prefix,
    const std::size_t count,
    const sc::IRealStaticModel::ValueVector& allValues,
    std::vector<double>& output
)
{
    output.assign(count, 0.0);
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::string name = std::string(prefix) + std::to_string(index + 1);
        const ssize_t position = model.getVariableIndex(name.c_str());
        if (position < 0 || static_cast<std::size_t>(position) >= allValues.size())
            return false;
        output[index] = allValues[static_cast<std::size_t>(position)];
        if (!std::isfinite(output[index]))
            return false;
    }
    return true;
}

DTwinReferenceResult runAttempt(
    const QPProblem& problem,
    const Solution* warmStart,
    const DTwinReferenceOptions& options
)
{
    DTwinReferenceResult result;
    result.usedWarmStart = warmStart != nullptr;

    sc::IRealStaticModel* rawModel = sc::createRealStaticModel(
        sc::IStatic::Problem::NLE,
        nullptr,
        static_cast<unsigned int>(std::max(1, options.maxIterations))
    );
    if (!rawModel)
    {
        result.status = DTwinStatus::ModelInitializationFailure;
        result.message = "dTwin could not create a real static NLE model.";
        return result;
    }
    ModelGuard guard(rawModel);

    const std::string modelText = buildDTwinKktModel(problem, warmStart, options);
    if (!rawModel->initFromString(td::String(modelText.c_str())))
    {
        result.status = DTwinStatus::ModelInitializationFailure;
        result.message = "dTwin could not parse the generated KKT model.";
        return result;
    }

    sc::SolutionOptions solverOptions;
    const double requestedTolerance = std::isfinite(options.tolerance)
        ? options.tolerance
        : 1e-9;
    solverOptions.eps = std::clamp(requestedTolerance, 1e-10, 1e-4);
    solverOptions.maxIter = static_cast<double>(std::max(1, options.maxIterations));
    rawModel->setSolutionOptions(solverOptions);

    sc::IStatic* solver = rawModel->getSolverInterface();
    if (!solver)
    {
        result.status = DTwinStatus::ModelInitializationFailure;
        result.message = "dTwin did not expose its static solver interface.";
        return result;
    }

    const sc::Solution status = solver->solve();
    if (status != sc::Solution::OK && status != sc::Solution::BaseOK)
    {
        result.status = DTwinStatus::SolverFailure;
        result.message = std::string("dTwin returned status: ")
            + sc::getSolutionStatusStr(status);
        return result;
    }

    sc::IRealStaticModel::ValueVector values;
    rawModel->getVariableValues(values);
    if (!copyNamedVariables(*rawModel, "x_", problem.variables(), values, result.x)
        || !copyNamedVariables(
            *rawModel,
            "y_",
            problem.equalities(),
            values,
            result.equalityDual
        )
        || !copyNamedVariables(
            *rawModel,
            "s_",
            problem.inequalities(),
            values,
            result.slack
        )
        || !copyNamedVariables(
            *rawModel,
            "z_",
            problem.inequalities(),
            values,
            result.inequalityDual
        ))
    {
        result.status = DTwinStatus::InvalidResult;
        result.message = "dTwin returned an incomplete KKT variable vector.";
        return result;
    }

    result.status = DTwinStatus::Solved;
    result.message = result.usedWarmStart
        ? "dTwin solved the generated KKT model after a NatIDQP warm start."
        : "dTwin solved the generated KKT model from a neutral start.";
    evaluateDTwinReference(problem, result);
    return result;
}

} // namespace

DTwinReferenceResult solveWithDTwin(
    const QPProblem& problem,
    const Solution& natidSolution,
    const DTwinReferenceOptions& options
)
{
    DTwinReferenceResult result = runAttempt(problem, nullptr, options);
    if (result.solved() || !options.retryWithNatIDWarmStart
        || !natidSolution.converged())
    {
        return result;
    }

    DTwinReferenceResult warmResult = runAttempt(problem, &natidSolution, options);
    if (!warmResult.solved())
    {
        warmResult.message = result.message + " Warm-start retry: "
            + warmResult.message;
    }
    return warmResult;
}

} // namespace natid_qp
