#pragma once

#include "natid_qp/InteriorPointSolver.h"
#include "natid_qp/QPProblem.h"

#include <gui/Canvas.h>
#include <gui/DrawableString.h>
#include <gui/Shape.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

class ObjectiveCanvas final : public gui::Canvas
{
private:
    struct Constraint2D
    {
        double a = 0.0;
        double b = 0.0;
        double rhs = 0.0;
    };

    std::vector<natid_qp::IterationStats> _history;
    std::vector<Constraint2D> _equalities;
    std::vector<Constraint2D> _inequalities;
    std::array<double, 4> _q{};
    std::array<double, 2> _c{};
    std::array<double, 2> _optimum{};
    td::String _problemName;
    td::String _summary;
    td::String _details;
    std::size_t _visiblePointCount = 0;
    std::size_t _variables = 0;
    double _xMinimum = -1.0;
    double _xMaximum = 1.0;
    double _yMinimum = -1.0;
    double _yMaximum = 1.0;
    bool _hasData = false;
    bool _converged = false;

    static void drawText(
        const td::String& text,
        const gui::Rect& rect,
        const gui::Font::ID font,
        const td::ColorID color,
        const td::TextAlignment horizontal = td::TextAlignment::Left,
        const td::VAlignment vertical = td::VAlignment::Center
    )
    {
        gui::DrawableString::draw(
            text,
            rect,
            font,
            color,
            horizontal,
            vertical
        );
    }

    [[nodiscard]] double objectiveValue(const double x, const double y) const
    {
        return 0.5 * (
            _q[0] * x * x
            + (_q[1] + _q[2]) * x * y
            + _q[3] * y * y
        ) + _c[0] * x + _c[1] * y;
    }

    [[nodiscard]] gui::CoordType mapDataX(
        const double value,
        const gui::Rect& plot
    ) const
    {
        return plot.left
            + (value - _xMinimum) / (_xMaximum - _xMinimum) * plot.width();
    }

    [[nodiscard]] gui::CoordType mapDataY(
        const double value,
        const gui::Rect& plot
    ) const
    {
        return plot.bottom
            - (value - _yMinimum) / (_yMaximum - _yMinimum) * plot.height();
    }

    [[nodiscard]] bool satisfiesInequalities(
        const double x,
        const double y
    ) const
    {
        for (const Constraint2D& constraint : _inequalities)
        {
            const double lhs = constraint.a * x + constraint.b * y;
            const double allowance = 1e-9 * (
                1.0 + std::abs(lhs) + std::abs(constraint.rhs)
            );
            if (lhs > constraint.rhs + allowance)
                return false;
        }
        return true;
    }

    void updateDomain()
    {
        bool hasPoint = false;
        double minimumX = 0.0;
        double maximumX = 0.0;
        double minimumY = 0.0;
        double maximumY = 0.0;

        const auto inspect = [&](const double x, const double y)
        {
            if (!std::isfinite(x) || !std::isfinite(y))
                return;
            if (!hasPoint)
            {
                minimumX = maximumX = x;
                minimumY = maximumY = y;
                hasPoint = true;
                return;
            }
            minimumX = std::min(minimumX, x);
            maximumX = std::max(maximumX, x);
            minimumY = std::min(minimumY, y);
            maximumY = std::max(maximumY, y);
        };

        for (const natid_qp::IterationStats& stats : _history)
        {
            if (stats.x.size() >= 2)
                inspect(stats.x[0], stats.x[1]);
        }
        if (_converged)
            inspect(_optimum[0], _optimum[1]);

        if (!hasPoint)
        {
            _xMinimum = -1.0;
            _xMaximum = 1.0;
            _yMinimum = -1.0;
            _yMaximum = 1.0;
            return;
        }

        const double centerX = 0.5 * (minimumX + maximumX);
        const double centerY = 0.5 * (minimumY + maximumY);
        const double coordinateScale = 1.0 + std::max(
            std::abs(centerX),
            std::abs(centerY)
        );
        double span = std::max(maximumX - minimumX, maximumY - minimumY);
        span = std::max(span, 0.75 * coordinateScale);
        const double halfSpan = 0.7 * span;

        _xMinimum = centerX - halfSpan;
        _xMaximum = centerX + halfSpan;
        _yMinimum = centerY - halfSpan;
        _yMaximum = centerY + halfSpan;
    }

