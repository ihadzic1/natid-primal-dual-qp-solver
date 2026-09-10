#pragma once

#include "natid_qp/InteriorPointSolver.h"
#include "natid_qp/DTwinReferenceSolver.h"
#include "natid_qp/QPProblem.h"

#include <gui/Canvas.h>
#include <gui/DrawableString.h>
#include <gui/Shape.h>
#include <gui/Transformation.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

class ConvergenceCanvas final : public gui::Canvas
{
public:
    enum class ScaleMode
    {
        Linear = 0,
        Log10,
        AccuracyDigits,
        EnhancedAccuracy
    };

private:
    static constexpr double c_LogFloor = -12.0;
    static constexpr double c_KeyboardZoomFactor = 1.25;
    static constexpr double c_KeyboardPanFraction = 0.12;
    static constexpr double c_MaximumZoom = 64.0;
    static constexpr gui::CoordType c_LeftMargin = 78.0;
    static constexpr gui::CoordType c_RightMargin = 32.0;
    static constexpr gui::CoordType c_TopMargin = 184.0;
    static constexpr gui::CoordType c_BottomMargin = 68.0;

    std::vector<double> _primal;
    std::vector<double> _dual;
    std::vector<double> _mu;
    std::vector<natid_qp::IterationStats> _history;

    td::String _problemName;
    td::String _summary;
    td::String _details;
    td::String _message;
    td::String _finalStatus;
    td::String _finalXPreview;
    td::String _dtwinStatusText;
    td::String _dtwinDifferenceText;

    std::size_t _visiblePointCount = 0;
    std::size_t _variables = 0;
    std::size_t _equalities = 0;
    std::size_t _inequalities = 0;
    double _tolerance = 1e-8;
    double _yMinimum = -12.0;
    double _yMaximum = 1.0;
    double _fullYMinimum = -12.0;
    double _fullYMaximum = 1.0;
    double _viewXMinimum = 0.0;
    double _viewXMaximum = 1.0;
    double _zoomLevel = 1.0;
    ScaleMode _scaleMode = ScaleMode::Log10;
    bool _converged = false;
    bool _hasData = false;
    natid_qp::MatchLevel _matchLevel = natid_qp::MatchLevel::NotCompared;

    [[nodiscard]] td::ColorID comparisonColor() const
    {
        switch (_matchLevel)
        {
            case natid_qp::MatchLevel::ExactMatch:
                return td::ColorID::Green;
            case natid_qp::MatchLevel::CloseMatch:
                return td::ColorID::DodgerBlue;
            case natid_qp::MatchLevel::PartialMatch:
                return td::ColorID::DarkOrange;
            case natid_qp::MatchLevel::Mismatch:
                return td::ColorID::Crimson;
            case natid_qp::MatchLevel::NotCompared:
                return td::ColorID::Gray;
        }
        return td::ColorID::Gray;
    }

    static double toLogValue(const double value)
    {
        if (!std::isfinite(value))
            return c_LogFloor;
        return std::max(c_LogFloor, std::log10(std::max(std::abs(value), 1e-12)));
    }

    [[nodiscard]] double displayValue(const double value) const
    {
        if (!std::isfinite(value))
            return _scaleMode == ScaleMode::Log10 ? c_LogFloor : 0.0;

        if (_scaleMode == ScaleMode::Linear)
            return std::abs(value);

        const double logarithm = toLogValue(value);
        if (_scaleMode == ScaleMode::Log10)
            return logarithm;

        const double accuracyDigits = -logarithm;
        if (_scaleMode == ScaleMode::AccuracyDigits)
            return accuracyDigits;

        // Signed square preserves ordering on both sides of value=1 while
        // strongly separating very small residuals near convergence.
        return std::copysign(
            accuracyDigits * accuracyDigits,
            accuracyDigits
        );
    }

    [[nodiscard]] const char* scaleAxisTitle() const
    {
        switch (_scaleMode)
        {
            case ScaleMode::Linear:
                return "value";
            case ScaleMode::Log10:
                return "log10(value)";
            case ScaleMode::AccuracyDigits:
                return "accuracy (-log10)";
            case ScaleMode::EnhancedAccuracy:
                return "enhanced accuracy^2";
        }
        return "value";
    }

