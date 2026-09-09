#include "natid_qp/InteriorPointSolver.h"
#include "natid_qp/MatrixMarket.h"
#include "natid_qp/QPProblem.h"

#include <mu/Application.h>
#include <sparse/ISolver.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{

struct CommandLine
{
    bool help = false;
    std::string demo = "inequality";
    std::optional<std::filesystem::path> problemDirectory;
    std::optional<natid_qp::ProblemFiles> explicitFiles;
    std::optional<std::filesystem::path> convergenceCsv;
    std::optional<std::filesystem::path> reportFile;
    std::optional<std::filesystem::path> solutionDirectory;
    natid_qp::SolverOptions solver;
};

std::string requireValue(int& position, const int argc, const char* argv[])
{
    if (++position >= argc)
        throw std::invalid_argument(std::string("Missing value after ") + argv[position - 1]);
    return argv[position];
}

int parseInteger(const std::string& value, const char* option)
{
    std::size_t consumed = 0;
    const int result = std::stoi(value, &consumed);
    if (consumed != value.size())
        throw std::invalid_argument(std::string("Invalid integer for ") + option + ": " + value);
    return result;
}

double parseDouble(const std::string& value, const char* option)
{
    std::size_t consumed = 0;
    const double result = std::stod(value, &consumed);
    if (consumed != value.size())
        throw std::invalid_argument(std::string("Invalid number for ") + option + ": " + value);
    return result;
}

CommandLine parseCommandLine(const int argc, const char* argv[])
{
    CommandLine result;
    std::optional<std::filesystem::path> q;
    std::optional<std::filesystem::path> c;
    std::optional<std::filesystem::path> a;
    std::optional<std::filesystem::path> b;
    std::optional<std::filesystem::path> g;
    std::optional<std::filesystem::path> h;

    for (int position = 1; position < argc; ++position)
    {
        const std::string option = argv[position];
        if (option == "--help" || option == "-h")
        {
            result.help = true;
        }
        else if (option == "--demo")
        {
            result.problemDirectory.reset();
            if (position + 1 < argc && argv[position + 1][0] != '-')
                result.demo = argv[++position];
        }
        else if (option == "--problem")
        {
            result.problemDirectory = requireValue(position, argc, argv);
        }
        else if (option == "--Q")
        {
            q = requireValue(position, argc, argv);
        }
        else if (option == "--c")
        {
            c = requireValue(position, argc, argv);
        }
        else if (option == "--A")
        {
            a = requireValue(position, argc, argv);
        }
        else if (option == "--b")
        {
            b = requireValue(position, argc, argv);
        }
        else if (option == "--G")
        {
            g = requireValue(position, argc, argv);
        }
        else if (option == "--h")
        {
            h = requireValue(position, argc, argv);
        }
        else if (option == "--tol")
        {
            result.solver.tolerance = parseDouble(
                requireValue(position, argc, argv),
                "--tol"
            );
        }
        else if (option == "--max-iter")
        {
            result.solver.maxIterations = parseInteger(
                requireValue(position, argc, argv),
                "--max-iter"
            );
        }
        else if (option == "--fraction")
        {
            result.solver.fractionToBoundary = parseDouble(
                requireValue(position, argc, argv),
                "--fraction"
            );
        }
        else if (option == "--regularization")
        {
            result.solver.regularization = parseDouble(
                requireValue(position, argc, argv),
                "--regularization"
            );
        }
        else if (option == "--zero-tol")
        {
            result.solver.sparseZeroTolerance = parseDouble(
                requireValue(position, argc, argv),
                "--zero-tol"
            );
        }
        else if (option == "--print-kkt")
        {
            result.solver.printKkt = true;
        }
        else if (option == "--no-print-kkt")
        {
            result.solver.printKkt = false;
        }
        else if (option == "--print-kkt-all")
        {
            result.solver.printKkt = true;
            result.solver.printKktEveryIteration = true;
        }
        else if (option == "--kkt-print-limit")
        {
            result.solver.printKktMaxDimension = parseInteger(
                requireValue(position, argc, argv),
                "--kkt-print-limit"
            );
        }
        else if (option == "--quiet")
        {
            result.solver.verbose = false;
        }
        else if (option == "--csv")
        {
            result.convergenceCsv = requireValue(position, argc, argv);
        }
        else if (option == "--output")
        {
            result.reportFile = requireValue(position, argc, argv);
        }
        else if (option == "--solution-dir")
        {
            result.solutionDirectory = requireValue(position, argc, argv);
        }
        else
        {
            throw std::invalid_argument("Unknown option: " + option);
        }
    }

    const bool anyExplicit = q || c || a || b || g || h;
    if (anyExplicit)
    {
        if (!q || !c || !g || !h)
            throw std::invalid_argument("Explicit input requires --Q, --c, --G, and --h.");
        if (a.has_value() != b.has_value())
            throw std::invalid_argument("--A and --b must be supplied together.");
        if (result.problemDirectory)
            throw std::invalid_argument("Use either --problem or explicit matrix paths, not both.");

        result.explicitFiles = natid_qp::ProblemFiles{
            *q,
            *c,
            a,
            b,
            *g,
            *h
        };
    }

    return result;
}

