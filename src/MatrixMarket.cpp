#include "natid_qp/MatrixMarket.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

struct MatrixMarketHeader
{
    std::string storage;
    std::string field;
    std::string symmetry;
};

std::string lower(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

MatrixMarketHeader readHeader(
    std::ifstream& input,
    const std::filesystem::path& fileName
)
{
    std::string line;
    if (!std::getline(input, line))
        throw std::runtime_error("Empty Matrix Market file: " + fileName.string());

    std::istringstream parser(line);
    std::string banner;
    std::string object;
    MatrixMarketHeader header;
    parser >> banner >> object >> header.storage >> header.field >> header.symmetry;

    banner = lower(banner);
    object = lower(object);
    header.storage = lower(header.storage);
    header.field = lower(header.field);
    header.symmetry = lower(header.symmetry);

    if (banner != "%%matrixmarket" || object != "matrix")
        throw std::runtime_error("Invalid Matrix Market header in: " + fileName.string());
    if (header.storage != "coordinate" && header.storage != "array")
        throw std::runtime_error("Unsupported Matrix Market storage in: " + fileName.string());
    if (header.field != "real" && header.field != "integer"
        && header.field != "pattern")
    {
        throw std::runtime_error(
            "Only real, integer, and pattern Matrix Market fields are supported: "
            + fileName.string()
        );
    }
    if (header.storage == "array" && header.field == "pattern")
        throw std::runtime_error("Pattern array matrices are not valid: " + fileName.string());
    if (header.symmetry != "general" && header.symmetry != "symmetric"
        && header.symmetry != "skew-symmetric" && header.symmetry != "hermitian")
    {
        throw std::runtime_error(
            "Unsupported Matrix Market symmetry in: " + fileName.string()
        );
    }

    return header;
}

bool nextDataLine(std::ifstream& input, std::string& line)
{
    while (std::getline(input, line))
    {
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '%')
            continue;
        line.erase(0, first);
        return true;
    }
    return false;
}

std::vector<double> readRemainingValues(std::ifstream& input)
{
    std::vector<double> values;
    std::string line;
    while (nextDataLine(input, line))
    {
        std::istringstream parser(line);
        double value = 0.0;
        while (parser >> value)
        {
            if (!std::isfinite(value))
                throw std::runtime_error("Matrix Market data contains a non-finite value.");
            values.push_back(value);
        }
        if (!parser.eof())
            throw std::runtime_error("Invalid numeric value in Matrix Market data.");
    }
    return values;
}

std::filesystem::path requiredFile(
    const std::filesystem::path& directory,
    const char* name
)
{
    const auto fileName = directory / name;
    if (!std::filesystem::is_regular_file(fileName))
        throw std::runtime_error("Required problem file is missing: " + fileName.string());
    return fileName;
}

} // namespace

dense::DblMatrix readMatrixMarket(const std::filesystem::path& fileName)
{
    std::ifstream input(fileName);
    if (!input)
        throw std::runtime_error("Cannot open Matrix Market file: " + fileName.string());

    const MatrixMarketHeader header = readHeader(input, fileName);

    std::string sizeLine;
    if (!nextDataLine(input, sizeLine))
        throw std::runtime_error("Matrix Market size line is missing: " + fileName.string());

    std::istringstream sizeParser(sizeLine);
    std::size_t rows = 0;
    std::size_t columns = 0;
    std::size_t entries = 0;

    if (header.storage == "coordinate")
    {
        if (!(sizeParser >> rows >> columns >> entries))
            throw std::runtime_error("Invalid coordinate size line: " + fileName.string());
    }
    else
    {
        if (!(sizeParser >> rows >> columns))
            throw std::runtime_error("Invalid array size line: " + fileName.string());
    }

    if (rows == 0 || columns == 0)
        throw std::runtime_error("Matrix Market dimensions must be positive: " + fileName.string());
    if ((header.symmetry == "symmetric" || header.symmetry == "skew-symmetric"
         || header.symmetry == "hermitian") && rows != columns)
    {
        throw std::runtime_error("A structured Matrix Market matrix must be square.");
    }

    dense::DblMatrix result(
        static_cast<unsigned int>(rows),
        static_cast<unsigned int>(columns),
        nullptr,
        true
    );
    auto matrix = result.getManipulator();

    if (header.storage == "coordinate")
    {
        std::string entryLine;
        for (std::size_t entry = 0; entry < entries; ++entry)
        {
            if (!nextDataLine(input, entryLine))
                throw std::runtime_error("Matrix Market file ended before all entries were read.");

            std::istringstream parser(entryLine);
            std::size_t oneBasedRow = 0;
            std::size_t oneBasedColumn = 0;
            double value = 1.0;
            if (!(parser >> oneBasedRow >> oneBasedColumn))
                throw std::runtime_error("Invalid coordinate entry in: " + fileName.string());
            if (header.field != "pattern" && !(parser >> value))
                throw std::runtime_error("Coordinate value is missing in: " + fileName.string());
            if (oneBasedRow == 0 || oneBasedRow > rows
                || oneBasedColumn == 0 || oneBasedColumn > columns)
            {
                throw std::runtime_error(
                    "Matrix Market coordinate is outside the declared dimensions."
                );
            }
            if (!std::isfinite(value))
                throw std::runtime_error("Matrix Market data contains a non-finite value.");

            const auto row = static_cast<unsigned int>(oneBasedRow - 1);
            const auto column = static_cast<unsigned int>(oneBasedColumn - 1);
            matrix(row, column) += value;

            if (row != column && header.symmetry != "general")
            {
                const double mirrored = header.symmetry == "skew-symmetric" ? -value : value;
                matrix(column, row) += mirrored;
            }
            else if (row == column && header.symmetry == "skew-symmetric"
                     && std::abs(value) > 0.0)
            {
                throw std::runtime_error(
                    "A skew-symmetric Matrix Market matrix must have zero diagonal."
                );
            }
        }
    }
    else
    {
        const std::vector<double> values = readRemainingValues(input);
        std::size_t position = 0;

        if (header.symmetry == "general")
        {
            const std::size_t expected = rows * columns;
            if (values.size() != expected)
                throw std::runtime_error("Wrong number of values in Matrix Market array.");

            for (unsigned int column = 0; column < columns; ++column)
            {
                for (unsigned int row = 0; row < rows; ++row)
                    matrix(row, column) = values[position++];
            }
        }
        else if (header.symmetry == "symmetric" || header.symmetry == "hermitian")
        {
            const std::size_t expected = rows * (rows + 1) / 2;
            if (values.size() != expected)
                throw std::runtime_error("Wrong number of values in structured Matrix Market array.");

            for (unsigned int column = 0; column < columns; ++column)
            {
                for (unsigned int row = column; row < rows; ++row)
                {
                    const double value = values[position++];
                    matrix(row, column) = value;
                    matrix(column, row) = value;
                }
            }
        }
        else
        {
            const std::size_t expected = rows * (rows - 1) / 2;
            if (values.size() != expected)
                throw std::runtime_error("Wrong number of values in skew Matrix Market array.");

            for (unsigned int column = 0; column < columns; ++column)
            {
                for (unsigned int row = column + 1; row < rows; ++row)
                {
                    const double value = values[position++];
                    matrix(row, column) = value;
                    matrix(column, row) = -value;
                }
            }
        }
    }

    return result;
}