    [[nodiscard]] gui::Rect plotBounds() const
    {
        gui::Size size;
        getSize(size);
        return gui::Rect(
            c_LeftMargin,
            c_TopMargin,
            size.width - c_RightMargin,
            size.height - c_BottomMargin
        );
    }

    [[nodiscard]] gui::CoordType mapX(
        const std::size_t index,
        const gui::Rect& plot
    ) const
    {
        const double range = _viewXMaximum - _viewXMinimum;
        if (range <= std::numeric_limits<double>::epsilon())
            return plot.left;
        return plot.left
            + (static_cast<double>(index) - _viewXMinimum)
                / range * plot.width();
    }

    gui::CoordType mapY(const double value, const gui::Rect& plot) const
    {
        const double range = _yMaximum - _yMinimum;
        if (range <= std::numeric_limits<double>::epsilon())
            return plot.bottom;
        const double normalized = (value - _yMinimum) / range;
        return plot.bottom - normalized * plot.height();
    }

    void resetZoom()
    {
        _zoomLevel = 1.0;
        _viewXMinimum = 0.0;
        _viewXMaximum = _history.size() > 1
            ? static_cast<double>(_history.size() - 1)
            : 1.0;
        _yMinimum = _fullYMinimum;
        _yMaximum = _fullYMaximum;
    }

    void zoomAt(const double requestedFactor, const gui::Point& requestedAnchor)
    {
        if (!_hasData || !std::isfinite(requestedFactor) || requestedFactor <= 0.0)
            return;

        const gui::Rect plot = plotBounds();
        if (plot.width() <= 0.0 || plot.height() <= 0.0)
            return;

        const double fullXSpan = _history.size() > 1
            ? static_cast<double>(_history.size() - 1)
            : 1.0;
        const double maximumZoom = std::max(
            1.0,
            std::min(c_MaximumZoom, fullXSpan)
        );
        const double newZoom = std::clamp(
            _zoomLevel * requestedFactor,
            1.0,
            maximumZoom
        );
        const double effectiveFactor = newZoom / _zoomLevel;
        if (std::abs(effectiveFactor - 1.0) <= 1e-12)
            return;

        if (newZoom <= 1.0 + 1e-12)
        {
            resetZoom();
            reDraw();
            return;
        }

        const gui::Point anchor = plot.contains(requestedAnchor)
            ? requestedAnchor
            : gui::Point(
                plot.left + 0.5 * plot.width(),
                plot.top + 0.5 * plot.height()
            );
        const double xRatio = std::clamp(
            (anchor.x - plot.left) / plot.width(),
            0.0,
            1.0
        );
        const double yRatio = std::clamp(
            (anchor.y - plot.top) / plot.height(),
            0.0,
            1.0
        );
        const double anchorX = _viewXMinimum
            + xRatio * (_viewXMaximum - _viewXMinimum);
        const double anchorY = _yMaximum
            - yRatio * (_yMaximum - _yMinimum);

        _viewXMinimum = anchorX
            - (anchorX - _viewXMinimum) / effectiveFactor;
        _viewXMaximum = anchorX
            + (_viewXMaximum - anchorX) / effectiveFactor;
        _yMinimum = anchorY - (anchorY - _yMinimum) / effectiveFactor;
        _yMaximum = anchorY + (_yMaximum - anchorY) / effectiveFactor;

        const auto keepInside = [](double& minimum,
                                   double& maximum,
                                   const double fullMinimum,
                                   const double fullMaximum)
        {
            if (minimum < fullMinimum)
            {
                maximum += fullMinimum - minimum;
                minimum = fullMinimum;
            }
            if (maximum > fullMaximum)
            {
                minimum -= maximum - fullMaximum;
                maximum = fullMaximum;
            }
        };
        keepInside(_viewXMinimum, _viewXMaximum, 0.0, fullXSpan);
        keepInside(_yMinimum, _yMaximum, _fullYMinimum, _fullYMaximum);
        _zoomLevel = newZoom;
        reDraw();
    }

    void zoomAtCenter(const double factor)
    {
        const gui::Rect plot = plotBounds();
        zoomAt(
            factor,
            gui::Point(
                plot.left + 0.5 * plot.width(),
                plot.top + 0.5 * plot.height()
            )
        );
    }

