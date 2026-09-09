#pragma once

#include "ConvergenceCanvas.h"

#include "natid_qp/InteriorPointSolver.h"
#include "natid_qp/MatrixMarket.h"
#include "natid_qp/QPProblem.h"

#include <gui/Button.h>
#include <gui/FileDialog.h>
#include <gui/HorizontalLayout.h>
#include <gui/VerticalLayout.h>
#include <gui/View.h>

#include <exception>
#include <filesystem>
#include <string>

class DashboardView final : public gui::View
{
private:
    static constexpr td::UINT4 c_QpFolderDialogID = 4101;

    gui::Button _inequalityButton;
    gui::Button _equalityButton;
    gui::Button _chooseFolderButton;
    gui::HorizontalLayout _buttonLayout;
    ConvergenceCanvas _chart;
    gui::VerticalLayout _layout;

    static natid_qp::SolverOptions solverOptions()
    {
        natid_qp::SolverOptions options;
        options.maxIterations = 100;
        options.tolerance = 1e-9;
        options.verbose = false;
        options.printKkt = false;
        return options;
    }

    void solveProblem(
        const natid_qp::QPProblem& problem,
        const std::string& problemName
    )
    {
        try
        {
            const natid_qp::SolverOptions options = solverOptions();

            const natid_qp::Solution solution =
                natid_qp::InteriorPointSolver(options).solve(problem);

            _chart.setSolution(
                problem,
                solution,
                problemName.c_str(),
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

    void solveDemo(const bool equalityDemo)
    {
        const natid_qp::QPProblem problem = equalityDemo
            ? natid_qp::makeEqualityDemoProblem()
            : natid_qp::makeInequalityDemoProblem();

        solveProblem(
            problem,
            equalityDemo ? "Equality demo" : "Inequality demo"
        );
    }

    void chooseProblemFolder()
    {
        gui::SelectFolderDialog::show(
            this,
            "Choose QP Folder",
            c_QpFolderDialogID,
            [this](gui::FileDialog* dialog)
            {
                if (!dialog || dialog->getStatus() != gui::FileDialog::Status::OK)
                    return;

                const td::String selectedFolder = dialog->getFileName();
                if (selectedFolder.isEmpty())
                    return;

                try
                {
                    const std::filesystem::path folder(selectedFolder.c_str());
                    const natid_qp::QPProblem problem =
                        natid_qp::loadProblemDirectory(folder);

                    std::string displayName = folder.filename().string();
                    if (displayName.empty())
                        displayName = folder.string();

                    solveProblem(problem, "QP folder: " + displayName);
                }
                catch (const std::exception& error)
                {
                    _chart.setError(error.what());
                }
                catch (...)
                {
                    _chart.setError("Unknown error while loading the selected QP folder.");
                }
            },
            "Choose"
        );
    }

public:
    DashboardView()
    : gui::View(12, 12, 12, 12)
    , _inequalityButton("Inequality demo")
    , _equalityButton("Equality demo")
    , _chooseFolderButton("Choose QP Folder")
    , _buttonLayout(4)
    , _layout(2)
    {
        _inequalityButton.setAsDefault();

        _buttonLayout
            << _inequalityButton
            << _equalityButton
            << _chooseFolderButton;
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
        _chooseFolderButton.onClick([this]()
        {
            chooseProblemFolder();
        });
    }

    void showInequalityDemo()
    {
        solveDemo(false);
    }
};
