#pragma once

#include "natid_qp/QPProblem.h"

#include <dense/Matrix.h>

#include <filesystem>
#include <optional>

namespace natid_qp
{

struct ProblemFiles
{
    std::filesystem::path Q;
    std::filesystem::path c;
    std::optional<std::filesystem::path> A;
    std::optional<std::filesystem::path> b;
    std::filesystem::path G;
    std::filesystem::path h;
};

dense::DblMatrix readMatrixMarket(const std::filesystem::path& fileName);
dense::DblMatrix readVectorMarket(const std::filesystem::path& fileName);

QPProblem loadProblem(const ProblemFiles& files);
QPProblem loadProblemDirectory(const std::filesystem::path& directory);

void writeMatrixMarket(
    const dense::DblMatrix& matrix,
    const std::filesystem::path& fileName
);

} // namespace natid_qp
