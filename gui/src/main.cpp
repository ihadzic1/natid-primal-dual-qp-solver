#include "Application.h"

#include <gui/WinMain.h>
#include <sparse/ISolver.h>

namespace
{

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
    Application application(argc, argv);
    SolverLibraryGuard solverLibraryGuard;
    application.init("EN");
    return application.run();
}
