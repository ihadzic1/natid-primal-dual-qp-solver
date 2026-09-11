#pragma once

#include "ObjectiveCanvas.h"

#include <gui/CheckBox.h>
#include <gui/HorizontalLayout.h>
#include <gui/Label.h>
#include <gui/VerticalLayout.h>
#include <gui/View.h>

class ObjectiveChartPanel final : public gui::View
{
private:
    ObjectiveCanvas _canvas;
    gui::Label _displayLabel;
    gui::CheckBox _landscapeCheckBox;
    gui::CheckBox _backgroundContoursCheckBox;
    gui::CheckBox _feasibleRegionCheckBox;
    gui::HorizontalLayout _displayLayout;
    gui::VerticalLayout _layout;

    void setTwoDimensionalControlsEnabled(
        const bool enabled,
        const bool hasFeasibleArea
    )
    {
        _landscapeCheckBox.enable(enabled);
        _backgroundContoursCheckBox.enable(enabled);
        _feasibleRegionCheckBox.enable(enabled && hasFeasibleArea);
    }

public:
    ObjectiveChartPanel()
    : gui::View(8, 0, 8, 6)
    , _displayLabel("2D display:")
    , _landscapeCheckBox("Objective colors (blue=low, orange=high)")
    , _backgroundContoursCheckBox("Background iso-lines")
    , _feasibleRegionCheckBox("Feasible region")
    , _displayLayout(6)
    , _layout(2)
    {
        _landscapeCheckBox.setChecked(true, false);
        _backgroundContoursCheckBox.setChecked(true, false);
        _feasibleRegionCheckBox.setChecked(true, false);
        setTwoDimensionalControlsEnabled(false, false);

        _displayLayout
            << _displayLabel
            << _landscapeCheckBox
            << _backgroundContoursCheckBox
            << _feasibleRegionCheckBox;
        _displayLayout.appendSpacer();
        _displayLayout.appendSpace(12);
        _displayLayout.setSpaceBetweenCells(14);

        _layout << _canvas << _displayLayout;
        _layout.setSpaceBetweenCells(6);
        setLayout(&_layout);

        _landscapeCheckBox.onClick([this]()
        {
            _canvas.setShowLandscapeColors(_landscapeCheckBox.isChecked());
        });
        _backgroundContoursCheckBox.onClick([this]()
        {
            _canvas.setShowBackgroundContours(
                _backgroundContoursCheckBox.isChecked()
            );
        });
        _feasibleRegionCheckBox.onClick([this]()
        {
            _canvas.setShowFeasibleRegion(_feasibleRegionCheckBox.isChecked());
        });
    }

    void setSolution(
        const natid_qp::QPProblem& problem,
        const natid_qp::Solution& solution,
        const char* problemName
    )
    {
        _canvas.setSolution(problem, solution, problemName);
        setTwoDimensionalControlsEnabled(
            problem.variables() == 2,
            problem.inequalities() > 0
        );
    }

    void setPlaybackIndex(const std::size_t historyIndex)
    {
        _canvas.setPlaybackIndex(historyIndex);
    }

    void setError(const char* message)
    {
        _canvas.setError(message);
        setTwoDimensionalControlsEnabled(false, false);
    }
};
