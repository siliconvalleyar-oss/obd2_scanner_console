#include "logger.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

Logger::~Logger() {
    if (file && file->is_open()) file->close();
}

std::string Logger::timestamp() {
    time_t now = time(nullptr);
    tm* t = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    return buf;
}

bool Logger::ensureLogsDir() {
    struct stat st;
    if (stat("logs", &st) != 0) {
#ifdef _WIN32
        if (_mkdir("logs") != 0) {
#else
        if (mkdir("logs", 0755) != 0) {
#endif
            std::cerr << "[monitor] No se pudo crear directorio logs/" << std::endl;
            return false;
        }
        std::cout << "[monitor] Directorio logs/ creado" << std::endl;
    }
    return true;
}

bool Logger::open() {
    ensureLogsDir();
    time_t now = time(nullptr);
    tm* t = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", t);
    std::string filename = "logs/sensors_" + std::string(ts) + ".csv";
    file = std::make_unique<std::ofstream>(filename);
    if (!file->is_open()) {
        std::cerr << "[monitor] No se pudo crear " << filename << std::endl;
        return false;
    }
    *file << "timestamp,key,value\n";
    std::cout << "[monitor] " << filename << std::endl;
    return true;
}

void Logger::log(const std::string& key, int value) {
    if (file && file->is_open()) {
        *file << timestamp() << "," << key << "," << value << "\n";
        file->flush();
    }
}

void Logger::log(const std::string& key, double value) {
    if (file && file->is_open()) {
        *file << timestamp() << "," << key << "," << std::fixed << std::setprecision(2) << value << "\n";
        file->flush();
    }
}

void Logger::log(const std::string& key, const std::string& value) {
    if (file && file->is_open()) {
        *file << timestamp() << "," << key << "," << value << "\n";
        file->flush();
    }
}

SessionLogger::~SessionLogger() {
    if (file && file->is_open()) file->close();
}

bool SessionLogger::open() {
    Logger::ensureLogsDir();
    time_t now = time(nullptr);
    tm* t = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", t);
    std::string filename = "logs/session_" + std::string(ts) + ".log";
    file = std::make_unique<std::ofstream>(filename);
    if (!file->is_open()) {
        std::cerr << "[monitor] No se pudo crear session log: " << filename << std::endl;
        return false;
    }
    *file << timestamp() << "|START|Sesión iniciada - OBD2 Console||||\n";
    file->flush();
    std::cout << "[monitor] " << filename << std::endl;
    return true;
}

std::string SessionLogger::timestamp() {
    time_t now = time(nullptr);
    tm* t = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    return buf;
}

void SessionLogger::logCommand(const std::string& cmd, const std::string& response) {
    if (file && file->is_open()) {
        *file << timestamp() << "|TX|" << cmd << "|RX|" << response << "\n";
        file->flush();
    }
}

void SessionLogger::logError(const std::string& context, const std::string& detail) {
    if (file && file->is_open()) {
        *file << timestamp() << "|ERROR|" << context << "|" << detail << "\n";
        file->flush();
    }
}

void SessionLogger::logInfo(const std::string& msg) {
    if (file && file->is_open()) {
        *file << timestamp() << "|INFO|" << msg << "\n";
        file->flush();
    }
}

void SessionLogger::logOption(int option, const std::string& title) {
    if (file && file->is_open()) {
        *file << timestamp() << "|OPTION|" << option << "|" << title << "\n";
        file->flush();
    }
}

void SessionLogger::logResult(const std::string& key, const std::string& value) {
    if (file && file->is_open()) {
        *file << timestamp() << "|RESULT|" << key << "|" << value << "\n";
        file->flush();
    }
}
