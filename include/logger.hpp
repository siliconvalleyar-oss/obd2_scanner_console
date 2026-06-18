#pragma once

#include <string>
#include <fstream>
#include <memory>
#include <ctime>

class Logger {
public:
    Logger() = default;
    ~Logger();
    bool open();
    void log(const std::string& key, int value);
    void log(const std::string& key, double value);
    void log(const std::string& key, const std::string& value);
    static bool ensureLogsDir();
private:
    std::unique_ptr<std::ofstream> file;
    static std::string timestamp();
};

class SessionLogger {
public:
    SessionLogger() = default;
    ~SessionLogger();
    bool open();
    void logCommand(const std::string& cmd, const std::string& response);
    void logError(const std::string& context, const std::string& detail);
    void logInfo(const std::string& msg);
    void logOption(int option, const std::string& title);
    void logResult(const std::string& key, const std::string& value);
private:
    std::unique_ptr<std::ofstream> file;
    std::string timestamp();
};
