#pragma once

#include "ConvergenceCanvas.h"
#include "ObjectiveCanvas.h"

#include "natid_qp/InteriorPointSolver.h"
#include "natid_qp/DTwinReferenceSolver.h"
#include "natid_qp/MatrixMarket.h"
#include "natid_qp/QPProblem.h"

#include <gui/Button.h>
#include <gui/Application.h>
#include <gui/ComboBox.h>
#include <gui/FileDialog.h>
#include <gui/HorizontalLayout.h>
#include <gui/Label.h>
#include <gui/NumericEdit.h>
#include <gui/Slider.h>
#include <gui/StandardTabView.h>
#include <gui/Timer.h>
#include <gui/VerticalLayout.h>
#include <gui/View.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

class AnimationSpeedSlider final : public gui::Slider
{
protected:
    void getMinSize(gui::Size& minSize) const override
    {
        minSize.width = 180.0;
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

    struct ProblemChoice
    {
        ProblemSource source = ProblemSource::None;
        std::filesystem::path folder;
    };

    static constexpr double c_BasePlaybackIntervalSeconds = 0.7;
    static constexpr td::UINT4 c_QpFolderDialogID = 4101;

    gui::Label _problemLabel;
    gui::ComboBox _problemComboBox;
    gui::Button _chooseFolderButton;
    gui::HorizontalLayout _problemLayout;
    gui::Label _setupLabel;
    gui::Label _toleranceLabel;
    gui::NumericEdit _toleranceEdit;
    gui::Button _runAgainButton;
    gui::HorizontalLayout _setupLayout;
    ObjectiveCanvas _objectiveChart;
    ConvergenceCanvas _residualsChart;
    gui::StandardTabView _charts;
    gui::Label _animationLabel;
    gui::Button _previousStepButton;
    gui::Button _playPauseButton;
    gui::Button _nextStepButton;
    gui::Button _playAgainButton;
    gui::Label _scaleLabel;
    gui::ComboBox _scaleComboBox;
    gui::Label _speedLabel;
    AnimationSpeedSlider _speedSlider;
    gui::Label _speedValueLabel;
    gui::HorizontalLayout _animationLayout;
    gui::HorizontalLayout _chartLayout;
    gui::VerticalLayout _layout;
    gui::Timer _playbackTimer;
    ProblemSource _problemSource = ProblemSource::None;
    std::filesystem::path _selectedFolder;
    std::vector<ProblemChoice> _problemChoices;
    std::size_t _historySize = 0;
    std::size_t _currentHistoryIndex = 0;

    static bool isValidProblemFolder(const std::filesystem::path& folder)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(folder, error) || error)
            return false;

        const auto hasFile = [&folder](const char* name)
        {
            std::error_code fileError;
            return std::filesystem::is_regular_file(folder / name, fileError)
                && !fileError;
        };

        if (!hasFile("Q.mtx")
            || !hasFile("c.mtx")
            || !hasFile("G.mtx")
            || !hasFile("h.mtx"))
        {
            return false;
        }

