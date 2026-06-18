#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>

#undef Q_OS_WIN

struct OxygenSensor {
    int bank;
    int sensor;
    double voltage;
    double shortTermTrim;
};

struct DTCCode {
    std::string code;
    std::string description;
};

struct FuelTrim {
    double shortTermBank1;
    double shortTermBank2;
    double longTermBank1;
    double longTermBank2;
    bool available;
};

class SessionLogger;

class ELM327
{
public:
    ELM327(const std::string& mac, int channel = 1);
    ~ELM327();

    bool connectBT();
    void disconnect();
    bool isConnected() { return sock >= 0; }
    bool isOnline() { return sock >= 0; }

    std::string send(const std::string& cmd, int delayMs = 200);
    std::string sendRaw(const std::string& cmd, int timeoutMs = 500);

    int getRPM();
    int getSpeed();
    int getTemp();
    int getCoolantTemp();
    int getEngineLoad();
    double getThrottlePosition();
    double getIntakePressure();
    int getIntakeTemp();
    double getTimingAdvance();
    double getFuelPressure();
    double getMAF();
    double getFuelLevel();
    double getAmbientTemp();
    double getOilTemp();
    double getCommandedEGR();
    double getEGRError();
    double getEVAPPressure();
    double getBarometricPressure();

    double getShortTermTrimBank1();
    double getShortTermTrimBank2();
    double getLongTermTrimBank1();
    double getLongTermTrimBank2();
    FuelTrim getAllFuelTrims();

    std::vector<OxygenSensor> getOxygenSensors();
    OxygenSensor getO2Sensor(int bank, int sensor);

    std::vector<DTCCode> getDTCs();
    bool clearDTCs();

    std::string getProtocol();
    std::string getVehicleInfo();
    std::string getVIN();
    bool checkMIL();
    bool setProtocol(int protocol);
    bool resetELM();
    std::string sendCommand(const std::string& cmd);
    void setSessionLogger(SessionLogger* logger) { m_sessionLog = logger; }

    bool recoverFromStopped();
    void ensureNormalConfig();
    static void fullCleanup();

    void logAllSensorsRaw(const std::string& filename);
    void logP0171Diagnostic(const std::string& filename, int durationSec = 60);

    int getCatalystTempB1S1();
    double getControlModuleVoltage();
    double getEngineTorqueStandard();
    int getOdometer();
    std::vector<DTCCode> getPendingDTCs();
    std::vector<DTCCode> getPermanentDTCs();
    bool clearPendingDTCs();

    struct DashboardData {
        int rpm;
        int speed;
        int coolant;
        int load;
        double throttle;
        double maf;
        double fuelLevel;
        double timing;
        double intakeTemp;
        int intakePressure;
        bool valid;
    };
    DashboardData getDashboardFast();

    std::string getFreezeFrame();
    std::string getMonitorTests();
    std::string getTestResults();
    bool requestO2Test(int bank, int sensor);
    std::string getECUName();
    std::string getCalibrationID();
    std::string getCalibrationDate();
    std::string getPerformanceTracking();

    struct ModuleScanResult {
        int id;
        std::string name;
        bool responds;
        std::vector<DTCCode> dtcs;
    };
    std::vector<ModuleScanResult> autoScan();

    std::vector<std::string> splitResponse(const std::string& response);
    int getSock() const { return sock; }
    int getStoppedPenaltyMs() const { return m_stoppedPenaltyMs; }
    int getStoppedCount() const { return m_stoppedCount; }

private:
    int sock;
    std::string mac;
    int channel;
    bool m_stoppedRecovery;
    int m_stoppedPenaltyMs;
    int m_consecutiveSuccess;
    int m_stoppedCount;
    SessionLogger* m_sessionLog;
    int m_protocol;
    bool m_configValid;

    std::string readRaw();
    std::string parseResponse(const std::string& response, const std::string& expected);
    std::string decodeDTCCode(const std::string& code);
    void detectAndCacheProtocol();
    void loadCachedProtocol();
    void saveCachedProtocol(int protocol);
    static std::string getCacheFilePath();
};
