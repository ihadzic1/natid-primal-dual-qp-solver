#pragma once

#include "DashboardView.h"

#include <gui/Window.h>

class MainWindow final : public gui::Window
{
private:
    DashboardView _dashboard;

protected:
    void onInitialAppearance() override
    {
        _dashboard.showInequalityDemo();
    }

public:
    MainWindow()
    : gui::Window(gui::Size(1040, 720))
    {
        setTitle("natidqp solver");
        setCentralView(&_dashboard);
        setResizable(true);
    }
};
