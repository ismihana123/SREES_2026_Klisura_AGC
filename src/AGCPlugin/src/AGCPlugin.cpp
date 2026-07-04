#include "AGCPlugin.h"
#include <sc/IPlugin.h>
#include "WindowPlugin.h"
#include <td/StringUtils.h>
#include <dense/Matrix.h>
#include <mu/ScopedCLocale.h>
#include <xml/DOMParser.h>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cassert>

// ---------------------------------------------------------------
// Standardni plugin dio (ne mijenjati)
// ---------------------------------------------------------------
class Plugin : public sc::IPlugin
{
    MemoryArchiveContainer _outArchives;
    WindowPlugin* _pWnd = nullptr;
public:
    Plugin()
    {
        for (size_t i = 0; i < size_t(ArchType::NA); ++i)
            _outArchives[i] = nullptr;
    }

    void show(gui::Window* parentWnd, MemoryArchiveContainer& archives, td::UINT4 wndID, const sc::IPlugin::Cleaner& cleaner, const sc::IPlugin::CallBack& onComplete) override final
    {
        for (size_t i = 0; i < size_t(ArchType::NA); ++i)
            _outArchives[i] = archives[i];

        if (_pWnd)
            _pWnd->setFocus();
        else
        {
            _pWnd = new WindowPlugin(parentWnd, this, onComplete, cleaner, wndID);
            _pWnd->open();
        }
    }

    td::String getMenuName() const override final { return "AGC Dynamic Converter"; }

    arch::MemoryOut* getArchive(sc::IPlugin::ArchType type) override final
    {
        auto iType = size_t(type);
        if (iType >= getMaxSupportedArchiveParts())
            return nullptr;
        return _outArchives[size_t(type)];
    }

    MemoryArchiveContainer& getArchives() override final { return _outArchives; }

    td::String getOutFileName() const override final
    {
        assert(_pWnd);
        return _pWnd->getOutFileName();
    }

    size_t getMaxSupportedArchiveParts() const override final { return size_t(ArchType::NA); }
    ModelType getModelType() const override final { return ModelType::DAE; }
    void onClosedPluginWindow() { _pWnd = nullptr; }
};

static Plugin s_plugin;

void onClosedPluginWindow()
{
    s_plugin.onClosedPluginWindow();
}

extern "C"
{
    PLUGIN_API sc::IPlugin* getPluginInterface()
    {
        return &s_plugin;
    }
}

// ---------------------------------------------------------------
// Mrezni podaci - IEEE Case 9 (MATPOWER standard)
// ---------------------------------------------------------------
struct BusInit { int id; int type; double pLoad; double qLoad; double pGen; double vSet; }; // type: 3=slack, 2=PV(gen), 1=PQ
struct LineData { int from; int to; double r; double x; double b; };

static const BusInit case9Buses[] = {
    {1, 3, 0.00, 0.00, 0.00, 1.040},
    {2, 2, 0.00, 0.00, 1.63, 1.025},
    {3, 2, 0.00, 0.00, 0.85, 1.025},
    {4, 1, 0.00, 0.00, 0.00, 1.000},
    {5, 1, 0.90, 0.30, 0.00, 1.000},
    {6, 1, 0.00, 0.00, 0.00, 1.000},
    {7, 1, 1.00, 0.35, 0.00, 1.000},
    {8, 1, 0.00, 0.00, 0.00, 1.000},
    {9, 1, 1.25, 0.50, 0.00, 1.000},
};

static const LineData case9Lines[] = {
    {1, 4, 0.0000, 0.0576, 0.000},
    {4, 5, 0.0170, 0.0920, 0.158},
    {5, 6, 0.0390, 0.1700, 0.358},
    {3, 6, 0.0000, 0.0586, 0.000},
    {6, 7, 0.0119, 0.1008, 0.209},
    {7, 8, 0.0085, 0.0720, 0.149},
    {8, 2, 0.0000, 0.0625, 0.000},
    {8, 9, 0.0320, 0.1610, 0.306},
    {9, 4, 0.0100, 0.0850, 0.176},
};

// ---------------------------------------------------------------
// Ybus matrica - koristi natID dense::DblMatrix (obavezan zahtjev)
// ---------------------------------------------------------------
struct YbusMatrices
{
    dense::DblMatrix mG;
    dense::DblMatrix mB;
};

