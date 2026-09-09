#pragma once

// Test-only compatibility layer for environments where natID binary libraries
// are unavailable. It implements only the API subset used by NatIDQP.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mem
{

template <typename T>
class PointerReleaser
{
public:
    explicit PointerReleaser(T* pointer = nullptr)
        : _pointer(pointer)
    {
    }

    PointerReleaser(const PointerReleaser&) = delete;
    PointerReleaser& operator=(const PointerReleaser&) = delete;

    ~PointerReleaser()
    {
        if (_pointer)
            _pointer->release();
    }

    T* operator->()
    {
        return _pointer;
    }

    const T* operator->() const
    {
        return _pointer;
    }

    T* ptr()
    {
        return _pointer;
    }

    const T* ptr() const
    {
        return _pointer;
    }

    T& ref()
    {
        return *_pointer;
    }

private:
    T* _pointer = nullptr;
};

} // namespace mem

namespace sparse
{

enum class Format : unsigned int
{
    Cpp = 1,
    SciLab = 2,
    Matlab = 4,
    LaTeX = 8,
    Mtx = 16,
    Fortran = 32
};

enum class Pivoting : unsigned char
{
    No = 0,
    DiagonalSinglePass,
    DiagonalMultiPass,
    MarkowitzSinglePass,
    MarkowitzMultiPass,
    AlterMatrixIfIndefinite
};

enum class Ordering : unsigned char
{
    Own = 0,
    OwnRadial,
    ExternSym,
    ExternAtPlusA,
    ExternAtMulA,
    ExternAMulAt
};

enum class Symmetry : unsigned char
{
    NonSymmetric = 0,
    SymmetricPosDef,
    SymmetricGeneral,
    SymmetricIndef,
    HermitianPosDef,
    HermitianIndef
};

enum class SolverType : unsigned char
{
    LU = 0,
    LLT,
    LDLT,
    Ctrl,
    Pardiso
};

} // namespace sparse

namespace dense
{

template <typename T>
class MatrixMgr;

template <typename T>
class MatrixReader
{
public:
    MatrixReader(const T* data, const unsigned int rows, const unsigned int columns)
        : _data(data), _rows(rows), _columns(columns)
    {
    }

    const T& operator()(const unsigned int row, const unsigned int column) const
    {
        assert(row < _rows && column < _columns);
        return _data[column * _rows + row];
    }

private:
    const T* _data = nullptr;
    unsigned int _rows = 0;
    unsigned int _columns = 0;
};

template <typename T>
class MatrixIO
{
public:
    MatrixIO(T* data, const unsigned int rows, const unsigned int columns)
        : _data(data), _rows(rows), _columns(columns)
    {
    }

    T& operator()(const unsigned int row, const unsigned int column)
    {
        assert(row < _rows && column < _columns);
        return _data[column * _rows + row];
    }

private:
    T* _data = nullptr;
    unsigned int _rows = 0;
    unsigned int _columns = 0;
};

template <typename T>
class FirstColumnReader
{
public:
    FirstColumnReader(const T* data, const unsigned int rows)
        : _data(data), _rows(rows)
    {
    }

    const T& operator()(const unsigned int row) const
    {
        assert(row < _rows);
        return _data[row];
    }

private:
    const T* _data = nullptr;
    unsigned int _rows = 0;
};

template <typename T>
class FirstColumnIO
{
public:
    FirstColumnIO(T* data, const unsigned int rows)
        : _data(data), _rows(rows)
    {
    }

    T& operator()(const unsigned int row)
    {
        assert(row < _rows);
        return _data[row];
    }

private:
    T* _data = nullptr;
    unsigned int _rows = 0;
};

template <typename T>
class Matrix
{
public:
    Matrix() = default;
    Matrix(const Matrix&) = default;
    Matrix(Matrix&&) noexcept = default;

    Matrix(
        const unsigned int rows,
        const unsigned int columns,
        MatrixMgr<T>* = nullptr,
        const bool = false
    )
        : _rows(rows), _columns(columns), _values(rows * columns, T{})
    {
    }

    void reserve(
        const unsigned int rows,
        const unsigned int columns,
        MatrixMgr<T>* = nullptr,
        const bool = false
    )
    {
        _rows = rows;
        _columns = columns;
        _values.assign(static_cast<std::size_t>(rows) * columns, T{});
    }

    void operator=(const Matrix& other)
    {
        _rows = other._rows;
        _columns = other._columns;
        _values = other._values;
    }

    Matrix& operator=(Matrix&&) noexcept = default;

    [[nodiscard]] unsigned int getNoOfRows() const
    {
        return _rows;
    }

    [[nodiscard]] unsigned int getNoOfCols() const
    {
        return _columns;
    }

    MatrixReader<T> getManipulator() const
    {
        return MatrixReader<T>(_values.data(), _rows, _columns);
    }

