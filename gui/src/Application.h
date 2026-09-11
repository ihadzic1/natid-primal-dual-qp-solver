#pragma once

#include "MainWindow.h"

#include <gui/Application.h>

class Application final : public gui::Application
{
protected:
    gui::Window* createInitialWindow() override
    {
        return new MainWindow();
    }

public:
    Application(const int argc, const char** argv)
    : gui::Application(argc, argv, "ba.unsa.etf.natidqp.solver")
    {
    }
};