    void panBy(const double horizontalFraction, const double verticalFraction)
    {
        if (!_hasData || _zoomLevel <= 1.0 + 1e-12)
            return;

        const double fullXSpan = _history.size() > 1
            ? static_cast<double>(_history.size() - 1)
            : 1.0;
        const double xShift = horizontalFraction
            * (_viewXMaximum - _viewXMinimum);
        const double yShift = verticalFraction
            * (_yMaximum - _yMinimum);
        _viewXMinimum += xShift;
        _viewXMaximum += xShift;
        _yMinimum += yShift;
        _yMaximum += yShift;

        const auto keepInside = [](double& minimum,
                                   double& maximum,
                                   const double fullMinimum,
                                   const double fullMaximum)
        {
            if (minimum < fullMinimum)
            {
                maximum += fullMinimum - minimum;
                minimum = fullMinimum;
            }
            if (maximum > fullMaximum)
            {
                minimum -= maximum - fullMaximum;
                maximum = fullMaximum;
            }
        };
        keepInside(_viewXMinimum, _viewXMaximum, 0.0, fullXSpan);
        keepInside(_yMinimum, _yMaximum, _fullYMinimum, _fullYMaximum);
        reDraw();
    }

    void updateYRange()
    {
        if (_scaleMode == ScaleMode::Linear)
        {
            double maximum = std::abs(_tolerance);
            const auto inspectMaximum = [&maximum](const std::vector<double>& values)
            {
                for (const double value : values)
                {
                    if (std::isfinite(value))
                        maximum = std::max(maximum, std::abs(value));
                }
            };

            inspectMaximum(_primal);
            inspectMaximum(_dual);
            inspectMaximum(_mu);
            _yMinimum = 0.0;
            _yMaximum = maximum > std::numeric_limits<double>::epsilon()
                ? 1.05 * maximum
                : 1.0;
        }
        else
        {
            double minimum = displayValue(_tolerance);
            double maximum = displayValue(_tolerance);

            const auto inspect = [this, &minimum, &maximum](
                const std::vector<double>& values
            )
            {
                for (const double value : values)
                {
                    const double displayed = displayValue(value);
                    minimum = std::min(minimum, displayed);
                    maximum = std::max(maximum, displayed);
                }
            };

            inspect(_primal);
            inspect(_dual);
            inspect(_mu);

            _yMinimum = std::floor(minimum);
            _yMaximum = std::ceil(maximum);
            if (_yMaximum - _yMinimum < 2.0)
                _yMaximum = _yMinimum + 2.0;
        }

        _fullYMinimum = _yMinimum;
        _fullYMaximum = _yMaximum;
        resetZoom();
    }

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

    void drawLegendItem(
        const gui::CoordType x,
        const gui::CoordType y,
        const td::ColorID color,
        const char* label,
        const td::LinePattern pattern = td::LinePattern::Solid
    ) const
    {
        const gui::Point start(x, y);
        const gui::Point end(x + 28.0, y);
        gui::Shape::drawLine(start, end, color, 3.0f, pattern);
        gui::DrawableString::draw(
            label,
            std::char_traits<char>::length(label),
            gui::Point(x + 35.0, y - 8.0),
            gui::Font::ID::SystemSmaller,
            td::ColorID::SysText
        );
    }

    void drawSeries(
        const std::vector<double>& values,
        const gui::Rect& plot,
        const td::ColorID color,
        const std::size_t visibleCount
    ) const
    {
        const std::size_t count = std::min(visibleCount, values.size());
        if (count == 0)
            return;

        const std::size_t firstIndex = std::min(
            static_cast<std::size_t>(std::floor(_viewXMinimum)),
            count - 1
        );
        const std::size_t lastIndex = std::min(
            static_cast<std::size_t>(std::ceil(_viewXMaximum)),
            count - 1
        );

        for (std::size_t index = firstIndex + 1; index <= lastIndex; ++index)
        {
            const gui::Point previous(
                mapX(index - 1, plot),
                mapY(displayValue(values[index - 1]), plot)
            );
            const gui::Point current(
                mapX(index, plot),
                mapY(displayValue(values[index]), plot)
            );
            gui::Shape::drawLine(previous, current, color, 2.5f);
        }

        for (std::size_t index = firstIndex; index <= lastIndex; ++index)
        {
            const gui::Point point(
                mapX(index, plot),
                mapY(displayValue(values[index]), plot)
            );
            const gui::Rect marker(
                point.x - 2.5,
                point.y - 2.5,
                point.x + 2.5,
                point.y + 2.5
            );
            gui::Shape::drawRect(marker, color);
        }
    }