    MatrixIO<T> getManipulator()
    {
        return MatrixIO<T>(_values.data(), _rows, _columns);
    }

    FirstColumnReader<T> getFirstColumnManipulator() const
    {
        return FirstColumnReader<T>(_values.data(), _rows);
    }

    FirstColumnIO<T> getFirstColumnManipulator()
    {
        return FirstColumnIO<T>(_values.data(), _rows);
    }

    void zeros()
    {
        std::fill(_values.begin(), _values.end(), T{});
    }

    void gemv(const Matrix<T>& x, Matrix<T>& y) const
    {
        if (_columns != x._rows || x._columns != 1)
            throw std::runtime_error("portable natID stub: gemv dimension mismatch");
        if (y._rows != _rows || y._columns != 1)
            y.reserve(_rows, 1);

        auto output = y.getFirstColumnManipulator();
        const auto input = x.getFirstColumnManipulator();
        const auto matrix = getManipulator();
        for (unsigned int row = 0; row < _rows; ++row)
        {
            T sum{};
            for (unsigned int column = 0; column < _columns; ++column)
                sum += matrix(row, column) * input(column);
            output(row) = sum;
        }
    }

    void show(
        std::ostream& output,
        const char* name,
        const double = 0.0,
        const int = -1,
        const int precision = -1
    ) const
    {
        if (precision >= 0)
            output << std::setprecision(precision);
        output << name << " = [\n";
        const auto matrix = getManipulator();
        for (unsigned int row = 0; row < _rows; ++row)
        {
            for (unsigned int column = 0; column < _columns; ++column)
                output << matrix(row, column) << (column + 1 == _columns ? "" : " ");
            output << (row + 1 == _rows ? "\n" : ";\n");
        }
        output << "];\n";
    }

    void serialize(
        const char* name,
        std::ostream& output,
        const sparse::Format = sparse::Format::Matlab
    ) const
    {
        show(output, name);
    }

private:
    unsigned int _rows = 0;
    unsigned int _columns = 0;
    std::vector<T> _values;
};

template <typename T>
class DiagReader
{
public:
    DiagReader(const T* data, const unsigned int size)
        : _data(data), _size(size)
    {
    }

    const T& operator()(const unsigned int position) const
    {
        assert(position < _size);
        return _data[position];
    }

private:
    const T* _data = nullptr;
    unsigned int _size = 0;
};

template <typename T>
class DiagIO
{
public:
    DiagIO(T* data, const unsigned int size)
        : _data(data), _size(size)
    {
    }

    T& operator()(const unsigned int position)
    {
        assert(position < _size);
        return _data[position];
    }

private:
    T* _data = nullptr;
    unsigned int _size = 0;
};

template <typename T>
class DiagMatrix
{
public:
    explicit DiagMatrix(const int size = 0, MatrixMgr<T>* = nullptr, const bool = false)
        : _values(static_cast<std::size_t>(std::max(size, 0)), T{})
    {
    }

    DiagReader<T> getManipulator() const
    {
        return DiagReader<T>(_values.data(), static_cast<unsigned int>(_values.size()));
    }

    DiagIO<T> getManipulator()
    {
        return DiagIO<T>(_values.data(), static_cast<unsigned int>(_values.size()));
    }

private:
    std::vector<T> _values;
};

using DblMatrix = Matrix<double>;
using DblDiagMatrix = DiagMatrix<double>;

} // namespace dense

namespace sparse
{

class IDblMatrix
{
public:
    IDblMatrix(
        const int rows,
        const int columns,
        const Symmetry symmetry
    )
        : _rows(rows),
          _columns(columns),
          _symmetry(symmetry),
          _values(static_cast<std::size_t>(rows) * columns, 0.0)
    {
    }

    void addTriple(const int row, const int column, const double value)
    {
        _values[static_cast<std::size_t>(row) * _columns + column] += value;
        ++_nonZeros;
    }

    [[nodiscard]] std::size_t getNoOfNonZero() const
    {
        return _nonZeros;
    }

    void serialize(
        const char* name,
        std::ostream& output,
        const Format = Format::Matlab
    ) const
    {
        output << name << " = [\n";
        for (int row = 0; row < _rows; ++row)
        {
            for (int column = 0; column < _columns; ++column)
            {
                double value = _values[static_cast<std::size_t>(row) * _columns + column];
                if (value == 0.0 && row > column && _symmetry != Symmetry::NonSymmetric)
                {
                    value = _values[
                        static_cast<std::size_t>(column) * _columns + row
                    ];
                }
                output << value << (column + 1 == _columns ? "" : " ");
            }
            output << (row + 1 == _rows ? "\n" : ";\n");
        }
        output << "];\n";
    }