    void updatePlaybackText()
    {
        if (_history.empty() || _visiblePointCount == 0)
            return;

        const std::size_t index =
            std::min(_visiblePointCount, _history.size()) - 1;
        const natid_qp::IterationStats& stats = _history[index];
        const natid_qp::IterationStats& finalStats = _history.back();
        _summary.format(
            "%s | iteration %d/%d | objective: %.12g",
            _problemName.c_str(),
            stats.iteration,
            finalStats.iteration,
            stats.objective
        );

        if (_variables == 2 && stats.x.size() >= 2)
        {
            _details.format(
                "Contour plot of f(x) = 0.5*x^T*Q*x + c^T*x | "
                "x^(%d) = (%.8g, %.8g)",
                stats.iteration,
                stats.x[0],
                stats.x[1]
            );
        }
        else
        {
            _details.format(
                "Objective value vs. iteration | variables: %llu",
                static_cast<unsigned long long>(_variables)
            );
        }
    }

    void drawFeasibleRegion(const gui::Rect& plot) const
    {
        if (_inequalities.empty())
            return;

        constexpr int columns = 44;
        constexpr int rows = 32;
        const double dataWidth = (_xMaximum - _xMinimum) / columns;
        const double dataHeight = (_yMaximum - _yMinimum) / rows;
        for (int row = 0; row < rows; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                const double x0 = _xMinimum + column * dataWidth;
                const double y0 = _yMinimum + row * dataHeight;
                const double x1 = x0 + dataWidth;
                const double y1 = y0 + dataHeight;
                if (!satisfiesInequalities(0.5 * (x0 + x1), 0.5 * (y0 + y1)))
                    continue;

                gui::Shape::drawRect(
                    gui::Rect(
                        mapDataX(x0, plot),
                        mapDataY(y1, plot),
                        mapDataX(x1, plot),
                        mapDataY(y0, plot)
                    ),
                    0.16f,
                    td::ColorID::LightGreen
                );
            }
        }
    }

    [[nodiscard]] std::vector<std::array<double, 2>> boundaryIntersections(
        const Constraint2D& constraint
    ) const
    {
        constexpr double epsilon = 1e-12;
        std::vector<std::array<double, 2>> points;
        const auto addIfInside = [&](const double x, const double y)
        {
            const double xTolerance = 1e-9 * (1.0 + std::abs(x));
            const double yTolerance = 1e-9 * (1.0 + std::abs(y));
            if (!std::isfinite(x) || !std::isfinite(y)
                || x < _xMinimum - xTolerance
                || x > _xMaximum + xTolerance
                || y < _yMinimum - yTolerance
                || y > _yMaximum + yTolerance)
            {
                return;
            }
            for (const auto& point : points)
            {
                if (std::hypot(point[0] - x, point[1] - y) < 1e-9)
                    return;
            }
            points.push_back({x, y});
        };

        if (std::abs(constraint.b) > epsilon)
        {
            addIfInside(
                _xMinimum,
                (constraint.rhs - constraint.a * _xMinimum) / constraint.b
            );
            addIfInside(
                _xMaximum,
                (constraint.rhs - constraint.a * _xMaximum) / constraint.b
            );
        }
        if (std::abs(constraint.a) > epsilon)
        {
            addIfInside(
                (constraint.rhs - constraint.b * _yMinimum) / constraint.a,
                _yMinimum
            );
            addIfInside(
                (constraint.rhs - constraint.b * _yMaximum) / constraint.a,
                _yMaximum
            );
        }
        return points;
    }

    void drawConstraintBoundaries(const gui::Rect& plot) const
    {
        const auto drawBoundary = [this, &plot](
            const Constraint2D& constraint,
            const td::ColorID color,
            const td::LinePattern pattern
        )
        {
            const auto points = boundaryIntersections(constraint);
            if (points.size() < 2)
                return;

            std::size_t first = 0;
            std::size_t second = 1;
            double maximumDistance = -1.0;
            for (std::size_t left = 0; left < points.size(); ++left)
            {
                for (std::size_t right = left + 1; right < points.size(); ++right)
                {
                    const double distance = std::hypot(
                        points[left][0] - points[right][0],
                        points[left][1] - points[right][1]
                    );
                    if (distance > maximumDistance)
                    {
                        maximumDistance = distance;
                        first = left;
                        second = right;
                    }
                }
            }

            gui::Shape::drawLine(
                gui::Point(
                    mapDataX(points[first][0], plot),
                    mapDataY(points[first][1], plot)
                ),
                gui::Point(
                    mapDataX(points[second][0], plot),
                    mapDataY(points[second][1], plot)
                ),
                color,
                1.5f,
                pattern,
                0.85f
            );
        };

        for (const Constraint2D& inequality : _inequalities)
            drawBoundary(inequality, td::ColorID::SeaGreen, td::LinePattern::Solid);
        for (const Constraint2D& equality : _equalities)
            drawBoundary(equality, td::ColorID::DarkOrange, td::LinePattern::Dash);
    }

    void drawContours(const gui::Rect& plot) const
    {
        constexpr int columns = 42;
        constexpr int rows = 30;
        constexpr int levelCount = 10;
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();

        for (int row = 0; row <= rows; ++row)
        {
            const double y = _yMinimum
                + (_yMaximum - _yMinimum) * row / rows;
            for (int column = 0; column <= columns; ++column)
            {
                const double x = _xMinimum
                    + (_xMaximum - _xMinimum) * column / columns;
                const double value = objectiveValue(x, y);
                if (std::isfinite(value))
                {
                    minimum = std::min(minimum, value);
                    maximum = std::max(maximum, value);
                }
            }
        }

        const double range = maximum - minimum;
        if (!std::isfinite(range)
            || range <= std::numeric_limits<double>::epsilon())
        {
            return;
        }

        const auto intersection = [](const double x0,
                                     const double y0,
                                     const double v0,
                                     const double x1,
                                     const double y1,
                                     const double v1,
                                     const double level)
        {
            const double denominator = v1 - v0;
            const double ratio = std::abs(denominator) > 1e-15
                ? std::clamp((level - v0) / denominator, 0.0, 1.0)
                : 0.5;
            return std::array<double, 2>{
                x0 + ratio * (x1 - x0),
                y0 + ratio * (y1 - y0)
            };
        };

        for (int levelIndex = 1; levelIndex <= levelCount; ++levelIndex)
        {
            const double ratio =
                static_cast<double>(levelIndex) / (levelCount + 1);
            const double level = minimum + ratio * range;
            for (int row = 0; row < rows; ++row)
            {
                const double y0 = _yMinimum
                    + (_yMaximum - _yMinimum) * row / rows;
                const double y1 = _yMinimum
                    + (_yMaximum - _yMinimum) * (row + 1) / rows;
                for (int column = 0; column < columns; ++column)
                {
                    const double x0 = _xMinimum
                        + (_xMaximum - _xMinimum) * column / columns;
                    const double x1 = _xMinimum
                        + (_xMaximum - _xMinimum) * (column + 1) / columns;
                    const double v00 = objectiveValue(x0, y0);
                    const double v10 = objectiveValue(x1, y0);
                    const double v11 = objectiveValue(x1, y1);
                    const double v01 = objectiveValue(x0, y1);
                    std::vector<std::array<double, 2>> crossings;
                    crossings.reserve(4);

                    const auto crosses = [level](const double a, const double b)
                    {
                        return (a <= level && b > level)
                            || (b <= level && a > level);
                    };
                    if (crosses(v00, v10))
                        crossings.push_back(intersection(x0, y0, v00, x1, y0, v10, level));
                    if (crosses(v10, v11))
                        crossings.push_back(intersection(x1, y0, v10, x1, y1, v11, level));
                    if (crosses(v11, v01))
                        crossings.push_back(intersection(x1, y1, v11, x0, y1, v01, level));
                    if (crosses(v01, v00))
                        crossings.push_back(intersection(x0, y1, v01, x0, y0, v00, level));

                    for (std::size_t point = 1; point < crossings.size(); point += 2)
                    {
                        gui::Shape::drawLine(
                            gui::Point(
                                mapDataX(crossings[point - 1][0], plot),
                                mapDataY(crossings[point - 1][1], plot)
                            ),
                            gui::Point(
                                mapDataX(crossings[point][0], plot),
                                mapDataY(crossings[point][1], plot)
                            ),
                            td::ColorID::SlateGray,
                            1.0f,
                            td::LinePattern::Solid,
                            0.55f
                        );
                    }
                }
            }
        }
    }

    void drawAxesAndTicks(const gui::Rect& bounds, const gui::Rect& plot) const
    {
        constexpr int divisions = 5;
        for (int division = 0; division <= divisions; ++division)
        {
            const double ratio = static_cast<double>(division) / divisions;
            const gui::CoordType x = plot.left + ratio * plot.width();
            const gui::CoordType y = plot.bottom - ratio * plot.height();
            gui::Shape::drawLine(
                gui::Point(x, plot.top),
                gui::Point(x, plot.bottom),
                td::ColorID::Gray,
                1.0f,
                td::LinePattern::Solid,
                0.22f
            );
            gui::Shape::drawLine(
                gui::Point(plot.left, y),
                gui::Point(plot.right, y),
                td::ColorID::Gray,
                1.0f,
                td::LinePattern::Solid,
                0.22f
            );

            td::String xLabel;
            xLabel.format(
                "%.3g",
                _xMinimum + ratio * (_xMaximum - _xMinimum)
            );
            drawText(
                xLabel,
                gui::Rect(x - 32.0, plot.bottom + 4.0, x + 32.0, bounds.bottom - 34.0),
                gui::Font::ID::SystemSmallest,
                td::ColorID::SysText,
                td::TextAlignment::Center
            );

            td::String yLabel;
            yLabel.format(
                "%.3g",
                _yMinimum + ratio * (_yMaximum - _yMinimum)
            );
            drawText(
                yLabel,
                gui::Rect(bounds.left + 4.0, y - 11.0, plot.left - 7.0, y + 11.0),
                gui::Font::ID::SystemSmallest,
                td::ColorID::SysText,
                td::TextAlignment::Right
            );
        }

        gui::Shape::drawRect(plot, td::ColorID::SysText, 1.0f);
        drawText(
            td::String("x1"),
            gui::Rect(plot.left, bounds.bottom - 33.0, plot.right, bounds.bottom - 10.0),
            gui::Font::ID::SystemSmaller,
            td::ColorID::SysText,
            td::TextAlignment::Center
        );
        drawText(
            td::String("x2"),
            gui::Rect(bounds.left + 6.0, plot.top - 27.0, plot.left + 65.0, plot.top - 3.0),
            gui::Font::ID::SystemSmaller,
            td::ColorID::SysText
        );
    }

    void drawPath(const gui::Rect& plot) const
    {
        const std::size_t count = std::min(_visiblePointCount, _history.size());
        if (count == 0)
            return;

        for (std::size_t index = 1; index < count; ++index)
        {
            if (_history[index - 1].x.size() < 2 || _history[index].x.size() < 2)
                continue;
            gui::Shape::drawLine(
                gui::Point(
                    mapDataX(_history[index - 1].x[0], plot),
                    mapDataY(_history[index - 1].x[1], plot)
                ),
                gui::Point(
                    mapDataX(_history[index].x[0], plot),
                    mapDataY(_history[index].x[1], plot)
                ),
                td::ColorID::DodgerBlue,
                3.0f
            );
        }

        for (std::size_t index = 0; index < count; ++index)
        {
            if (_history[index].x.size() < 2)
                continue;
            const gui::Point point(
                mapDataX(_history[index].x[0], plot),
                mapDataY(_history[index].x[1], plot)
            );
            const double radius = index + 1 == count ? 5.5 : 2.8;
            const td::ColorID fill = index + 1 == count
                ? td::ColorID::Crimson
                : td::ColorID::DodgerBlue;
            gui::Shape::drawRect(
                gui::Rect(
                    point.x - radius,
                    point.y - radius,
                    point.x + radius,
                    point.y + radius
                ),
                fill,
                td::ColorID::SysText,
                1.0f
            );
        }
    }

    void drawOptimum(const gui::Rect& plot) const
    {
        if (!_converged)
            return;

        const gui::Point point(
            mapDataX(_optimum[0], plot),
            mapDataY(_optimum[1], plot)
        );
        gui::Shape::drawLine(
            gui::Point(point.x - 8.0, point.y),
            gui::Point(point.x + 8.0, point.y),
            td::ColorID::Green,
            2.5f
        );
        gui::Shape::drawLine(
            gui::Point(point.x, point.y - 8.0),
            gui::Point(point.x, point.y + 8.0),
            td::ColorID::Green,
            2.5f
        );
    }

    void drawTwoDimensionalChart(const gui::Rect& bounds) const
    {
        constexpr gui::CoordType leftMargin = 78.0;
        constexpr gui::CoordType rightMargin = 32.0;
        constexpr gui::CoordType topMargin = 126.0;
        constexpr gui::CoordType bottomMargin = 65.0;
        const gui::Rect plot(
            bounds.left + leftMargin,
            bounds.top + topMargin,
            bounds.right - rightMargin,
            bounds.bottom - bottomMargin
        );

        if (plot.width() < 180.0 || plot.height() < 140.0)
        {
            drawText(
                td::String("Increase the window size to display the objective plot."),
                bounds,
                gui::Font::ID::SystemNormal,
                td::ColorID::SysText,
                td::TextAlignment::Center,
                td::VAlignment::Center
            );
            return;
        }

        gui::Shape::drawRect(plot, td::ColorID::SysBackAlt1);
        drawFeasibleRegion(plot);
        drawAxesAndTicks(bounds, plot);
        drawContours(plot);
        drawConstraintBoundaries(plot);
        drawOptimum(plot);
        drawPath(plot);

        const gui::CoordType legendY = plot.top - 17.0;
        gui::Shape::drawLine(
            gui::Point(plot.left + 95.0, legendY),
            gui::Point(plot.left + 123.0, legendY),
            td::ColorID::DodgerBlue,
            3.0f
        );
        drawText(
            td::String("iterate path"),
            gui::Rect(plot.left + 130.0, legendY - 10.0, plot.left + 225.0, legendY + 10.0),
            gui::Font::ID::SystemSmallest,
            td::ColorID::SysText
        );
        gui::Shape::drawLine(
            gui::Point(plot.left + 245.0, legendY),
            gui::Point(plot.left + 273.0, legendY),
            td::ColorID::Green,
            2.5f
        );
        drawText(
            td::String("optimum"),
            gui::Rect(plot.left + 280.0, legendY - 10.0, plot.left + 350.0, legendY + 10.0),
            gui::Font::ID::SystemSmallest,
            td::ColorID::SysText
        );
        if (!_inequalities.empty())
        {
            gui::Shape::drawRect(
                gui::Rect(plot.left + 370.0, legendY - 6.0, plot.left + 392.0, legendY + 6.0),
                0.25f,
                td::ColorID::LightGreen
            );
            drawText(
                td::String("feasible"),
                gui::Rect(plot.left + 398.0, legendY - 10.0, plot.left + 468.0, legendY + 10.0),
                gui::Font::ID::SystemSmallest,
                td::ColorID::SysText
            );
        }
    }

    void drawObjectiveHistory(const gui::Rect& bounds) const
    {
        constexpr gui::CoordType leftMargin = 78.0;
        constexpr gui::CoordType rightMargin = 32.0;
        constexpr gui::CoordType topMargin = 126.0;
        constexpr gui::CoordType bottomMargin = 65.0;
        const gui::Rect plot(
            bounds.left + leftMargin,
            bounds.top + topMargin,
            bounds.right - rightMargin,
            bounds.bottom - bottomMargin
        );
        if (plot.width() < 180.0 || plot.height() < 140.0)
        {
            drawText(
                td::String("Increase the window size to display the objective history."),
                bounds,
                gui::Font::ID::SystemNormal,
                td::ColorID::SysText,
                td::TextAlignment::Center,
                td::VAlignment::Center
            );
            return;
        }

        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        for (const natid_qp::IterationStats& stats : _history)
        {
            if (std::isfinite(stats.objective))
            {
                minimum = std::min(minimum, stats.objective);
                maximum = std::max(maximum, stats.objective);
            }
        }
        if (!std::isfinite(minimum) || !std::isfinite(maximum))
        {
            minimum = -1.0;
            maximum = 1.0;
        }
        double range = maximum - minimum;
        if (range <= std::numeric_limits<double>::epsilon())
            range = std::max(1.0, std::abs(maximum));
        minimum -= 0.08 * range;
        maximum += 0.08 * range;

        const auto mapX = [&plot, this](const std::size_t index)
        {
            if (_history.size() <= 1)
                return plot.left + 0.5 * plot.width();
            return plot.left
                + static_cast<double>(index) / (_history.size() - 1) * plot.width();
        };
        const auto mapY = [&plot, minimum, maximum](const double value)
        {
            return plot.bottom
                - (value - minimum) / (maximum - minimum) * plot.height();
        };

        gui::Shape::drawRect(plot, td::ColorID::SysBackAlt1);
        constexpr int divisions = 6;
        for (int division = 0; division <= divisions; ++division)
        {
            const double ratio = static_cast<double>(division) / divisions;
            const gui::CoordType y = plot.top + ratio * plot.height();
            gui::Shape::drawLine(
                gui::Point(plot.left, y),
                gui::Point(plot.right, y),
                td::ColorID::Gray,
                1.0f,
                td::LinePattern::Solid,
                0.3f
            );
            td::String label;
            label.format("%.4g", maximum - ratio * (maximum - minimum));
            drawText(
                label,
                gui::Rect(bounds.left + 4.0, y - 11.0, plot.left - 7.0, y + 11.0),
                gui::Font::ID::SystemSmallest,
                td::ColorID::SysText,
                td::TextAlignment::Right
            );
        }
        gui::Shape::drawRect(plot, td::ColorID::SysText, 1.0f);

        const std::size_t count = std::min(_visiblePointCount, _history.size());
        for (std::size_t index = 1; index < count; ++index)
        {
            gui::Shape::drawLine(
                gui::Point(mapX(index - 1), mapY(_history[index - 1].objective)),
                gui::Point(mapX(index), mapY(_history[index].objective)),
                td::ColorID::DodgerBlue,
                2.5f
            );
        }
        if (count > 0)
        {
            const gui::Point current(
                mapX(count - 1),
                mapY(_history[count - 1].objective)
            );
            gui::Shape::drawRect(
                gui::Rect(
                    current.x - 5.0,
                    current.y - 5.0,
                    current.x + 5.0,
                    current.y + 5.0
                ),
                td::ColorID::Crimson,
                td::ColorID::SysText,
                1.0f
            );
        }

        const std::size_t tickCount = std::min<std::size_t>(6, _history.size());
        for (std::size_t tick = 0; tick < tickCount; ++tick)
        {
            const std::size_t index = tickCount <= 1
                ? 0
                : tick * (_history.size() - 1) / (tickCount - 1);
            td::String label;
            label.format("%d", _history[index].iteration);
            const gui::CoordType x = mapX(index);
            drawText(
                label,
                gui::Rect(x - 24.0, plot.bottom + 4.0, x + 24.0, bounds.bottom - 34.0),
                gui::Font::ID::SystemSmallest,
                td::ColorID::SysText,
                td::TextAlignment::Center
            );
        }

        drawText(
            td::String("iteration"),
            gui::Rect(plot.left, bounds.bottom - 33.0, plot.right, bounds.bottom - 10.0),
            gui::Font::ID::SystemSmaller,
            td::ColorID::SysText,
            td::TextAlignment::Center
        );
        drawText(
            td::String("objective"),
            gui::Rect(bounds.left + 6.0, plot.top - 27.0, plot.left + 90.0, plot.top - 3.0),
            gui::Font::ID::SystemSmaller,
            td::ColorID::SysText
        );
    }