    void drawChart(const gui::Rect& bounds)
    {
        const gui::Rect plot(
            bounds.left + c_LeftMargin,
            bounds.top + c_TopMargin,
            bounds.right - c_RightMargin,
            bounds.bottom - c_BottomMargin
        );

        if (plot.width() < 180.0 || plot.height() < 140.0)
        {
            const td::String message(
                "Increase the window size to display the convergence chart."
            );
            drawText(
                message,
                bounds,
                gui::Font::ID::SystemNormal,
                td::ColorID::SysText,
                td::TextAlignment::Center,
                td::VAlignment::Center
            );
            return;
        }

        gui::Shape::drawRect(plot, td::ColorID::SysBackAlt1);

        constexpr int yDivisions = 6;
        for (int division = 0; division <= yDivisions; ++division)
        {
            const double ratio =
                static_cast<double>(division) / static_cast<double>(yDivisions);
            const gui::CoordType y = plot.top + ratio * plot.height();
            gui::Shape::drawLine(
                gui::Point(plot.left, y),
                gui::Point(plot.right, y),
                td::ColorID::Gray,
                1.0f,
                td::LinePattern::Solid,
                0.35f
            );

            const double value = _yMaximum - ratio * (_yMaximum - _yMinimum);
            td::String label;
            if (_scaleMode == ScaleMode::Linear)
                label.format("%.2e", value);
            else
                label.format("%.1f", value);
            drawText(
                label,
                gui::Rect(bounds.left + 5.0, y - 11.0, plot.left - 8.0, y + 11.0),
                gui::Font::ID::SystemSmallest,
                td::ColorID::SysText,
                td::TextAlignment::Right
            );
        }

        const std::size_t pointCount = _primal.size();
        if (pointCount > 0)
        {
            const std::size_t firstTick = std::min(
                static_cast<std::size_t>(std::ceil(_viewXMinimum - 1e-9)),
                pointCount - 1
            );
            const std::size_t lastTick = std::min(
                static_cast<std::size_t>(std::floor(_viewXMaximum + 1e-9)),
                pointCount - 1
            );
            const std::size_t availableTicks = lastTick >= firstTick
                ? lastTick - firstTick + 1
                : 0;
            const std::size_t tickCount = std::min<std::size_t>(7, availableTicks);

            const auto drawIterationTick =
                [this, &plot, &bounds](const std::size_t index)
            {
                const gui::CoordType x = mapX(index, plot);
                gui::Shape::drawLine(
                    gui::Point(x, plot.top),
                    gui::Point(x, plot.bottom),
                    td::ColorID::Gray,
                    1.0f,
                    td::LinePattern::Solid,
                    0.25f
                );

                td::String label;
                label.format("%d", _history[index].iteration);
                drawText(
                    label,
                    gui::Rect(x - 22.0, plot.bottom + 5.0, x + 22.0, bounds.bottom - 35.0),
                    gui::Font::ID::SystemSmallest,
                    td::ColorID::SysText,
                    td::TextAlignment::Center
                );
            };

            std::size_t previousTick = pointCount;
            for (std::size_t tick = 0; tick < tickCount; ++tick)
            {
                const std::size_t index = tickCount <= 1
                    ? firstTick
                    : firstTick + tick * (lastTick - firstTick) / (tickCount - 1);
                if (index == previousTick)
                    continue;
                drawIterationTick(index);
                previousTick = index;
            }
        }

        gui::Shape::drawRect(plot, td::ColorID::SysText, 1.0f);

        gui::Transformation::saveContext();
        gui::Transformation::setClip(plot);
        const gui::CoordType toleranceY = mapY(displayValue(_tolerance), plot);
        gui::Shape::drawLine(
            gui::Point(plot.left, toleranceY),
            gui::Point(plot.right, toleranceY),
            td::ColorID::DarkOrange,
            1.5f,
            td::LinePattern::Dash
        );

        drawSeries(
            _primal,
            plot,
            td::ColorID::DodgerBlue,
            _visiblePointCount
        );
        drawSeries(
            _dual,
            plot,
            td::ColorID::Crimson,
            _visiblePointCount
        );
        drawSeries(
            _mu,
            plot,
            td::ColorID::Green,
            _visiblePointCount
        );
        gui::Transformation::restoreContext();
        gui::Shape::drawRect(plot, td::ColorID::SysText, 1.0f);

        const td::String xAxis("iteration");
        drawText(
            xAxis,
            gui::Rect(plot.left, bounds.bottom - 34.0, plot.right, bounds.bottom - 10.0),
            gui::Font::ID::SystemSmaller,
            td::ColorID::SysText,
            td::TextAlignment::Center
        );

        const td::String yAxis(scaleAxisTitle());
        drawText(
            yAxis,
            gui::Rect(bounds.left + 6.0, plot.top - 28.0, plot.left + 110.0, plot.top - 4.0),
            gui::Font::ID::SystemSmaller,
            td::ColorID::SysText
        );

        gui::CoordType legendX = std::max(plot.left + 130.0, plot.right - 510.0);
        const gui::CoordType legendY = plot.top - 17.0;
        drawLegendItem(legendX, legendY, td::ColorID::DodgerBlue, "primal");
        legendX += 118.0;
        drawLegendItem(legendX, legendY, td::ColorID::Crimson, "dual");
        legendX += 100.0;
        drawLegendItem(legendX, legendY, td::ColorID::Green, "mu");
        legendX += 88.0;
        drawLegendItem(
            legendX,
            legendY,
            td::ColorID::DarkOrange,
            "tolerance",
            td::LinePattern::Dash
        );
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
            "%s | step %d/%d | objective: %.12g",
            _problemName.c_str(),
            stats.iteration,
            finalStats.iteration,
            stats.objective
        );
        _details.format(
            "primal: %.3e | dual: %.3e | mu: %.3e | gap: %.3e",
            stats.primalResidual,
            stats.dualResidual,
            stats.mu,
            stats.dualityGap
        );
        _message.format(
            "a_pri: %.4f | a_dual: %.4f | sigma: %.3g | KKT nnz: %llu | "
            "n=%llu, p=%llu, m=%llu | final: %s | x(final)=%s",
            stats.alphaPrimal,
            stats.alphaDual,
            stats.sigma,
            static_cast<unsigned long long>(stats.kktNonZeros),
            static_cast<unsigned long long>(_variables),
            static_cast<unsigned long long>(_equalities),
            static_cast<unsigned long long>(_inequalities),
            _finalStatus.c_str(),
            _finalXPreview.c_str()
        );
    }