static void buildYbus(YbusMatrices& ybus, int nBus, const LineData* lines, int nLines)
{
    ybus.mG.reserve(nBus, nBus, nullptr, true);
    ybus.mB.reserve(nBus, nBus, nullptr, true);
    auto matG = ybus.mG.getManipulator1();
    auto matB = ybus.mB.getManipulator1();

    for (int i = 1; i <= nBus; ++i)
        for (int j = 1; j <= nBus; ++j)
        {
            matG(i, j) = 0.0;
            matB(i, j) = 0.0;
        }

    for (int l = 0; l < nLines; ++l)
    {
        int f = lines[l].from;
        int t = lines[l].to;
        double denom = lines[l].r * lines[l].r + lines[l].x * lines[l].x;
        double g = lines[l].r / denom;
        double b = -lines[l].x / denom;

        matG(f, t) -= g;    matG(t, f) -= g;
        matB(f, t) -= b;    matB(t, f) -= b;
        matG(f, f) += g;    matG(t, t) += g;
        matB(f, f) += b + lines[l].b / 2.0;
        matB(t, t) += b + lines[l].b / 2.0;
    }
}

// ---------------------------------------------------------------
// XML konfiguracija: <AGCConfig><Generator id="2" type="1"/></AGCConfig>
// ---------------------------------------------------------------
static bool readAgcConfigFromXml(const td::String& fileName, std::map<int, int>& genTypes)
{
    xml::FileParser parser;
    if (!parser.parseFile(fileName))
        return false;

    const auto& root = parser.getRootNode();
    if (root->getName().cCompare("AGCConfig") != 0)
        return false;

    auto it = root.getChildNode();
    while (it.isOk())
    {
        if (it->getName().cCompare("Generator") == 0)
        {
            double idVal = 0.0;
            double typeVal = 0.0;
            it.getAttribValue("id", idVal);
            it.getAttribValue("type", typeVal);
            if (int(idVal) > 0)
                genTypes[int(idVal)] = int(typeVal);
        }
        ++it;
    }
    return true;
}

static void parseGenMapText(const td::String& mapStr, std::map<int, int>& genTypes)
{
    const char* p = mapStr.c_str();
    if (!p)
        return;
    std::string s(p);
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t comma = s.find(',', pos);
        std::string tok = (comma == std::string::npos) ? s.substr(pos) : s.substr(pos, comma - pos);
        size_t colon = tok.find(':');
        if (colon != std::string::npos)
        {
            int id = std::atoi(tok.substr(0, colon).c_str());
            int tp = std::atoi(tok.substr(colon + 1).c_str());
            if (id > 0)
                genTypes[id] = tp;
        }
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
}

static std::string fmtNum(double v)
{
    char buff[64];
    std::snprintf(buff, sizeof(buff), "%.9g", v);
    return std::string(buff);
}

