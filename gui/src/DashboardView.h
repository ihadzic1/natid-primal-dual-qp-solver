#pragma once

#include "ConvergenceCanvas.h"

#include "natid_qp/InteriorPointSolver.h"
#include "natid_qp/QPProblem.h"

#include <gui/Button.h>
#include <gui/HorizontalLayout.h>
#include <gui/VerticalLayout.h>
#include <gui/View.h>

#include <exception>

class DashboardView final : public gui::View
{
private:
    gui::Button _inequalityButton;
    gui::Button _equalityButton;
    gui::HorizontalLayout _buttonLayout;
    ConvergenceCanvas _chart;
    gui::VerticalLayout _layout;

    void solveDemo(const bool equalityDemo)
    {
        try
        {
            const natid_qp::QPProblem problem = equalityDemo
                ? natid_qp::makeEqualityDemoProblem()
                : natid_qp::makeInequalityDemoProblem();

            natid_qp::SolverOptions options;
            options.maxIterations = 100;
            options.tolerance = 1e-9;
            options.verbose = false;
            options.printKkt = false;

            const natid_qp::Solution solution =
                natid_qp::InteriorPointSolver(options).solve(problem);

            _chart.setSolution(
                problem,
                solution,
                equalityDemo ? "Equality demo" : "Inequality demo",
                options.tolerance
            );
        }
        catch (const std::exception& error)
        {
            _chart.setError(error.what());
        }
        catch (...)
        {
            _chart.setError("Unknown error while solving the selected problem.");
        }
    }

public:
    DashboardView()
    : gui::View(12, 12, 12, 12)
    , _inequalityButton("Inequality demo")
    , _equalityButton("Equality demo")
    , _buttonLayout(3)
    , _layout(2)
    {
        _inequalityButton.setAsDefault();

        _buttonLayout << _inequalityButton << _equalityButton;
        _buttonLayout.appendSpacer();
        _buttonLayout.setSpaceBetweenCells(8);

        _layout << _buttonLayout << _chart;
        _layout.setSpaceBetweenCells(10);
        setLayout(&_layout);

        _inequalityButton.onClick([this]()
        {
            solveDemo(false);
        });
        _equalityButton.onClick([this]()
        {
            solveDemo(true);
        });
    }

    void showInequalityDemo()
    {
        solveDemo(false);
    }
};
