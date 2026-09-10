#include "natid_qp/DTwinReferenceSolver.h"
#include "natid_qp/InteriorPointSolver.h"
#include "natid_qp/MatrixMarket.h"
#include "natid_qp/QPProblem.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

void require(const bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

double vectorValue(const dense::DblMatrix& vector, const unsigned int row)
{
    return vector.getFirstColumnManipulator()(row);
}

natid_qp::SolverOptions testOptions()
{
    natid_qp::SolverOptions options;
    options.maxIterations = 100;
    options.tolerance = 1e-9;
    options.regularization = 1e-11;
    options.verbose = false;
    options.printKkt = false;
    return options;
}

void testInequalityDemo()
{
    const natid_qp::QPProblem problem = natid_qp::makeInequalityDemoProblem();
    const natid_qp::Solution solution =
        natid_qp::InteriorPointSolver(testOptions()).solve(problem);

    require(solution.converged(), "Inequality demo did not converge: " + solution.message);
    require(
        std::abs(vectorValue(solution.x, 0) - 0.75) < 2e-6,
        "Wrong x1 for inequality demo."
    );
    require(
        std::abs(vectorValue(solution.x, 1) - 0.25) < 2e-6,
        "Wrong x2 for inequality demo."
    );
    require(
        std::abs(solution.objective + 1.0625) < 2e-6,
        "Wrong objective for inequality demo."
    );
    require(
        solution.history.back().primalResidual < 1e-7,
        "Large final primal residual for inequality demo."
    );
    require(
        solution.history.back().dualResidual < 1e-7,
        "Large final dual residual for inequality demo."
    );
}

void testEqualityDemo()
{
    const natid_qp::QPProblem problem = natid_qp::makeEqualityDemoProblem();
    const natid_qp::Solution solution =
        natid_qp::InteriorPointSolver(testOptions()).solve(problem);

    require(solution.converged(), "Equality demo did not converge: " + solution.message);
    require(
        std::abs(vectorValue(solution.x, 0) - 0.25) < 2e-6,
        "Wrong x1 for equality demo."
    );
    require(
        std::abs(vectorValue(solution.x, 1) - 0.75) < 2e-6,
        "Wrong x2 for equality demo."
    );
    require(
        solution.history.back().primalResidual < 1e-7,
        "Large final primal residual for equality demo."
    );
}

void testEqualityOnlyProblem()
{
    natid_qp::QPProblem problem(2, 1, 0);
    auto q = problem.Q.getManipulator();
    q(0, 0) = 1.0;
    q(1, 1) = 1.0;
    auto c = problem.c.getFirstColumnManipulator();
    c(0) = -2.0;
    c(1) = -1.0;
    auto a = problem.A.getManipulator();
    a(0, 0) = 1.0;
    a(0, 1) = 1.0;
    problem.b.getFirstColumnManipulator()(0) = 1.0;

    const natid_qp::Solution solution =
        natid_qp::InteriorPointSolver(testOptions()).solve(problem);
    require(solution.converged(), "Equality-only QP did not converge.");
    require(
        std::abs(vectorValue(solution.x, 0) - 1.0) < 2e-6,
        "Wrong x1 for equality-only QP."
    );
    require(
        std::abs(vectorValue(solution.x, 1)) < 2e-6,
        "Wrong x2 for equality-only QP."
    );
}

void testMatrixMarketExamples()
{
    const std::filesystem::path dataRoot = NATID_QP_TEST_DATA_DIR;

    const natid_qp::QPProblem inequality =
        natid_qp::loadProblemDirectory(dataRoot / "inequality_qp");
    require(inequality.variables() == 2, "Wrong variable count after Matrix Market load.");
    require(inequality.equalities() == 0, "Unexpected equality data.");
    require(inequality.inequalities() == 3, "Wrong inequality count.");

    const natid_qp::Solution inequalitySolution =
        natid_qp::InteriorPointSolver(testOptions()).solve(inequality);
    require(inequalitySolution.converged(), "Loaded inequality example did not converge.");
    require(
        std::abs(vectorValue(inequalitySolution.x, 0) - 0.75) < 2e-6,
        "Loaded inequality example returned a wrong solution."
    );

    const natid_qp::QPProblem equality =
        natid_qp::loadProblemDirectory(dataRoot / "equality_qp");
    require(equality.equalities() == 1, "Equality Matrix Market data was not loaded.");
    const natid_qp::Solution equalitySolution =
        natid_qp::InteriorPointSolver(testOptions()).solve(equality);
    require(equalitySolution.converged(), "Loaded equality example did not converge.");
    require(
        std::abs(vectorValue(equalitySolution.x, 1) - 0.75) < 2e-6,
        "Loaded equality example returned a wrong solution."
    );
}

void testValidation()
{
    natid_qp::QPProblem invalid(2, 0, 1);
    auto q = invalid.Q.getManipulator();
    q(0, 0) = 1.0;
    q(1, 1) = 1.0;
    q(0, 1) = 2.0;
    q(1, 0) = 0.0;

    const natid_qp::Solution solution =
        natid_qp::InteriorPointSolver(testOptions()).solve(invalid);
    require(
        solution.status == natid_qp::SolverStatus::InvalidProblem,
        "Nonsymmetric Q was not rejected."
    );
}

natid_qp::DTwinReferenceResult makeReferenceResult(
    const natid_qp::QPProblem& problem,
    const natid_qp::Solution& solution
)
{
    natid_qp::DTwinReferenceResult result;
    result.status = natid_qp::DTwinStatus::Solved;

    const auto copyVector = [](const dense::DblMatrix& source)
    {
        std::vector<double> output(source.getNoOfRows(), 0.0);
        if (source.getNoOfRows() == 0)
            return output;
        const auto values = source.getFirstColumnManipulator();
        for (unsigned int row = 0; row < source.getNoOfRows(); ++row)
            output[row] = values(row);
        return output;
    };

    result.x = copyVector(solution.x);
    result.equalityDual = copyVector(solution.equalityDual);
    result.slack = copyVector(solution.slack);
    result.inequalityDual = copyVector(solution.inequalityDual);
    natid_qp::evaluateDTwinReference(problem, result);
    return result;
}

void testDTwinModelGeneration()
{
    const natid_qp::QPProblem problem = natid_qp::makeEqualityDemoProblem();
    natid_qp::DTwinReferenceOptions options;
    options.tolerance = 1e-9;
    options.maxIterations = 123;

    const std::string model = natid_qp::buildDTwinKktModel(
        problem,
        nullptr,
        options
    );
    require(
        model.find("NatIDQP_KKT_Reference") != std::string::npos,
        "Generated dTwin model has no expected model name."
    );
    require(
        model.find("maxIter=123") != std::string::npos,
        "Generated dTwin model ignored maxIterations."
    );
    require(
        model.find("x_1") != std::string::npos
            && model.find("y_1") != std::string::npos
            && model.find("s_1") != std::string::npos
            && model.find("z_1") != std::string::npos,
        "Generated dTwin KKT variable groups are incomplete."
    );
    require(
        model.find("sqrt(s_1^2+z_1^2+2*fb_tau)-s_1-z_1=0")
            != std::string::npos,
        "Generated dTwin model has no smooth complementarity equation."
    );
}

void testDTwinComparisonLevels()
{
    const natid_qp::QPProblem problem = natid_qp::makeInequalityDemoProblem();
    const natid_qp::Solution solution =
        natid_qp::InteriorPointSolver(testOptions()).solve(problem);
    require(solution.converged(), "Comparison fixture QP did not converge.");

    natid_qp::DTwinReferenceResult exact =
        makeReferenceResult(problem, solution);
    require(
        natid_qp::compareWithDTwin(solution, exact, 1e-9).level
            == natid_qp::MatchLevel::ExactMatch,
        "Identical solutions were not classified as an exact match."
    );

    natid_qp::DTwinReferenceResult close = exact;
    close.x[0] += 5e-7;
    natid_qp::evaluateDTwinReference(problem, close);
    require(
        natid_qp::compareWithDTwin(solution, close, 1e-9).level
            == natid_qp::MatchLevel::CloseMatch,
        "Small perturbation was not classified as a close match."
    );

    natid_qp::DTwinReferenceResult partial = exact;
    partial.x[0] += 2e-4;
    natid_qp::evaluateDTwinReference(problem, partial);
    require(
        natid_qp::compareWithDTwin(solution, partial, 1e-9).level
            == natid_qp::MatchLevel::PartialMatch,
        "Moderate perturbation was not classified as a partial match."
    );

    natid_qp::DTwinReferenceResult mismatch = exact;
    mismatch.x[0] += 1.0;
    natid_qp::evaluateDTwinReference(problem, mismatch);
    require(
        natid_qp::compareWithDTwin(solution, mismatch, 1e-9).level
            == natid_qp::MatchLevel::Mismatch,
        "Materially different solution was not classified as a mismatch."
    );

    natid_qp::DTwinReferenceResult failed;
    failed.status = natid_qp::DTwinStatus::SolverFailure;
    require(
        natid_qp::compareWithDTwin(solution, failed, 1e-9).level
            == natid_qp::MatchLevel::NotCompared,
        "Failed dTwin solve should not produce a match classification."
    );
}

} // namespace

int main()
{
    try
    {
        testInequalityDemo();
        testEqualityDemo();
        testEqualityOnlyProblem();
        testMatrixMarketExamples();
        testValidation();
        testDTwinModelGeneration();
        testDTwinComparisonLevels();
        std::cout << "All NatIDQP tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "TEST FAILURE: " << error.what() << '\n';
        return 1;
    }
}
