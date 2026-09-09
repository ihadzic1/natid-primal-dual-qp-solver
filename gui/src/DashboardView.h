#pragma once

#include "ConvergenceCanvas.h"

#include "natid_qp/InteriorPointSolver.h"
#include "natid_qp/MatrixMarket.h"
#include "natid_qp/QPProblem.h"

#include <gui/Button.h>
#include <gui/FileDialog.h>
#include <gui/HorizontalLayout.h>
#include <gui/Label.h>
#include <gui/NumericEdit.h>
#include <gui/Slider.h>
#include <gui/Timer.h>
#include <gui/VerticalLayout.h>
#include <gui/View.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>

class AnimationSpeedSlider final : public gui::Slider
{
protected:
    void getMinSize(gui::Size& minSize) const override
    {
        minSize.width = 260.0;
        minSize.height = 24.0;
    }

public:
    AnimationSpeedSlider()
    {
        setRange(0.25, 3.0, 12);
        setValue(1.0, false);
        setToolTip("Animation speed (0.25x to 3.00x)");
    }
};

class DashboardView final : public gui::View
{
private:
    enum class ProblemSource
    {
        None,
        InequalityDemo,
        EqualityDemo,
        Folder
    };

    static constexpr double c_BasePlaybackIntervalSeconds = 0.7;
    static constexpr td::UINT4 c_QpFolderDialogID = 4101;

    gui::Label _problemLabel;
    gui::Button _inequalityButton;
    gui::Button _equalityButton;
    gui::Button _chooseFolderButton;
    gui::HorizontalLayout _problemLayout;
    gui::Label _setupLabel;
    gui::Label _toleranceLabel;
    gui::NumericEdit _toleranceEdit;
    gui::Button _runAgainButton;
    gui::HorizontalLayout _setupLayout;
    ConvergenceCanvas _chart;
    gui::Label _animationLabel;
    gui::Button _previousStepButton;
    gui::Button _playPauseButton;
    gui::Button _nextStepButton;
    gui::Button _playAgainButton;
    gui::Label _speedLabel;
    AnimationSpeedSlider _speedSlider;
    gui::Label _speedValueLabel;
    gui::HorizontalLayout _animationLayout;
    gui::VerticalLayout _layout;
    gui::Timer _playbackTimer;
    ProblemSource _problemSource = ProblemSource::None;
    std::filesystem::path _selectedFolder;

    void updatePlaybackSpeed()
    {
        const double speed = std::clamp(_speedSlider.getValue(), 0.25, 3.0);
        _playbackTimer.setInterval(
            static_cast<float>(c_BasePlaybackIntervalSeconds / speed)
        );

        td::String speedText;
        speedText.format("%.2fx", speed);
        _speedValueLabel.setTitle(speedText);
    }

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

    void showPreviousStep()
    {
        stopPlayback();
        _chart.retreatPlayback();
    }

    void playAgain()
    {
        stopPlayback();
        _chart.resetPlayback();
        startPlayback();
    }

    natid_qp::SolverOptions solverOptions() const
    {
        natid_qp::SolverOptions options;
        options.maxIterations = 100;
        options.tolerance = _toleranceEdit.getValue().r8Val();
        if (!std::isfinite(options.tolerance)
            || options.tolerance < 1e-12
            || options.tolerance > 1.0)
        {
            throw std::invalid_argument(
                "Tolerance (threshold) must be between 1e-12 and 1.0."
            );
        }
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
        _problemSource = equalityDemo
            ? ProblemSource::EqualityDemo
            : ProblemSource::InequalityDemo;

        const natid_qp::QPProblem problem = equalityDemo
            ? natid_qp::makeEqualityDemoProblem()
            : natid_qp::makeInequalityDemoProblem();

        solveProblem(
            problem,
            equalityDemo ? "Equality demo" : "Inequality demo"
        );
    }