        return hasFile("A.mtx") == hasFile("b.mtx");
    }

    static std::pair<bool, std::size_t> numericPrefix(const std::string& name)
    {
        std::size_t value = 0;
        std::size_t length = 0;
        while (length < name.size()
            && std::isdigit(static_cast<unsigned char>(name[length])) != 0)
        {
            value = value * 10
                + static_cast<std::size_t>(name[length] - '0');
            ++length;
        }
        return {length > 0, value};
    }

    static bool naturalFolderOrder(
        const std::filesystem::path& left,
        const std::filesystem::path& right
    )
    {
        const std::string leftName = left.filename().string();
        const std::string rightName = right.filename().string();
        const auto leftPrefix = numericPrefix(leftName);
        const auto rightPrefix = numericPrefix(rightName);

        if (leftPrefix.first != rightPrefix.first)
            return leftPrefix.first;
        if (leftPrefix.first && leftPrefix.second != rightPrefix.second)
            return leftPrefix.second < rightPrefix.second;
        return leftName < rightName;
    }

    static bool containsValidProblemFolder(
        const std::filesystem::path& dataDirectory
    )
    {
        std::error_code iteratorError;
        std::filesystem::directory_iterator iterator(
            dataDirectory,
            iteratorError
        );
        const std::filesystem::directory_iterator end;
        while (!iteratorError && iterator != end)
        {
            if (isValidProblemFolder(iterator->path()))
                return true;
            iterator.increment(iteratorError);
        }
        return false;
    }

    static std::optional<std::filesystem::path> findDataDirectory()
    {
        std::vector<std::filesystem::path> candidates;
        const auto addAncestorCandidates =
            [&candidates](std::filesystem::path location)
        {
            // Resource paths differ between a source-tree run, a natID IDE
            // build and an installed application. Walk a few parents instead
            // of relying on one particular working-directory convention.
            for (int depth = 0; depth < 7 && !location.empty(); ++depth)
            {
                candidates.push_back(location / "data");
                const std::filesystem::path parent = location.parent_path();
                if (parent == location)
                    break;
                location = parent;
            }
        };

#ifdef NATID_QP_DATA_DIR
        candidates.emplace_back(NATID_QP_DATA_DIR);
#endif

        // This fallback also works when an existing CMake build recompiles
        // the header without first regenerating its compile definitions.
        const std::filesystem::path sourceHeader(__FILE__);
        addAncestorCandidates(sourceHeader.parent_path());

        std::error_code currentPathError;
        const std::filesystem::path currentPath =
            std::filesystem::current_path(currentPathError);
        if (!currentPathError)
            addAncestorCandidates(currentPath);

        if (const gui::Application* application = gui::getApplication())
        {
            const std::filesystem::path resourcePath(
                application->getResPath().string()
            );
            const std::filesystem::path applicationPath(
                application->getFolderPath().string()
            );
            addAncestorCandidates(resourcePath);
            addAncestorCandidates(applicationPath);

            const auto [argumentCount, arguments] =
                application->getMainArgs();
            constexpr const char* devResourcePrefix = "-devResPath=";
            for (int index = 1; index < argumentCount; ++index)
            {
                const std::string argument = arguments[index]
                    ? arguments[index]
                    : "";
                if (argument.starts_with(devResourcePrefix))
                {
                    addAncestorCandidates(
                        std::filesystem::path(
                            argument.substr(
                                std::char_traits<char>::length(
                                    devResourcePrefix
                                )
                            )
                        )
                    );
                }
                else if (argument == "-devResPath"
                    && index + 1 < argumentCount
                    && arguments[index + 1])
                {
                    addAncestorCandidates(
                        std::filesystem::path(arguments[++index])
                    );
                }
            }
        }

        for (const std::filesystem::path& candidate : candidates)
        {
            std::error_code error;
            if (std::filesystem::is_directory(candidate, error)
                && !error
                && containsValidProblemFolder(candidate))
            {
                return candidate;
            }
        }
        return std::nullopt;
    }

    void populateProblemChoices()
    {
        _problemChoices.clear();

        _problemComboBox.addItem("Inequality demo");
        _problemChoices.push_back({ProblemSource::InequalityDemo, {}});
        _problemComboBox.addItem("Equality demo");
        _problemChoices.push_back({ProblemSource::EqualityDemo, {}});

        const std::optional<std::filesystem::path> dataDirectory =
            findDataDirectory();
        if (dataDirectory)
        {
            std::vector<std::filesystem::path> folders;
            std::error_code iteratorError;
            std::filesystem::directory_iterator iterator(
                *dataDirectory,
                iteratorError
            );
            const std::filesystem::directory_iterator end;
            while (!iteratorError && iterator != end)
            {
                if (isValidProblemFolder(iterator->path()))
                    folders.push_back(iterator->path());
                iterator.increment(iteratorError);
            }

            std::sort(
                folders.begin(),
                folders.end(),
                naturalFolderOrder
            );
            for (const std::filesystem::path& folder : folders)
            {
                const std::string name = folder.filename().string();
                _problemComboBox.addItem(name.c_str());
                _problemChoices.push_back({ProblemSource::Folder, folder});
            }
        }

        _problemComboBox.selectIndex(0, false);
    }

    void selectFolderInProblemComboBox(const std::filesystem::path& folder)
    {
        std::error_code requestedError;
        const std::filesystem::path requested =
            std::filesystem::weakly_canonical(folder, requestedError);

        for (std::size_t index = 0; index < _problemChoices.size(); ++index)
        {
            if (_problemChoices[index].source != ProblemSource::Folder)
                continue;

            std::error_code candidateError;
            const std::filesystem::path candidate =
                std::filesystem::weakly_canonical(
                    _problemChoices[index].folder,
                    candidateError
                );
            if (!requestedError && !candidateError && candidate == requested)
            {
                _problemComboBox.selectIndex(static_cast<int>(index), false);
                return;
            }
        }

        std::string name = folder.filename().string();
        if (name.empty())
            name = folder.string();
        const std::string label = "Custom: " + name;
        _problemComboBox.addItem(label.c_str());
        _problemChoices.push_back({ProblemSource::Folder, folder});
        _problemComboBox.selectIndex(
            static_cast<int>(_problemChoices.size() - 1),
            false
        );
    }

    void solveSelectedProblemChoice()
    {
        const int selectedIndex = _problemComboBox.getSelectedIndex();
        if (selectedIndex < 0
            || static_cast<std::size_t>(selectedIndex) >= _problemChoices.size())
        {
            setError("Select a demo problem or a QP data folder first.");
            return;
        }

        const ProblemChoice& choice =
            _problemChoices[static_cast<std::size_t>(selectedIndex)];
        switch (choice.source)
        {
            case ProblemSource::InequalityDemo:
                solveDemo(false);
                break;
            case ProblemSource::EqualityDemo:
                solveDemo(true);
                break;
            case ProblemSource::Folder:
                _selectedFolder = choice.folder;
                _problemSource = ProblemSource::Folder;
                solveSelectedFolder();
                break;
            case ProblemSource::None:
                setError("Select a demo problem or a QP data folder first.");
                break;
        }
    }

    void syncPlayback()
    {
        _objectiveChart.setPlaybackIndex(_currentHistoryIndex);
        _residualsChart.setPlaybackIndex(_currentHistoryIndex);
    }

    void setError(const char* message)
    {
        stopPlayback();
        _historySize = 0;
        _currentHistoryIndex = 0;
        _objectiveChart.setError(message);
        _residualsChart.setError(message);
    }

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
        if (_historySize == 0)
            return;

        if (_currentHistoryIndex + 1 >= _historySize)
        {
            _currentHistoryIndex = 0;
            syncPlayback();
        }

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
        if (_currentHistoryIndex + 1 < _historySize)
            ++_currentHistoryIndex;
        syncPlayback();
    }

    void showPreviousStep()
    {
        stopPlayback();
        if (_historySize > 0 && _currentHistoryIndex > 0)
            --_currentHistoryIndex;
        syncPlayback();
    }

    void playAgain()
    {
        stopPlayback();
        _currentHistoryIndex = 0;
        syncPlayback();
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

            natid_qp::DTwinReferenceResult dtwinResult;
            natid_qp::SolutionComparison comparison;
            if (solution.converged())
            {
                try
                {
                    natid_qp::DTwinReferenceOptions dtwinOptions;
                    dtwinOptions.tolerance = options.tolerance;
                    dtwinOptions.maxIterations = 200;
                    dtwinOptions.retryWithNatIDWarmStart = true;
                    dtwinResult = natid_qp::solveWithDTwin(
                        problem,
                        solution,
                        dtwinOptions
                    );
                    comparison = natid_qp::compareWithDTwin(
                        solution,
                        dtwinResult,
                        options.tolerance
                    );
                }
                catch (const std::exception& error)
                {
                    dtwinResult.status = natid_qp::DTwinStatus::SolverFailure;
                    dtwinResult.message = std::string("dTwin exception: ")
                        + error.what();
                    comparison = natid_qp::compareWithDTwin(
                        solution,
                        dtwinResult,
                        options.tolerance
                    );
                }
                catch (...)
                {
                    dtwinResult.status = natid_qp::DTwinStatus::SolverFailure;
                    dtwinResult.message = "Unknown dTwin reference-solver error.";
                    comparison = natid_qp::compareWithDTwin(
                        solution,
                        dtwinResult,
                        options.tolerance
                    );
                }
            }
            else
            {
                comparison.message =
                    "NatIDQP did not converge, so dTwin was not run.";
            }

            _objectiveChart.setSolution(
                problem,
                solution,
                problemName.c_str()
            );
            _residualsChart.setSolution(
                problem,
                solution,
                dtwinResult,
                comparison,
                problemName.c_str(),
                options.tolerance
            );
            _historySize = solution.history.size();
            _currentHistoryIndex = 0;
            syncPlayback();
            startPlayback();
        }
        catch (const std::exception& error)
        {
            setError(error.what());
        }
        catch (...)
        {
            setError("Unknown error while solving the selected problem.");
        }
    }

    void solveDemo(const bool equalityDemo)
    {
        _problemSource = equalityDemo
            ? ProblemSource::EqualityDemo
            : ProblemSource::InequalityDemo;
        _problemComboBox.selectIndex(equalityDemo ? 1 : 0, false);

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
            setError(error.what());
        }
        catch (...)
        {
            stopPlayback();
            setError("Unknown error while loading the selected QP folder.");
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
                setError("Choose a demo problem or a QP folder first.");
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
                selectFolderInProblemComboBox(_selectedFolder);
                solveSelectedFolder();
            },
            "Choose"
        );
    }