protected:
    void onPrimaryButtonPressed(const gui::InputDevice&) override
    {
        setFocus(false);
    }

    bool onZoom(const gui::InputDevice& inputDevice) override
    {
        // getModelPoint() is local to this canvas. getFramePoint() is relative
        // to the containing frame and makes the cursor anchor miss the plot.
        zoomAt(inputDevice.getScale(), inputDevice.getModelPoint());
        return true;
    }

    bool onKeyPressed(const gui::Key& key) override
    {
        if (key.isCmdOnMacOrCtrlOnOtherPressed())
        {
            const char character = key.getChar();
            const gui::Key::Virtual virtualKey = key.getVirtual();
            if (character == '+' || character == '='
                || virtualKey == gui::Key::Virtual::NumPlus)
            {
                zoomAtCenter(c_KeyboardZoomFactor);
                return true;
            }
            if (character == '-' || character == '_'
                || virtualKey == gui::Key::Virtual::NumMinus)
            {
                zoomAtCenter(1.0 / c_KeyboardZoomFactor);
                return true;
            }
            if (character == '0' || virtualKey == gui::Key::Virtual::Num0)
            {
                resetZoom();
                reDraw();
                return true;
            }
        }

        if (!key.isAltCtrlOrCmdPressed())
        {
            const char character = key.getChar();
            const gui::Key::Virtual virtualKey = key.getVirtual();
            if (character == 'a' || character == 'A'
                || virtualKey == gui::Key::Virtual::Left
                || virtualKey == gui::Key::Virtual::NumLeft)
            {
                panBy(-c_KeyboardPanFraction, 0.0);
                return true;
            }
            if (character == 'd' || character == 'D'
                || virtualKey == gui::Key::Virtual::Right
                || virtualKey == gui::Key::Virtual::NumRight)
            {
                panBy(c_KeyboardPanFraction, 0.0);
                return true;
            }
            if (character == 'w' || character == 'W'
                || virtualKey == gui::Key::Virtual::Up
                || virtualKey == gui::Key::Virtual::NumUp)
            {
                panBy(0.0, c_KeyboardPanFraction);
                return true;
            }
            if (character == 's' || character == 'S'
                || virtualKey == gui::Key::Virtual::Down
                || virtualKey == gui::Key::Virtual::NumDown)
            {
                panBy(0.0, -c_KeyboardPanFraction);
                return true;
            }
        }
        return gui::Canvas::onKeyPressed(key);
    }

    void onDraw(const gui::Rect&) override
    {
        gui::Size size;
        getSize(size);
        const gui::Rect bounds(0.0, 0.0, size.width, size.height);

        gui::Shape::drawRect(bounds, td::ColorID::SysCtrlBack);
        const gui::Rect header(bounds.left, bounds.top, bounds.right, bounds.top + 158.0);
        gui::Shape::drawRect(header, td::ColorID::SysBackAlt2);

        const td::String title("NatIDQP residuals");
        drawText(
            title,
            gui::Rect(22.0, 10.0, bounds.right - 22.0, 42.0),
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
            gui::Rect(22.0, 68.0, bounds.right - 20.0, 93.0),
            gui::Font::ID::SystemSmaller,
            td::ColorID::SysText
        );
        drawText(
            _message,
            gui::Rect(22.0, 91.0, bounds.right - 20.0, 113.0),
            gui::Font::ID::SystemSmallest,
            _converged ? td::ColorID::SysText : td::ColorID::Crimson
        );

        gui::Shape::drawRect(
            gui::Rect(23.0, 121.0, 31.0, 129.0),
            comparisonColor()
        );
        drawText(
            _dtwinStatusText,
            gui::Rect(39.0, 112.0, bounds.right - 20.0, 139.0),
            gui::Font::ID::SystemSmaller,
            comparisonColor()
        );
        drawText(
            _dtwinDifferenceText,
            gui::Rect(22.0, 135.0, bounds.right - 20.0, 157.0),
            gui::Font::ID::SystemSmallest,
            td::ColorID::SysText
        );

        if (_hasData)
        {
            drawChart(bounds);
        }
        else
        {
            const td::String noData("No solver history is available.");
            drawText(
                noData,
                gui::Rect(20.0, 170.0, bounds.right - 20.0, bounds.bottom - 20.0),
                gui::Font::ID::SystemNormal,
                td::ColorID::SysText,
                td::TextAlignment::Center,
                td::VAlignment::Center
            );
        }
    }