    void solveSelectedFolder()
    {
        try
        {
            const natid_qp::QPProblem problem =
                natid_qp::loadProblemDirectory(_selectedFolder);

            std::string displayName = _selectedFolder.filename().string();
            if (displayName.empty())
                displayName = _selectedFolder.string();

            solveProblem(problem, "QP folder: " + displayName);
        }
        catch (const std::exception& error)
        {
            stopPlayback();
            _chart.setError(error.what());
        }
        catch (...)
        {
            stopPlayback();
            _chart.setError("Unknown error while loading the selected QP folder.");
        }
    }

    void runCurrentProblemAgain()
    {
        switch (_problemSource)
        {
            case ProblemSource::InequalityDemo:
                solveDemo(false);
                break;
            case ProblemSource::EqualityDemo:
                solveDemo(true);
                break;
            case ProblemSource::Folder:
                solveSelectedFolder();
                break;
            case ProblemSource::None:
                _chart.setError("Choose a demo problem or a QP folder first.");
                break;
        }
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

                _selectedFolder = std::filesystem::path(selectedFolder.c_str());
                _problemSource = ProblemSource::Folder;
                solveSelectedFolder();
            },
            "Choose"
        );
    }

public:
    DashboardView()
    : gui::View(12, 12, 12, 12)
    , _problemLabel("QP problem:")
    , _inequalityButton("Inequality demo")
    , _equalityButton("Equality demo")
    , _chooseFolderButton("Choose QP Folder")
    , _problemLayout(5)
    , _setupLabel("Solver setup:")
    , _toleranceLabel("Tolerance (threshold):")
    , _toleranceEdit(
        td::real8,
        gui::LineEdit::Messages::DoNotSend,
        false,
        "Solver convergence tolerance (1e-12 to 1.0)",
        3
    )
    , _runAgainButton("Run Again")
    , _setupLayout(5)
    , _animationLabel("Animation:")
    , _previousStepButton("Previous Step")
    , _playPauseButton("Play")
    , _nextStepButton("Next Step")
    , _playAgainButton("Play Again")
    , _speedLabel("Animation speed:")
    , _speedValueLabel("1.00x")
    , _animationLayout(9)
    , _layout(4)
    , _playbackTimer(
        this,
        static_cast<float>(c_BasePlaybackIntervalSeconds),
        false
    )
    {
        _inequalityButton.setAsDefault();

        _problemLayout
            << _problemLabel
            << _inequalityButton
            << _equalityButton
            << _chooseFolderButton;
        _problemLayout.appendSpacer();
        _problemLayout.setSpaceBetweenCells(8);

        _toleranceEdit.setFormat(td::FormatFloat::Scientific);
        _toleranceEdit.setMinValue(1e-12);
        _toleranceEdit.setMaxValue(1.0);
        _toleranceEdit.setValue(1e-9);

        _setupLayout
            << _setupLabel
            << _toleranceLabel
            << _toleranceEdit
            << _runAgainButton;
        _setupLayout.appendSpacer();
        _setupLayout.setSpaceBetweenCells(8);

        _animationLayout
            << _animationLabel
            << _previousStepButton
            << _playPauseButton
            << _nextStepButton
            << _playAgainButton;
        _animationLayout.appendSpacer();
        _animationLayout
            << _speedLabel
            << _speedSlider
            << _speedValueLabel;
        _animationLayout.setSpaceBetweenCells(8);

        _layout
            << _problemLayout
            << _setupLayout
            << _animationLayout
            << _chart;
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
        _runAgainButton.onClick([this]()
        {
            runCurrentProblemAgain();
        });
        _playPauseButton.onClick([this]()
        {
            togglePlayback();
        });
        _nextStepButton.onClick([this]()
        {
            showNextStep();
        });
        _previousStepButton.onClick([this]()
        {
            showPreviousStep();
        });
        _playAgainButton.onClick([this]()
        {
            playAgain();
        });
        _speedSlider.onChangedValue([this]()
        {
            updatePlaybackSpeed();
        });
        _toleranceEdit.onActivate([this]()
        {
            runCurrentProblemAgain();
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