public:
    DashboardView()
    : gui::View(12, 12, 12, 12)
    , _problemLabel("QP problem:")
    , _problemComboBox("Quick-select a built-in or data-folder QP problem")
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
    , _scaleLabel("Y-axis mode:")
    , _scaleComboBox("Convergence chart scale")
    , _speedLabel("Animation speed:")
    , _speedValueLabel("1.00x")
    , _animationLayout(12)
    , _chartLayout(2)
    , _layout(5)
    , _playbackTimer(
        this,
        static_cast<float>(c_BasePlaybackIntervalSeconds),
        false
    )
    {
        populateProblemChoices();
        _problemComboBox.setSizeLimitForNChars(
            38,
            gui::Control::Limit::Fixed
        );

        _problemLayout
            << _problemLabel
            << _problemComboBox
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
            << _playAgainButton
            << _scaleLabel
            << _scaleComboBox;
        _animationLayout.appendSpacer();
        _animationLayout
            << _speedLabel
            << _speedSlider
            << _speedValueLabel;
        // Keep the final value away from the rounded/right window edge even
        // when the horizontal controls consume their full minimum width.
        _animationLayout.appendSpace(18);
        _animationLayout.setSpaceBetweenCells(8);

        _scaleComboBox.addItem("Linear");
        _scaleComboBox.addItem("Log10");
        _scaleComboBox.addItem("Accuracy (-log10)");
        _scaleComboBox.addItem("Enhanced accuracy");
        _scaleComboBox.selectIndex(1, false);
        _scaleComboBox.sizeToFit();

        _charts.addView(&_objectiveChart, "Objective Function");
        _charts.addView(&_residualsChart, "Residuals");
        _charts.setCurrentViewPos(0);

        // A real layout cell is used as the right safe inset. This remains
        // effective when the tab view expands during a maximize operation.
        _chartLayout << _charts;
        _chartLayout.appendSpace(18);
        _chartLayout.setSpaceBetweenCells(0);

        _layout
            << _problemLayout
            << _setupLayout
            << _animationLayout
            << _chartLayout;
        // Reserve the Windows taskbar/safe-area strip. It also prevents the
        // canvas axis labels from touching the bottom rounded corners.
        _layout.appendSpace(36);
        _layout.setSpaceBetweenCells(10);
        setLayout(&_layout);

        _problemComboBox.onChangedSelection([this]()
        {
            solveSelectedProblemChoice();
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
        _scaleComboBox.onChangedSelection([this]()
        {
            const int selectedIndex = std::clamp(
                _scaleComboBox.getSelectedIndex(),
                0,
                3
            );
            _residualsChart.setScaleMode(
                static_cast<ConvergenceCanvas::ScaleMode>(selectedIndex)
            );
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
            if (_historySize == 0
                || _currentHistoryIndex + 1 >= _historySize)
            {
                stopPlayback();
                return;
            }

            ++_currentHistoryIndex;
            syncPlayback();
            if (_currentHistoryIndex + 1 >= _historySize)
                stopPlayback();
        });
    }

    void showInequalityDemo()
    {
        solveDemo(false);
    }
};