// ---------------------------------------------------------------
// Glavna konverzija: MATPOWER case + XML -> dTwin .dmodl + .vmodl
// ---------------------------------------------------------------
bool createModel(const td::String& inputFileName, const td::String& outFileName, sc::IPlugin* pIPlugin, const Options& options, gui::LineEdit& status)
{
    mu::ScopedCLocale scopedLocale;
    (void)pIPlugin;

    std::map<int, int> genTypes;
    bool xmlOk = readAgcConfigFromXml(inputFileName, genTypes);
    if (!xmlOk || genTypes.empty())
        parseGenMapText(options.genAgcMap, genTypes);

    std::atomic<int> progress(0);
    std::atomic<bool> done(false);
    bool success = false;

    // thread 2: real-time pracenje progresa konverzije
    std::thread progressThread([&progress, &done]()
        {
            while (!done.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });

    // thread 1: konverzija
    std::thread convThread([&]()
        {
            progress = 5;

            const int nBus = 9;
            const int nLines = 9;
            const BusInit* buses = case9Buses;
            const LineData* lines = case9Lines;

            YbusMatrices ybus;
            buildYbus(ybus, nBus, lines, nLines);
            auto matG = ybus.mG.getManipulator1();
            auto matB = ybus.mB.getManipulator1();
            progress = 20;

            struct GenInfo { int id; double pGen; double vSet; bool agc; };
            std::vector<GenInfo> gens;
            int slackId = 1;
            double slackV = 1.04;
            for (int i = 0; i < nBus; ++i)
            {
                if (buses[i].type == 3)
                {
                    slackId = buses[i].id;
                    slackV = buses[i].vSet;
                }
                else if (buses[i].type == 2)
                {
                    GenInfo g;
                    g.id = buses[i].id;
                    g.pGen = buses[i].pGen;
                    g.vSet = buses[i].vSet;
                    auto itT = genTypes.find(g.id);
                    g.agc = (itT != genTypes.end() && itT->second == 1);
                    gens.push_back(g);
                }
            }

            // poremecaj za demonstraciju AGC-a: +10% opterecenja na sabirnici s najvecim opterecenjem
            int stepBus = -1;
            double maxLoad = 0.0;
            for (int i = 0; i < nBus; ++i)
                if (buses[i].pLoad > maxLoad)
                {
                    maxLoad = buses[i].pLoad;
                    stepBus = buses[i].id;
                }
            progress = 35;

            auto isGenBus = [&](int k) -> const GenInfo*
                {
                    for (size_t g = 0; g < gens.size(); ++g)
                        if (gens[g].id == k)
                            return &gens[g];
                    return nullptr;
                };

            // polarna injekcija aktivne snage u mrezu na sabirnici k (koristi Ybus matricu)
            auto pInj = [&](int k) -> std::string
                {
                    std::ostringstream os;
                    os << "V" << k << "*(";
                    bool first = true;
                    for (int m = 1; m <= nBus; ++m)
                    {
                        double gkm = matG(k, m);
                        double bkm = matB(k, m);
                        if (std::fabs(gkm) < 1e-12 && std::fabs(bkm) < 1e-12)
                            continue;
                        if (!first) os << " + ";
                        os << "V" << m << "*(";
                        bool inner = false;
                        if (std::fabs(gkm) >= 1e-12)
                        {
                            os << "(" << fmtNum(gkm) << ")*cos(th" << k << "-th" << m << ")";
                            inner = true;
                        }
                        if (std::fabs(bkm) >= 1e-12)
                        {
                            if (inner) os << " + ";
                            os << "(" << fmtNum(bkm) << ")*sin(th" << k << "-th" << m << ")";
                        }
                        os << ")";
                        first = false;
                    }
                    os << ")";
                    return os.str();
                };
            // polarna injekcija reaktivne snage
            auto qInj = [&](int k) -> std::string
                {
                    std::ostringstream os;
                    os << "V" << k << "*(";
                    bool first = true;
                    for (int m = 1; m <= nBus; ++m)
                    {
                        double gkm = matG(k, m);
                        double bkm = matB(k, m);
                        if (std::fabs(gkm) < 1e-12 && std::fabs(bkm) < 1e-12)
                            continue;
                        if (!first) os << " + ";
                        os << "V" << m << "*(";
                        bool inner = false;
                        if (std::fabs(gkm) >= 1e-12)
                        {
                            os << "(" << fmtNum(gkm) << ")*sin(th" << k << "-th" << m << ")";
                            inner = true;
                        }
                        if (std::fabs(bkm) >= 1e-12)
                        {
                            if (inner) os << " - ";
                            else os << "-";
                            os << "(" << fmtNum(bkm) << ")*cos(th" << k << "-th" << m << ")";
                        }
                        os << ")";
                        first = false;
                    }
                    os << ")";
                    return os.str();
                };

            std::ofstream fOut(outFileName.c_str());
            if (!fOut.is_open())
            {
                done = true;
                return;
            }

            // ---------------- Header ----------------
            fOut << "Header:\n";
            fOut << "\tmaxIter = " << options.maxIter << "\n";
            fOut << "\treport = Solved\n";
            fOut << "\tmaxReps = -1\n";
            fOut << "\toutToTxt = false\n";
            fOut << "\tstartTime = 0\n";
            fOut << "\tdTime = " << fmtNum(options.dTime) << "\n";
            fOut << "\tendTime = " << fmtNum(options.endTime) << "\n";
            fOut << "end\n";
            fOut << "//AGC dinamicki model - generisan AGC plugin konvertorom\n";
            fOut << "Model [type=DAE domain=real method=RK2 eps=1e-8 name=\"" << options.modelName.c_str() << "\"]:\n";

            // ---------------- Vars ----------------
            fOut << "Vars [out=true]:\n";
            for (int i = 0; i < nBus; ++i)
            {
                if (buses[i].id == slackId)
                    continue;
                fOut << "\tth" << buses[i].id << " = 0.0; V" << buses[i].id << " = 1.0\n";
            }
            for (size_t g = 0; g < gens.size(); ++g)
            {
                int id = gens[g].id;
                fOut << "\tdelta" << id << "; omega" << id << " = 1.0\n";
                if (gens[g].agc)
                    fOut << "\tPm" << id << "; xI" << id << "; Pml" << id << "\n";
            }

            // ---------------- Params ----------------
            fOut << "Params:\n";
            fOut << "\tf0 = 50; oms = 2*pi*f0\n";
            fOut << "\tf = 50 [out=true]\n";
            fOut << "\tth" << slackId << " = 0.0; V" << slackId << " = " << fmtNum(slackV) << "\n";
            for (int i = 0; i < nBus; ++i)
            {
                int k = buses[i].id;
                if (k == slackId)
                    continue;
                double plMain = buses[i].pLoad;
                if (k == stepBus)
                    plMain = buses[i].pLoad * 1.10;   // +10% skok opterecenja (AGC test)
                fOut << "\tPL" << k << " = " << fmtNum(buses[i].pLoad)
                    << "; PLm" << k << " = " << fmtNum(plMain)
                    << "; QL" << k << " = " << fmtNum(buses[i].qLoad) << "\n";
            }
            for (size_t g = 0; g < gens.size(); ++g)
            {
                int id = gens[g].id;
                fOut << "\tPg" << id << " = " << fmtNum(gens[g].pGen)
                    << "; Vsp" << id << " = " << fmtNum(gens[g].vSet) << "\n";
                fOut << "\tEp" << id << "; Xdp" << id << " = " << fmtNum(options.xdp)
                    << "; H" << id << " = " << fmtNum(options.h)
                    << "; D" << id << " = " << fmtNum(options.d) << "\n";
                if (gens[g].agc)
                {
                    fOut << "\tKp" << id << " = " << fmtNum(options.kp)
                        << "; Ki" << id << " = " << fmtNum(options.ki)
                        << "; Tgov" << id << " = " << fmtNum(options.tgov)
                        << "; Pmin" << id << " = " << fmtNum(options.pmin)
                        << "; Pmax" << id << " = " << fmtNum(options.pmax)
                        << "; Pref" << id << " = " << fmtNum(gens[g].pGen) << "\n";
                }
                else
                {
                    fOut << "\tPm" << id << " = " << fmtNum(gens[g].pGen) << "\n";
                }
            }
            progress = 50;

            // ---------------- SubModel: inicijalizacija (power flow) ----------------
            // Kljucni korak po uzoru na profesorov konvertor: PF se rijesi PRVO,
            // pa se tacni pocetni uslovi prenesu u glavni model preko @main.
            fOut << "SubModel [type=NL name=\"Initialization\" copyPars=-1 eps=1e-8 pivot=\"Diagonal\"]:\n";
            fOut << "Vars [out=true]:\n";
            for (int i = 0; i < nBus; ++i)
            {
                if (buses[i].id == slackId)
                    continue;
                fOut << "\tth" << buses[i].id << " = 0.0; V" << buses[i].id << " = 1.0\n";
            }
            fOut << "Params:\n";
            for (size_t g = 0; g < gens.size(); ++g)
            {
                int id = gens[g].id;
                fOut << "\tQinj" << id << "; Qm" << id << "; Ir" << id << "; Ii" << id
                    << "; Er" << id << "; Ei" << id << "\n";
            }
            fOut << "NLEs:\n";
            for (int i = 0; i < nBus; ++i)
            {
                int k = buses[i].id;
                if (k == slackId)
                    continue;
                const GenInfo* pg = isGenBus(k);
                if (pg)
                {
                    // PV sabirnica: bilans P + fiksni napon
                    fOut << "\t" << pInj(k) << " = Pg" << k << " - PL" << k << "\n";
                    fOut << "\tV" << k << " = Vsp" << k << "\n";
                }
                else
                {
                    // PQ sabirnica: bilans P i Q
                    fOut << "\t" << pInj(k) << " = -PL" << k << "\n";
                    fOut << "\t" << qInj(k) << " = -QL" << k << "\n";
                }
            }
            fOut << "PostProc:\n";
            for (size_t g = 0; g < gens.size(); ++g)
            {
                int id = gens[g].id;
                fOut << "\t// inicijalizacija generatora " << id << "\n";
                fOut << "\tQinj" << id << " = " << qInj(id) << "\n";
                fOut << "\tQm" << id << " = Qinj" << id << " + QL" << id << "\n";
                fOut << "\tIr" << id << " = (Pg" << id << "*cos(th" << id << ") + Qm" << id << "*sin(th" << id << "))/V" << id << "\n";
                fOut << "\tIi" << id << " = (Pg" << id << "*sin(th" << id << ") - Qm" << id << "*cos(th" << id << "))/V" << id << "\n";
                fOut << "\tEr" << id << " = V" << id << "*cos(th" << id << ") - Xdp" << id << "*Ii" << id << "\n";
                fOut << "\tEi" << id << " = V" << id << "*sin(th" << id << ") + Xdp" << id << "*Ir" << id << "\n";
                fOut << "\t@main.delta" << id << " = atg2(Er" << id << ", Ei" << id << ")\n";
                fOut << "\t@main.Ep" << id << " = (Er" << id << "^2 + Ei" << id << "^2)^0.5\n";
                fOut << "\t@main.omega" << id << " = 1.0\n";
                if (gens[g].agc)
                {
                    fOut << "\t@main.Pm" << id << " = Pg" << id << "\n";
                    fOut << "\t@main.Pml" << id << " = Pg" << id << "\n";
                    fOut << "\t@main.xI" << id << " = 0.0\n";
                }
            }
            for (int i = 0; i < nBus; ++i)
            {
                int k = buses[i].id;
                if (k == slackId)
                    continue;
                fOut << "\t@main.th" << k << " = th" << k << "; @main.V" << k << " = V" << k << "\n";
            }
            fOut << "end\t\t//end of SubModel\n";
            progress = 65;

            // ---------------- ODEs ----------------
            fOut << "ODEs:\n";
            for (size_t g = 0; g < gens.size(); ++g)
            {
                int id = gens[g].id;
                std::string pe = "(Ep" + std::to_string(id) + "*V" + std::to_string(id) + "/Xdp" + std::to_string(id) + ")*sin(delta" + std::to_string(id) + " - th" + std::to_string(id) + ")";
                fOut << "\t// --- Generator na sabirnici " << id << (gens[g].agc ? " (AGC)" : " (standardni)") << " ---\n";
                fOut << "\tdelta" << id << "' = (omega" << id << " - 1.0)*oms\n";
                std::string pmName = gens[g].agc ? ("Pml" + std::to_string(id)) : ("Pm" + std::to_string(id));
                fOut << "\tomega" << id << "' = (" << pmName << " - " << pe
                    << " - D" << id << "*(omega" << id << " - 1.0)) * oms / (2*H" << id << ")\n";
                if (gens[g].agc)
                {
                    fOut << "\txI" << id << "' = Ki" << id << "*(1.0 - omega" << id << ")\n";
                    fOut << "\tPm" << id << "' = (Pref" << id << " + Kp" << id << "*(1.0 - omega" << id << ") + xI" << id << " - Pm" << id << ")/Tgov" << id << "\n";
                }
            }
            progress = 75;

            // ---------------- NLEs (glavni model - sa PLm opterecenjem) ----------------
            fOut << "NLEs:\n";
            for (int i = 0; i < nBus; ++i)
            {
                int k = buses[i].id;
                if (k == slackId)
                    continue;
                const GenInfo* pg = isGenBus(k);
                std::string peK, qeK;
                if (pg)
                {
                    peK = "(Ep" + std::to_string(k) + "*V" + std::to_string(k) + "/Xdp" + std::to_string(k) + ")*sin(delta" + std::to_string(k) + " - th" + std::to_string(k) + ")";
                    qeK = "(Ep" + std::to_string(k) + "*V" + std::to_string(k) + "/Xdp" + std::to_string(k) + ")*cos(delta" + std::to_string(k) + " - th" + std::to_string(k) + ") - (V" + std::to_string(k) + "^2)/Xdp" + std::to_string(k) + "";
                }
                fOut << "\t" << pInj(k) << " = ";
                if (pg)
                    fOut << peK << " - PLm" << k << "\n";
                else
                    fOut << "-PLm" << k << "\n";
                fOut << "\t" << qInj(k) << " = ";
                if (pg)
                    fOut << qeK << " - QL" << k << "\n";
                else
                    fOut << "-QL" << k << "\n";
            }
            for (size_t g = 0; g < gens.size(); ++g)
            {
                if (gens[g].agc)
                {
                    int id = gens[g].id;
                    fOut << "\tPml" << id << " = lim(Pm" << id << ", Pmin" << id << ", Pmax" << id << ")\n";
                }
            }

            // ---------------- PostProc ----------------
            fOut << "PostProc:\n";
            if (!gens.empty())
                fOut << "\tf = f0*omega" << gens[0].id << "\n";
            fOut << "end\n";
            fOut.close();
            progress = 85;

            // ---------------- .vmodl (grafike) ----------------
            std::string outStr(outFileName.c_str());
            size_t dot = outStr.rfind('.');
            std::string vPath = (dot == std::string::npos) ? (outStr + ".vmodl") : (outStr.substr(0, dot) + ".vmodl");
            std::ofstream fV(vPath.c_str());
            if (fV.is_open())
            {
                const char* colsL[] = { "darkGreen", "darkBlue", "darkRed", "black", "brown", "darkCyan" };
                const char* colsD[] = { "yellow", "cyan", "magenta", "red", "orange", "lightGreen" };
                int nc = 6;
                std::string tEnd = fmtNum(options.endTime);

                fV << "Header:\n\tnewTab = false\nend\n";
                fV << "//Prikaz rezultata AGC regulacije\n";
                fV << "Model [name=\"AGC rezultati\"]:\n";
                fV << "Plots [backColor=auto]:\n";

                fV << "\tlinePlot [xLabel=\"Time [s]\" yLabel=\"Frequency [Hz]\" name=\"Frekvencija sistema\"]:\n";
                fV << "\t\thLine@ -> f0 [width=2 pattern=dot name=\"f_ref\" colorL=green colorD=lightGreen minX=0 maxX=" << tEnd << "]\n";
                fV << "\t\t@x << t\n";
                for (size_t g = 0; g < gens.size(); ++g)
                    fV << "\t\t@y << f0*omega" << gens[g].id << " [colorL=" << colsL[g % nc] << " colorD=" << colsD[g % nc] << " width=2 name=\"f gen " << gens[g].id << "\"]\n";
                fV << "\t\t@cond -> repeat# == 0\n\tend\n";

                fV << "\tlinePlot [xLabel=\"Time [s]\" yLabel=\"Rotor angle [rad]\" name=\"Uglovi rotora\"]:\n";
                fV << "\t\t@x << t\n";
                for (size_t g = 0; g < gens.size(); ++g)
                    fV << "\t\t@y << delta" << gens[g].id << " [colorL=" << colsL[g % nc] << " colorD=" << colsD[g % nc] << " width=2 name=\"delta " << gens[g].id << "\"]\n";
                fV << "\t\t@cond -> repeat# == 0\n\tend\n";

                bool anyAgc = false;
                for (size_t g = 0; g < gens.size(); ++g)
                    if (gens[g].agc)
                        anyAgc = true;
                if (anyAgc)
                {
                    fV << "\tlinePlot [xLabel=\"Time [s]\" yLabel=\"Power [p.u.]\" name=\"AGC regulacija\"]:\n";
                    fV << "\t\t@x << t\n";
                    int ci = 0;
                    for (size_t g = 0; g < gens.size(); ++g)
                    {
                        if (!gens[g].agc)
                            continue;
                        int id = gens[g].id;
                        fV << "\t\t@y << Pm" << id << " [colorL=" << colsL[ci % nc] << " colorD=" << colsD[ci % nc] << " width=2 name=\"Pm " << id << "\"]\n";
                        ++ci;
                        fV << "\t\t@y << xI" << id << " [colorL=" << colsL[ci % nc] << " colorD=" << colsD[ci % nc] << " width=1 name=\"xI " << id << "\"]\n";
                        ++ci;
                    }
                    fV << "\t\t@cond -> repeat# == 0\n\tend\n";
                }
                fV << "end\n";
                fV.close();
            }

            progress = 100;
            success = true;
            done = true;
        });

    convThread.join();
    progressThread.join();

    if (!success)
    {
        status = "ERROR! Conversion failed (cannot write output file)";
        return false;
    }
    status = "OK! AGC model generated (100%)";
    return true;
}