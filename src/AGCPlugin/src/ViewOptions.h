#pragma once
#include <gui/View.h>
#include <gui/Label.h>
#include <gui/LineEdit.h>
#include <gui/GridLayout.h>
#include <gui/GridComposer.h>
#include <cstdlib>
#include "AGCPlugin.h"

class ViewOptions : public gui::View
{
protected:
    gui::Label _lblName;    gui::LineEdit _editName;
    gui::Label _lblCase;    gui::LineEdit _editCase;
    gui::Label _lblMaxIter; gui::LineEdit _editMaxIter;
    gui::Label _lbldT;      gui::LineEdit _editDeltaTime;
    gui::Label _lblEndT;    gui::LineEdit _editEndTime;
    gui::Label _lblGenMap;  gui::LineEdit _editGenMap;
    gui::Label _lblKp;      gui::LineEdit _editKp;
    gui::Label _lblKi;      gui::LineEdit _editKi;
    gui::Label _lblTgov;    gui::LineEdit _editTgov;
    gui::Label _lblPmin;    gui::LineEdit _editPmin;
    gui::Label _lblPmax;    gui::LineEdit _editPmax;
    gui::Label _lblH;       gui::LineEdit _editH;
    gui::Label _lblD;       gui::LineEdit _editD;
    gui::Label _lblXdp;     gui::LineEdit _editXdp;

    gui::GridLayout _gl;
    Options _options;

public:
    ViewOptions()
        : _lblName(tr("Model name:"))
        , _lblCase(tr("Case (9/30/118/300):"))
        , _lblMaxIter(tr("Max iters:"))
        , _lbldT(tr("dT [s]:"))
        , _lblEndT(tr("End time [s]:"))
        , _lblGenMap(tr("Gen:AGC map (npr. 2:1,3:1):"))
        , _lblKp(tr("AGC Kp (1/R droop):"))
        , _lblKi(tr("AGC Ki:"))
        , _lblTgov(tr("Tgov [s]:"))
        , _lblPmin(tr("Pm min [p.u.]:"))
        , _lblPmax(tr("Pm max [p.u.]:"))
        , _lblH(tr("H [s]:"))
        , _lblD(tr("D:"))
        , _lblXdp(tr("Xd' [p.u.]:"))
        , _gl(14, 2)
    {
        _editName = "AGC Dynamic Model";
        _editCase = "9";
        _editMaxIter = "100";
        _editDeltaTime = "0.001";
        _editEndTime = "20.0";
        _editGenMap = "2:1,3:1";
        _editKp = "20.0";
        _editKi = "5.0";
        _editTgov = "0.5";
        _editPmin = "0.0";
        _editPmax = "3.0";
        _editH = "5.0";
        _editD = "2.0";
        _editXdp = "0.3";

        gui::GridComposer gc(_gl);
        gc.appendRow(_lblName);    gc.appendCol(_editName);
        gc.appendRow(_lblCase);    gc.appendCol(_editCase);
        gc.appendRow(_lblMaxIter); gc.appendCol(_editMaxIter);
        gc.appendRow(_lbldT);      gc.appendCol(_editDeltaTime);
        gc.appendRow(_lblEndT);    gc.appendCol(_editEndTime);
        gc.appendRow(_lblGenMap);  gc.appendCol(_editGenMap);
        gc.appendRow(_lblKp);      gc.appendCol(_editKp);
        gc.appendRow(_lblKi);      gc.appendCol(_editKi);
        gc.appendRow(_lblTgov);    gc.appendCol(_editTgov);
        gc.appendRow(_lblPmin);    gc.appendCol(_editPmin);
        gc.appendRow(_lblPmax);    gc.appendCol(_editPmax);
        gc.appendRow(_lblH);       gc.appendCol(_editH);
        gc.appendRow(_lblD);       gc.appendCol(_editD);
        gc.appendRow(_lblXdp);     gc.appendCol(_editXdp);

        setLayout(&_gl);
    }

    const Options& getOptions()
    {
        _options.modelName = _editName.getText();
        _options.genAgcMap = _editGenMap.getText();
        _options.caseNumber = td::INT4(std::atoi(_editCase.getText().c_str()));
        _options.maxIter = td::INT4(std::atoi(_editMaxIter.getText().c_str()));
        _options.dTime = std::atof(_editDeltaTime.getText().c_str());
        _options.endTime = std::atof(_editEndTime.getText().c_str());
        _options.kp = std::atof(_editKp.getText().c_str());
        _options.ki = std::atof(_editKi.getText().c_str());
        _options.tgov = std::atof(_editTgov.getText().c_str());
        _options.pmin = std::atof(_editPmin.getText().c_str());
        _options.pmax = std::atof(_editPmax.getText().c_str());
        _options.h = std::atof(_editH.getText().c_str());
        _options.d = std::atof(_editD.getText().c_str());
        _options.xdp = std::atof(_editXdp.getText().c_str());
        return _options;
    }
};