    void release()
    {
        delete this;
    }

private:
    int _rows = 0;
    int _columns = 0;
    Symmetry _symmetry = Symmetry::NonSymmetric;
    std::vector<double> _values;
    std::size_t _nonZeros = 0;
};

class DblSolver
{
public:
    DblSolver(const int dimension, const Symmetry symmetry)
        : _dimension(dimension),
          _symmetry(symmetry),
          _matrix(static_cast<std::size_t>(dimension) * dimension, 0.0),
          _rhs(dimension, 0.0),
          _solution(dimension, 0.0)
    {
    }

    void addTriple(const int row, const int column, const double value)
    {
        at(row, column) += value;
        if (row != column && _symmetry != Symmetry::NonSymmetric)
            at(column, row) += value;
        ++_nonZeros;
    }

    void populateDiagonals(const double&)
    {
    }

    void clearRHS()
    {
        std::fill(_rhs.begin(), _rhs.end(), 0.0);
    }

    void setRHS(const int row, const double value)
    {
        _rhs.at(static_cast<std::size_t>(row)) = value;
    }

    [[nodiscard]] const double& x(const int row) const
    {
        return _solution.at(static_cast<std::size_t>(row));
    }

    bool factorize()
    {
        _lastError.clear();
        return true;
    }

    bool solve()
    {
        std::vector<double> a = _matrix;
        std::vector<double> rhs = _rhs;

        for (int pivot = 0; pivot < _dimension; ++pivot)
        {
            int bestRow = pivot;
            double best = std::abs(a[index(pivot, pivot)]);
            for (int row = pivot + 1; row < _dimension; ++row)
            {
                const double candidate = std::abs(a[index(row, pivot)]);
                if (candidate > best)
                {
                    best = candidate;
                    bestRow = row;
                }
            }

            if (best <= 1e-15)
            {
                _lastError = "portable stub detected a singular KKT matrix";
                return false;
            }

            if (bestRow != pivot)
            {
                for (int column = 0; column < _dimension; ++column)
                    std::swap(a[index(pivot, column)], a[index(bestRow, column)]);
                std::swap(rhs[pivot], rhs[bestRow]);
            }

            for (int row = pivot + 1; row < _dimension; ++row)
            {
                const double factor = a[index(row, pivot)] / a[index(pivot, pivot)];
                a[index(row, pivot)] = 0.0;
                for (int column = pivot + 1; column < _dimension; ++column)
                    a[index(row, column)] -= factor * a[index(pivot, column)];
                rhs[row] -= factor * rhs[pivot];
            }
        }

        for (int row = _dimension - 1; row >= 0; --row)
        {
            double value = rhs[row];
            for (int column = row + 1; column < _dimension; ++column)
                value -= a[index(row, column)] * _solution[column];
            _solution[row] = value / a[index(row, row)];
        }
        return true;
    }

    [[nodiscard]] const char* getLastError() const
    {
        return _lastError.c_str();
    }

    void serialize(
        const char* name,
        std::ostream& output,
        const Format = Format::Matlab
    ) const
    {
        output << name << " = [\n";
        for (int row = 0; row < _dimension; ++row)
        {
            for (int column = 0; column < _dimension; ++column)
            {
                output << _matrix[index(row, column)]
                       << (column + 1 == _dimension ? "" : " ");
            }
            output << (row + 1 == _dimension ? "\n" : ";\n");
        }
        output << "];\n";
    }

    [[nodiscard]] int getNoOfNonZero() const
    {
        return _nonZeros;
    }

    void release()
    {
        delete this;
    }

private:
    [[nodiscard]] std::size_t index(const int row, const int column) const
    {
        return static_cast<std::size_t>(row) * _dimension + column;
    }

    double& at(const int row, const int column)
    {
        return _matrix[index(row, column)];
    }

    int _dimension = 0;
    Symmetry _symmetry = Symmetry::NonSymmetric;
    std::vector<double> _matrix;
    std::vector<double> _rhs;
    std::vector<double> _solution;
    std::string _lastError;
    int _nonZeros = 0;
};

using DblMatrixReleaser = mem::PointerReleaser<IDblMatrix>;
using DblSolverReleaser = mem::PointerReleaser<DblSolver>;

inline IDblMatrix* createDblMatrix(
    const int rows,
    const int columns,
    const int,
    const Symmetry symmetry = Symmetry::NonSymmetric
)
{
    return new IDblMatrix(rows, columns, symmetry);
}

inline DblSolver* createDblSolver(
    const int dimension,
    const int,
    const Symmetry symmetry = Symmetry::NonSymmetric,
    const SolverType = SolverType::LU,
    const Pivoting = Pivoting::DiagonalSinglePass,
    const Ordering = Ordering::Own
)
{
    return new DblSolver(dimension, symmetry);
}

inline void releaseSolverLibraries()
{
}

} // namespace sparse

namespace mu
{

class Application
{
public:
    Application(
        const int,
        const char**,
        const char* = "ba.natID",
        const char* = nullptr,
        const bool = true
    )
    {
    }
};

} // namespace mu
