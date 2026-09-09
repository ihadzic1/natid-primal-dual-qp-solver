#pragma once

#include "ConvergenceCanvas.h"

#include "natid_qp/InteriorPointSolver.h"
#include "natid_qp/MatrixMarket.h"
#include "natid_qp/QPProblem.h"

#include <gui/Button.h>
#include <gui/FileDialog.h>
#include <gui/HorizontalLayout.h>
#include <gui/Timer.h>
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
    gui::Button _playPauseButton;
    gui::Button _nextStepButton;
    gui::Button _resetButton;
    gui::HorizontalLayout _buttonLayout;
    ConvergenceCanvas _chart;
    gui::VerticalLayout _layout;
    gui::Timer _playbackTimer;

    void stopPlayback()
    {
        if (_playbackTimer.isRunning())
            _playbackTimer.stop();
        _playPauseButton.setTitle("Play");
    }

    void startPlayback()
    {
        if (!_chart.hasPlaybackData())
            return;

        if (_chart.isPlaybackComplete())
            _chart.resetPlayback();

        _playPauseButton.setTitle("Pause");
        _playbackTimer.start();
    }

    void togglePlayback()
    {
        if (_playbackTimer.isRunning())
            stopPlayback();
        else
            startPlayback();
    }

    void showNextStep()
    {
        stopPlayback();
        _chart.advancePlayback();
    }

    void resetPlayback()
    {
        stopPlayback();
        _chart.resetPlayback();
    }

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
        stopPlayback();

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
            startPlayback();
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
    , _playPauseButton("Play")
    , _nextStepButton("Next Step")
    , _resetButton("Reset")
    , _buttonLayout(7)
    , _layout(2)
    , _playbackTimer(this, 0.7f, false)
    {
        _inequalityButton.setAsDefault();

        _buttonLayout
            << _inequalityButton
            << _equalityButton
            << _chooseFolderButton
            << _playPauseButton
            << _nextStepButton
            << _resetButton;
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
        _playPauseButton.onClick([this]()
        {
            togglePlayback();
        });
        _nextStepButton.onClick([this]()
        {
            showNextStep();
        });
        _resetButton.onClick([this]()
        {
            resetPlayback();
        });
        _playbackTimer.onTimer([this]()
        {
            if (!_chart.advancePlayback())
                stopPlayback();
        });
    }

    void showInequalityDemo()
    {
        solveDemo(false);
    }
};