public:
    ConvergenceCanvas()
    : gui::Canvas({
        gui::InputDevice::Event::PrimaryClicks,
        gui::InputDevice::Event::Zoom,
        gui::InputDevice::Event::Keyboard
    })
    {
        enableResizeEvent(true);
        setFocusable(true);
        setClipsToBounds();
        setToolTip(
            "Zoom: mouse wheel or Ctrl+/Ctrl-. Pan: arrows or W/A/S/D. "
            "Ctrl+0 resets the view."
        );
        _summary = "Solver has not been run.";
        _details = "Choose a demo problem or a QP folder.";
        _message = "";
        _dtwinStatusText = "dTwin: not compared";
        _dtwinDifferenceText = "";
    }

    void setSolution(
        const natid_qp::QPProblem& problem,
        const natid_qp::Solution& solution,
        const natid_qp::DTwinReferenceResult& dtwinResult,
        const natid_qp::SolutionComparison& comparison,
        const char* problemName,
        const double tolerance
    )
    {
        _primal.clear();
        _dual.clear();
        _mu.clear();
        _history = solution.history;

        _primal.reserve(solution.history.size());
        _dual.reserve(solution.history.size());
        _mu.reserve(solution.history.size());

        for (const natid_qp::IterationStats& stats : solution.history)
        {
            _primal.push_back(std::abs(stats.primalResidual));
            _dual.push_back(std::abs(stats.dualResidual));
            _mu.push_back(std::abs(stats.mu));
        }

        _problemName = problemName;
        _variables = problem.variables();
        _equalities = problem.equalities();
        _inequalities = problem.inequalities();
        _tolerance = std::abs(tolerance);
        _converged = solution.converged();
        _hasData = !_primal.empty();
        _visiblePointCount = _hasData ? 1 : 0;

        std::ostringstream xPreview;
        xPreview << '[' << std::setprecision(6);
        const unsigned int variableCount = solution.x.getNoOfRows();
        const unsigned int previewCount = std::min(variableCount, 4U);
        const auto xValues = solution.x.getFirstColumnManipulator();
        for (unsigned int index = 0; index < previewCount; ++index)
        {
            if (index > 0)
                xPreview << ", ";
            xPreview << xValues(index);
        }
        if (variableCount > previewCount)
            xPreview << ", ...";
        xPreview << ']';

        _finalStatus = natid_qp::toString(solution.status);
        _finalXPreview = xPreview.str().c_str();
        _matchLevel = comparison.level;

        std::ostringstream dtwinStatus;
        dtwinStatus << "dTwin: " << natid_qp::toString(comparison.level);
        if (dtwinResult.solved())
        {
            dtwinStatus << " | objective: " << std::setprecision(12)
                        << dtwinResult.objective
                        << " | KKT: " << std::scientific
                        << std::setprecision(3) << dtwinResult.kktResidual
                        << " | start: "
                        << (dtwinResult.usedWarmStart ? "NatIDQP warm" : "neutral");
        }
        else if (!dtwinResult.message.empty())
        {
            dtwinStatus << " | " << dtwinResult.message;
        }
        _dtwinStatusText = dtwinStatus.str().c_str();

        std::ostringstream dtwinDifference;
        if (dtwinResult.solved())
        {
            dtwinDifference << std::scientific << std::setprecision(3)
                            << "max|dx|=" << comparison.maxAbsoluteXDifference
                            << " | rel x=" << comparison.relativeXDifference
                            << " | |dObj|="
                            << comparison.absoluteObjectiveDifference
                            << " | x(dTwin)=[";
            const std::size_t previewCount =
                std::min<std::size_t>(dtwinResult.x.size(), 4);
            for (std::size_t index = 0; index < previewCount; ++index)
            {
                if (index > 0)
                    dtwinDifference << ", ";
                dtwinDifference << dtwinResult.x[index];
            }
            if (dtwinResult.x.size() > previewCount)
                dtwinDifference << ", ...";
            dtwinDifference << "] | " << comparison.message;
        }
        else
        {
            dtwinDifference << comparison.message;
        }
        _dtwinDifferenceText = dtwinDifference.str().c_str();

        updateYRange();
        updatePlaybackText();
        reDraw();
    }

    void setScaleMode(const ScaleMode mode)
    {
        if (_scaleMode == mode)
            return;

        _scaleMode = mode;
        updateYRange();
        reDraw();
    }

    [[nodiscard]] ScaleMode getScaleMode() const
    {
        return _scaleMode;
    }

    [[nodiscard]] bool hasPlaybackData() const
    {
        return !_history.empty();
    }

    [[nodiscard]] bool isPlaybackComplete() const
    {
        return _history.empty() || _visiblePointCount >= _history.size();
    }

    void setPlaybackIndex(const std::size_t historyIndex)
    {
        _visiblePointCount = _history.empty()
            ? 0
            : std::min(historyIndex + 1, _history.size());
        updatePlaybackText();
        reDraw();
    }

    bool advancePlayback()
    {
        if (_history.empty())
            return false;

        if (_visiblePointCount < _history.size())
            ++_visiblePointCount;

        updatePlaybackText();
        reDraw();
        return !isPlaybackComplete();
    }

    bool retreatPlayback()
    {
        if (_history.empty())
            return false;

        if (_visiblePointCount > 1)
            --_visiblePointCount;

        updatePlaybackText();
        reDraw();
        return _visiblePointCount > 1;
    }

    void resetPlayback()
    {
        _visiblePointCount = _history.empty() ? 0 : 1;
        updatePlaybackText();
        reDraw();
    }

    void setError(const char* message)
    {
        _primal.clear();
        _dual.clear();
        _mu.clear();
        _history.clear();
        _visiblePointCount = 0;
        _converged = false;
        _hasData = false;
        _summary = "Solver error";
        _details = "The selected QP input could not be loaded or solved.";
        _message = message;
        _matchLevel = natid_qp::MatchLevel::NotCompared;
        _dtwinStatusText = "dTwin: not compared";
        _dtwinDifferenceText = "Primary solver error.";
        _fullYMinimum = -12.0;
        _fullYMaximum = 1.0;
        resetZoom();
        reDraw();
    }
};