protected:
    void onDraw(const gui::Rect&) override
    {
        gui::Size size;
        getSize(size);
        const gui::Rect bounds(0.0, 0.0, size.width, size.height);
        gui::Shape::drawRect(bounds, td::ColorID::SysCtrlBack);
        gui::Shape::drawRect(
            gui::Rect(bounds.left, bounds.top, bounds.right, bounds.top + 96.0),
            td::ColorID::SysBackAlt2
        );

        drawText(
            td::String("NatIDQP objective function"),
            gui::Rect(22.0, 10.0, bounds.right - 22.0, 40.0),
            gui::Font::ID::SystemLargerBold,
            td::ColorID::SysText
        );
        const bool playbackComplete =
            _hasData && _visiblePointCount >= _history.size();
        const td::ColorID statusColor = playbackComplete
            ? (_converged ? td::ColorID::Green : td::ColorID::Crimson)
            : td::ColorID::DarkOrange;
        gui::Shape::drawRect(gui::Rect(23.0, 51.0, 31.0, 59.0), statusColor);
        drawText(
            _summary,
            gui::Rect(39.0, 42.0, bounds.right - 20.0, 69.0),
            gui::Font::ID::SystemNormal,
            td::ColorID::SysText
        );
        drawText(
            _details,
            gui::Rect(22.0, 68.0, bounds.right - 20.0, 94.0),
            gui::Font::ID::SystemSmaller,
            td::ColorID::SysText
        );

        if (!_hasData)
        {
            drawText(
                td::String("No solver history is available."),
                gui::Rect(20.0, 105.0, bounds.right - 20.0, bounds.bottom - 20.0),
                gui::Font::ID::SystemNormal,
                td::ColorID::SysText,
                td::TextAlignment::Center,
                td::VAlignment::Center
            );
        }
        else if (_variables == 2)
        {
            drawTwoDimensionalChart(bounds);
        }
        else
        {
            drawObjectiveHistory(bounds);
        }
    }

