#pragma once
#include <compiler/Definitions.h>
#include <sc/IPlugin.h>
#include <gui/LineEdit.h>

#ifdef MU_WINDOWS
#ifdef PLUGIN_EXPORTS
#define PLUGIN_API __declspec(dllexport)
#else
#define PLUGIN_API __declspec(dllimport)
#endif
#else
#ifdef PLUGIN_EXPORTS
#define PLUGIN_API __attribute__((visibility("default")))
#else
#define PLUGIN_API
#endif
#endif

// Tip regulacije po generatoru: 0 = standardni model, 1 = AGC
enum class AGCType { Standard = 0, AGC = 1 };

using Options = struct _Options
{
    td::String modelName;
    td::String genAgcMap;   // rezervna opcija ako XML ne sadrzi generatore, npr. "2:1,3:1"
    td::INT4 caseNumber;    // 9, 30, 118 ili 300
    td::INT4 maxIter;
    double dTime;
    double endTime;
    double kp;              // proporcionalno pojacanje primarne regulacije (1/R droop)
    double ki;              // integralno pojacanje sekundarne (AGC) regulacije
    double tgov;            // vremenska konstanta turbine/governora [s]
    double pmin;            // donja granica mehanicke snage [p.u.]
    double pmax;            // gornja granica mehanicke snage [p.u.]
    double h;               // konstanta inercije H [s]
    double d;               // koeficijent prigusenja D
    double xdp;             // tranzijentna reaktansa Xd' [p.u.]
};

void onClosedPluginWindow();

bool createModel(const td::String& inputFileName, const td::String& outFileName, sc::IPlugin* pIPlugin, const Options& options, gui::LineEdit& status); //in AGCPlugin.cpp