void printHelp(const char* executable)
{
    std::cout
        << "NatIDQP - Mehrotra primal-dual solver for convex quadratic programs\n\n"
        << "Usage:\n"
        << "  " << executable << " --demo [inequality|equality] [options]\n"
        << "  " << executable << " --problem DIRECTORY [options]\n"
        << "  " << executable
        << " --Q Q.mtx --c c.mtx [--A A.mtx --b b.mtx]"
        << " --G G.mtx --h h.mtx [options]\n\n"
        << "Solver options:\n"
        << "  --tol VALUE              Scaled KKT tolerance (default 1e-8)\n"
        << "  --max-iter N             Maximum IPM iterations (default 100)\n"
        << "  --fraction VALUE         Fraction-to-boundary in (0,1) (default 0.995)\n"
        << "  --regularization VALUE   Primal diagonal regularization (default 1e-10)\n"
        << "  --zero-tol VALUE         Sparse assembly drop tolerance (default 1e-14)\n"
        << "  --print-kkt              Print first small KKT matrix and LDLT factors\n"
        << "  --print-kkt-all          Print every small KKT matrix and factors\n"
        << "  --no-print-kkt           Disable KKT printing\n"
        << "  --kkt-print-limit N      Maximum printed KKT dimension (default 12)\n"
        << "  --quiet                  Suppress per-iteration table\n\n"
        << "Output options:\n"
        << "  --csv FILE               Write convergence history as CSV\n"
        << "  --output FILE            Write final text report\n"
        << "  --solution-dir DIRECTORY Write x, y, s, z as Matrix Market arrays\n"
        << "  --help                    Show this message\n";
}

void writeSolutionMatrices(
    const natid_qp::QPProblem& problem,
    const natid_qp::Solution& solution,
    const std::filesystem::path& directory
)
{
    std::filesystem::create_directories(directory);
    natid_qp::writeMatrixMarket(solution.x, directory / "x.mtx");
    if (problem.equalities() > 0)
    {
        natid_qp::writeMatrixMarket(
            solution.equalityDual,
            directory / "y.mtx"
        );
    }
    if (problem.inequalities() > 0)
    {
        natid_qp::writeMatrixMarket(solution.slack, directory / "s.mtx");
        natid_qp::writeMatrixMarket(
            solution.inequalityDual,
            directory / "z.mtx"
        );
    }
}

class SolverLibraryGuard
{
public:
    ~SolverLibraryGuard()
    {
        sparse::releaseSolverLibraries();
    }
};

} // namespace

int main(const int argc, const char* argv[])
{
    mu::Application application(
        argc,
        argv,
        "ba.natID.NatIDQP",
        nullptr,
        false
    );
    SolverLibraryGuard solverLibraryGuard;

    try
    {
        const CommandLine command = parseCommandLine(argc, argv);
        if (command.help)
        {
            printHelp(argv[0]);
            return EXIT_SUCCESS;
        }

        natid_qp::QPProblem problem;
        if (command.explicitFiles)
        {
            problem = natid_qp::loadProblem(*command.explicitFiles);
        }
        else if (command.problemDirectory)
        {
            problem = natid_qp::loadProblemDirectory(*command.problemDirectory);
        }
        else if (command.demo == "inequality")
        {
            problem = natid_qp::makeInequalityDemoProblem();
        }
        else if (command.demo == "equality")
        {
            problem = natid_qp::makeEqualityDemoProblem();
        }
        else
        {
            throw std::invalid_argument(
                "Unknown demo '" + command.demo + "'. Use inequality or equality."
            );
        }

        std::cout
            << "Problem dimensions: n=" << problem.variables()
            << ", p=" << problem.equalities()
            << ", m=" << problem.inequalities() << '\n';

        const natid_qp::InteriorPointSolver solver(command.solver);
        const natid_qp::Solution solution = solver.solve(problem);
        natid_qp::writeSolutionReport(problem, solution, std::cout);

        if (command.convergenceCsv)
        {
            natid_qp::writeConvergenceCsv(
                solution,
                command.convergenceCsv->string()
            );
            std::cout << "Convergence CSV: " << *command.convergenceCsv << '\n';
        }

        if (command.reportFile)
        {
            if (!command.reportFile->parent_path().empty())
            {
                std::filesystem::create_directories(
                    command.reportFile->parent_path()
                );
            }
            std::ofstream output(*command.reportFile);
            if (!output)
                throw std::runtime_error(
                    "Cannot create report: " + command.reportFile->string()
                );
            natid_qp::writeSolutionReport(problem, solution, output);
            std::cout << "Solution report: " << *command.reportFile << '\n';
        }

        if (command.solutionDirectory)
        {
            writeSolutionMatrices(problem, solution, *command.solutionDirectory);
            std::cout << "Solution matrices: " << *command.solutionDirectory << '\n';
        }

        return solution.converged() ? EXIT_SUCCESS : 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << "\n\n";
        printHelp(argv[0]);
        return EXIT_FAILURE;
    }
}