dense::DblMatrix readVectorMarket(const std::filesystem::path& fileName)
{
    dense::DblMatrix input = readMatrixMarket(fileName);
    const auto rows = input.getNoOfRows();
    const auto columns = input.getNoOfCols();

    if (columns == 1)
        return input;
    if (rows != 1)
        throw std::runtime_error(
            "Expected a row or column vector in Matrix Market file: " + fileName.string()
        );

    dense::DblMatrix output(columns, 1, nullptr, true);
    const dense::DblMatrix& constantInput = input;
    const auto source = constantInput.getManipulator();
    auto destination = output.getFirstColumnManipulator();
    for (unsigned int column = 0; column < columns; ++column)
        destination(column) = source(0, column);
    return output;
}

QPProblem loadProblem(const ProblemFiles& files)
{
    if (files.A.has_value() != files.b.has_value())
        throw std::runtime_error("A.mtx and b.mtx must be supplied together.");

    QPProblem problem;
    problem.Q = readMatrixMarket(files.Q);
    problem.c = readVectorMarket(files.c);
    problem.G = readMatrixMarket(files.G);
    problem.h = readVectorMarket(files.h);

    if (files.A)
    {
        problem.A = readMatrixMarket(*files.A);
        problem.b = readVectorMarket(*files.b);
    }

    const std::string validationError = problem.validate();
    if (!validationError.empty())
        throw std::runtime_error("Invalid QP data: " + validationError);
    return problem;
}

QPProblem loadProblemDirectory(const std::filesystem::path& directory)
{
    if (!std::filesystem::is_directory(directory))
        throw std::runtime_error("Problem directory does not exist: " + directory.string());

    ProblemFiles files{
        requiredFile(directory, "Q.mtx"),
        requiredFile(directory, "c.mtx"),
        std::nullopt,
        std::nullopt,
        requiredFile(directory, "G.mtx"),
        requiredFile(directory, "h.mtx")
    };

    const auto a = directory / "A.mtx";
    const auto b = directory / "b.mtx";
    const bool hasA = std::filesystem::is_regular_file(a);
    const bool hasB = std::filesystem::is_regular_file(b);
    if (hasA != hasB)
        throw std::runtime_error("Problem directory must contain both A.mtx and b.mtx.");
    if (hasA)
    {
        files.A = a;
        files.b = b;
    }

    return loadProblem(files);
}

void writeMatrixMarket(
    const dense::DblMatrix& matrix,
    const std::filesystem::path& fileName
)
{
    if (!fileName.parent_path().empty())
        std::filesystem::create_directories(fileName.parent_path());

    std::ofstream output(fileName);
    if (!output)
        throw std::runtime_error("Cannot create Matrix Market file: " + fileName.string());

    output << "%%MatrixMarket matrix array real general\n";
    output << "% Generated by NatIDQP\n";
    output << matrix.getNoOfRows() << ' ' << matrix.getNoOfCols() << '\n';
    output << std::setprecision(17) << std::scientific;

    if (matrix.getNoOfRows() == 0 || matrix.getNoOfCols() == 0)
        return;

    const auto values = matrix.getManipulator();
    for (unsigned int column = 0; column < matrix.getNoOfCols(); ++column)
    {
        for (unsigned int row = 0; row < matrix.getNoOfRows(); ++row)
            output << values(row, column) << '\n';
    }
}

} // namespace natid_qp