public:
    ObjectiveCanvas()
    {
        enableResizeEvent(true);
        _summary = "Solver has not been run.";
        _details = "Choose a demo problem or a QP folder.";
    }

    void setSolution(
        const natid_qp::QPProblem& problem,
        const natid_qp::Solution& solution,
        const char* problemName
    )
    {
        _history = solution.history;
        _variables = problem.variables();
        _problemName = problemName;
        _converged = solution.converged();
        _hasData = !_history.empty();
        _visiblePointCount = _hasData ? 1 : 0;
        _equalities.clear();
        _inequalities.clear();

        if (_variables == 2)
        {
            const auto q = problem.Q.getManipulator();
            const auto c = problem.c.getFirstColumnManipulator();
            _q = {q(0, 0), q(0, 1), q(1, 0), q(1, 1)};
            _c = {c(0), c(1)};

            if (problem.equalities() > 0)
            {
                const auto a = problem.A.getManipulator();
                const auto b = problem.b.getFirstColumnManipulator();
                _equalities.reserve(problem.equalities());
                for (unsigned int row = 0; row < problem.equalities(); ++row)
                    _equalities.push_back({a(row, 0), a(row, 1), b(row)});
            }
            if (problem.inequalities() > 0)
            {
                const auto g = problem.G.getManipulator();
                const auto h = problem.h.getFirstColumnManipulator();
                _inequalities.reserve(problem.inequalities());
                for (unsigned int row = 0; row < problem.inequalities(); ++row)
                    _inequalities.push_back({g(row, 0), g(row, 1), h(row)});
            }

            if (solution.x.getNoOfRows() >= 2)
            {
                const auto x = solution.x.getFirstColumnManipulator();
                _optimum = {x(0), x(1)};
            }
            updateDomain();
        }

        updatePlaybackText();
        reDraw();
    }

    void setPlaybackIndex(const std::size_t historyIndex)
    {
        _visiblePointCount = _history.empty()
            ? 0
            : std::min(historyIndex + 1, _history.size());
        updatePlaybackText();
        reDraw();
    }

    void setError(const char* message)
    {
        _history.clear();
        _equalities.clear();
        _inequalities.clear();
        _visiblePointCount = 0;
        _variables = 0;
        _hasData = false;
        _converged = false;
        _summary = "Solver error";
        _details = message;
        reDraw();
    }
};
