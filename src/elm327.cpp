/**
 * @file elm327.cpp
 * @brief Implementación de la clase ELM327 - comunicación Bluetooth con adaptador OBD-II.
 *
 * Gestiona la conexión Bluetooth RFCOMM, envío/recepción de comandos
 * AT y OBD-II, parseo de respuestas hexadecimales, y recuperación
 * de errores (STOPPED, timeout adaptativo).
 */

#include "elm327.hpp"

#include <fstream>
#include <functional>
#include <ctime>
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <locale>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <sys/stat.h>

#include "logger.hpp"

// ============================================================================
// Constructor / Destructor
// ============================================================================

ELM327::ELM327(const std::string& macAddr, int ch)
    : sock(-1),
      mac(macAddr), channel(ch), m_stoppedRecovery(false),
      m_stoppedPenaltyMs(0), m_consecutiveSuccess(0), m_stoppedCount(0),
      m_sessionLog(nullptr), m_protocol(-1), m_configValid(false) {}

ELM327::~ELM327()
{
    disconnect();
}

bool ELM327::connectBT()
{
    fullCleanup();

    // Conexión Linux: Bluetooth RFCOMM socket
    struct sockaddr_rc addr{};

    sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (sock < 0)
    {
        perror("[ERROR] socket");
        return false;
    }

    addr.rc_family = AF_BLUETOOTH;
    addr.rc_channel = (uint8_t)channel;
    str2ba(mac.c_str(), &addr.rc_bdaddr);

    std::cout << "[monitor] Conectando a " << mac << "...\n";

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("[ERROR] connect");
        return false;
    }

    std::cout << "[monitor] Conectado!\n";

    // Inicializar ELM327
    send("ATZ", 2000);
    send("ATE0"); // echo off
    send("ATL0"); // linefeed off
    send("ATS0"); // spaces off
    send("ATH0"); // headers off (crítico: CAN headers corrompen splitResponse)
    // Flush residual buffer after ATZ
    for (int flush = 0; flush < 3; flush++) {
        send("\r", 100);
    }
    // Usar protocolo cacheado si está disponible (evita ATSP0 ~2-3s)
    loadCachedProtocol();
    if (m_protocol > 0) {
        send("ATSP" + std::to_string(m_protocol));
        std::cout << "[CACHE] Usando protocolo cacheado: ATSP" << m_protocol << std::endl;
    } else {
        send("ATSP0"); // Auto-detección primera vez
    }

    // Configurar para respuestas más rápidas
    send("ATAT1"); // adaptor timeout 1
    send("ATST10"); // timeout 40ms
    send("ATAL1");  // Allow long CAN messages (multi-frame)
    if (m_protocol >= 6 && m_protocol <= 9) {
        send("ATCF 7E8", 50); // CAN filter: solo ECU (0x7E8)
        send("ATCM 7FF", 50); // CAN mask: coincidencia exacta 11 bits
    }

    // Detectar y cachear protocolo para futuras conexiones rápidas
    if (m_protocol <= 0) {
        detectAndCacheProtocol();
    }

    m_configValid = true;
    return true;
}

void ELM327::disconnect()
{
    if (sock >= 0)
    {
        std::cout << "[INFO] Cerrando conexión\n";
        close(sock);
        sock = -1;
    }
}

std::string ELM327::readRaw()
{
    if (!isConnected()) return "";

    char buffer[4096];
    int n = read(sock, buffer, sizeof(buffer)-1);
    if (n > 0)
    {
        buffer[n] = 0;
        std::string result(buffer);
        result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
        result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
        result.erase(std::remove(result.begin(), result.end(), ' '), result.end());
        return result;
    }

    return "";
}

// sendRaw - Envío con select() timeout + detección STOPPED + penalidad adaptativa
std::string ELM327::sendRaw(const std::string& cmd, int timeoutMs) {
    if (!isConnected()) return "";

    std::string full = cmd + "\r";
    std::cout << "[TX RAW] " << cmd << std::endl;

    ::write(sock, full.c_str(), full.size());

    int effectiveTimeout = timeoutMs + m_stoppedPenaltyMs;
    char buffer[1024];
    std::string response;
    fd_set fds;
    struct timeval tv;

    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    tv.tv_sec = effectiveTimeout / 1000;
    tv.tv_usec = (effectiveTimeout % 1000) * 1000;

    while (select(sock + 1, &fds, NULL, NULL, &tv) > 0) {
        int n = ::recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        buffer[n] = '\0';
        response += buffer;
        if (response.find(">") != std::string::npos) break;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 300000;
    }

    response.erase(std::remove(response.begin(), response.end(), '\r'), response.end());
    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());

    if (!response.empty()) {
        std::cout << "[RX RAW] " << response.substr(0, 120) << std::endl;
    }

    // Detectar STOPPED y recuperar (con penalidad adaptativa)
    if (!m_stoppedRecovery && response.find("STOPPED") != std::string::npos) {
        std::cout << "[WARN] sendRaw detectó STOPPED! Recuperando...\n";
        m_stoppedRecovery = true;
        m_stoppedPenaltyMs = std::min(m_stoppedPenaltyMs + 100, 1000);
        m_consecutiveSuccess = 0;
        m_stoppedCount++;
        std::cout << "[ADAPT] Penalidad +" << m_stoppedPenaltyMs << "ms (total: " << m_stoppedCount << ")\n";
        recoverFromStopped();
        m_stoppedRecovery = false;
        std::cout << "[RETRY] Reintentando " << cmd << "...\n";
        return sendRaw(cmd, timeoutMs);
    }

    // Decay de penalidad (solo si no estamos en recovery)
    if (!m_stoppedRecovery && m_stoppedPenaltyMs > 0) {
        m_consecutiveSuccess++;
        if (m_consecutiveSuccess >= 10) {
            m_stoppedPenaltyMs = std::max(0, m_stoppedPenaltyMs - 25);
            m_consecutiveSuccess = 0;
            if (m_stoppedPenaltyMs > 0) {
                std::cout << "[ADAPT] Penalidad reducida a +" << m_stoppedPenaltyMs << "ms\n";
            } else {
                std::cout << "[ADAPT] Penalidad restablecida a 0ms\n";
            }
        }
    }

    return response;
}

// ================= UTILIDADES =================

std::string ELM327::send(const std::string& cmd, int delayMs)
{
    if (!isConnected()) return "";

    std::string full = cmd + "\r";

    std::cout << "[TX] " << cmd << std::endl;

    ::write(sock, full.c_str(), full.size());

    char buffer[4096];
    std::string response;
    fd_set fds;
    struct timeval tv;
    int ret;

    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    int timeoutMs = (delayMs > 0) ? delayMs : 200;
    int effectiveTimeout = timeoutMs + m_stoppedPenaltyMs;
    tv.tv_sec = effectiveTimeout / 1000;
    tv.tv_usec = (effectiveTimeout % 1000) * 1000;

    if (m_stoppedPenaltyMs > 0 && cmd.substr(0, 2) != "AT") {
        std::cout << "[ADAPT] +" << m_stoppedPenaltyMs << "ms (" << m_stoppedCount << " STOPPED)\n";
    }

    while (true) {
        ret = select(sock + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) break;

        int n = ::recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        buffer[n] = '\0';
        response += buffer;

        if (response.find('>') != std::string::npos) break;

        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 300000;
    }

    // Limpiar caracteres de control para el log
    std::string logResp = response;
    logResp.erase(std::remove(logResp.begin(), logResp.end(), '\r'), logResp.end());
    logResp.erase(std::remove(logResp.begin(), logResp.end(), '\n'), logResp.end());
    if (!logResp.empty()) {
        std::cout << "[RX] " << logResp.substr(0, 100) << std::endl;
    }

    // Session log
    if (m_sessionLog && !cmd.empty()) {
        std::string hexForLog = logResp;
        size_t p = hexForLog.find('>');
        if (p != std::string::npos) hexForLog = hexForLog.substr(0, p);
        if (cmd.substr(0, 2) != "AT" && cmd != "\r") {
            m_sessionLog->logCommand(cmd, hexForLog);
        }
    }

    // Detectar estado STOPPED y recuperar automáticamente
    if (!m_stoppedRecovery && logResp.find("STOPPED") != std::string::npos) {
        std::cout << "[WARN] ELM327 en estado STOPPED! Recuperando...\n";
        m_stoppedRecovery = true;

        m_stoppedPenaltyMs = std::min(m_stoppedPenaltyMs + 100, 1000);
        m_consecutiveSuccess = 0;
        m_stoppedCount++;
        std::cout << "[ADAPT] Penalidad +" << m_stoppedPenaltyMs << "ms (total STOPPED: " << m_stoppedCount << ")\n";

        recoverFromStopped();
        m_stoppedRecovery = false;
        std::cout << "[RETRY] Reintentando " << cmd << " con penalidad " << m_stoppedPenaltyMs << "ms...\n";
        return send(cmd, delayMs);
    }

    // Reducción gradual de penalidad tras comandos exitosos
    if (!m_stoppedRecovery && m_stoppedPenaltyMs > 0) {
        m_consecutiveSuccess++;
        if (m_consecutiveSuccess >= 10) {
            m_stoppedPenaltyMs = std::max(0, m_stoppedPenaltyMs - 25);
            m_consecutiveSuccess = 0;
            if (m_stoppedPenaltyMs > 0) {
                std::cout << "[ADAPT] Penalidad reducida a +" << m_stoppedPenaltyMs << "ms\n";
            } else {
                std::cout << "[ADAPT] Penalidad restablecida a 0ms\n";
            }
        }
    }

    return response;
}

std::vector<std::string> ELM327::splitResponse(const std::string& response) {
    std::vector<std::string> results;

    std::string clean = response;
    clean.erase(std::remove(clean.begin(), clean.end(), '\r'), clean.end());
    clean.erase(std::remove(clean.begin(), clean.end(), '\n'), clean.end());

    size_t promptPos = clean.find('>');
    if (promptPos != std::string::npos) {
        clean = clean.substr(0, promptPos);
    }

    std::transform(clean.begin(), clean.end(), clean.begin(), ::toupper);

    size_t startPos = std::string::npos;

    const char* prefixes[] = {"41", "43", "44", "47", "49", "4A", "62", "7F", "61"};
    for (const char* prefix : prefixes) {
        size_t pos = clean.find(prefix);
        if (pos != std::string::npos) {
            startPos = pos;
            break;
        }
    }

    if (startPos == std::string::npos) {
        size_t okPos = clean.find("OK");
        if (okPos != std::string::npos && (okPos == 0 || !isxdigit(clean[okPos-1]))) {
            startPos = okPos;
        }
    }
    if (startPos == std::string::npos) {
        size_t ndPos = clean.find("NODATA");
        if (ndPos != std::string::npos) startPos = ndPos;
    }
    if (startPos == std::string::npos) {
        size_t sePos = clean.find("SEARCHING");
        if (sePos != std::string::npos) startPos = sePos;
    }

    if (startPos == std::string::npos) {
        startPos = clean.find_first_of("0123456789ABCDEF");
        if (startPos == std::string::npos) return results;
    }

    std::string data = clean.substr(startPos);

    if (data.substr(0, 2) == "OK" || data.substr(0, 6) == "NODATA") {
        results.push_back(data);
        return results;
    }

    if (data.substr(0, 9) == "SEARCHING") {
        data = data.substr(9);
    }

    for (size_t i = 0; i + 1 < data.length(); i += 2) {
        if (i + 2 > data.length()) break;
        std::string byte = data.substr(i, 2);
        if (isxdigit(byte[0]) && isxdigit(byte[1])) {
            results.push_back(byte);
        } else {
            break;
        }
    }

    return results;
}

// ============================================================================
// Parámetros del motor — PIDs OBD-II Modo 01
// ============================================================================

int ELM327::getRPM()
{
    std::string r = send("010C");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 4) {
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            return (a * 256 + b) / 4;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

int ELM327::getSpeed()
{
    std::string r = send("010D");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            return std::stoi(bytes[2], nullptr, 16);
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

int ELM327::getCoolantTemp()
{
    std::string r = send("0105");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            int temp = std::stoi(bytes[2], nullptr, 16);
            return temp - 40;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

int ELM327::getTemp()
{
    return getCoolantTemp();
}

int ELM327::getEngineLoad()
{
    std::string r = send("0104");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            int load = std::stoi(bytes[2], nullptr, 16);
            return (load * 100) / 255;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getThrottlePosition()
{
    std::string r = send("0111");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            int pos = std::stoi(bytes[2], nullptr, 16);
            return (pos * 100.0) / 255.0;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getIntakePressure()
{
    std::string r = send("010B");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            return std::stoi(bytes[2], nullptr, 16);
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

int ELM327::getIntakeTemp()
{
    std::string r = send("010F");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            int temp = std::stoi(bytes[2], nullptr, 16);
            return temp - 40;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getTimingAdvance()
{
    std::string r = send("010E");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            int advance = std::stoi(bytes[2], nullptr, 16);
            return advance / 2.0 - 64;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getFuelPressure()
{
    std::string r = send("010A");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            int pressure = std::stoi(bytes[2], nullptr, 16);
            return pressure * 3;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getMAF()
{
    std::string r = send("0110");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 4) {
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            return (a * 256 + b) / 100.0;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getFuelLevel()
{
    std::string r = send("012F");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            int level = std::stoi(bytes[2], nullptr, 16);
            return (level * 100.0) / 255.0;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getAmbientTemp()
{
    std::string r = send("0146");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            int temp = std::stoi(bytes[2], nullptr, 16);
            return temp - 40;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getOilTemp()
{
    std::string r = send("015C");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            int temp = std::stoi(bytes[2], nullptr, 16);
            return temp - 40;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getCommandedEGR()
{
    std::string r = send("012C");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            int egr = std::stoi(bytes[2], nullptr, 16);
            return (egr * 100.0) / 255.0;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getEGRError()
{
    std::string r = send("012D");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            int error = std::stoi(bytes[2], nullptr, 16);
            return (error * 100.0) / 128.0 - 100;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getEVAPPressure()
{
    std::string r = send("0132");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 4) {
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            return (a * 256 + b) / 4.0;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

double ELM327::getBarometricPressure()
{
    std::string r = send("0133");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            return std::stoi(bytes[2], nullptr, 16);
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

// ============================================================================
// Sensores de oxígeno — PIDs 0114-0117
// ============================================================================

std::vector<OxygenSensor> ELM327::getOxygenSensors()
{
    std::vector<OxygenSensor> sensors;

    std::string r1 = send("0114", 400);
    auto bytes1 = splitResponse(r1);

    if (bytes1.size() >= 4 && bytes1[0] == "41" && bytes1[1] == "14") {
        for (size_t i = 2; i + 1 < bytes1.size() && i < 10; i += 2) {
            try {
                int voltageByte = std::stoi(bytes1[i], nullptr, 16);
                int trimByte = std::stoi(bytes1[i+1], nullptr, 16);
                if (voltageByte == 0xFF || voltageByte == 0x00 ||
                    trimByte == 0xFF || trimByte == 0x00) continue;
                OxygenSensor sensor;
                sensor.bank = 1;
                sensor.sensor = static_cast<int>((i - 2) / 2 + 1);
                sensor.voltage = voltageByte / 200.0;
                sensor.shortTermTrim = (trimByte * 100.0 / 128.0) - 100;
                sensors.push_back(sensor);
            } catch (...) {}
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::string r2 = send("0115", 400);
    auto bytes2 = splitResponse(r2);

    if (bytes2.size() >= 4 && bytes2[0] == "41" && bytes2[1] == "15") {
        for (size_t i = 2; i + 1 < bytes2.size() && i < 10; i += 2) {
            try {
                int voltageByte = std::stoi(bytes2[i], nullptr, 16);
                int trimByte = std::stoi(bytes2[i+1], nullptr, 16);
                if (voltageByte == 0xFF || voltageByte == 0x00 ||
                    trimByte == 0xFF || trimByte == 0x00) continue;
                OxygenSensor sensor;
                sensor.bank = 2;
                sensor.sensor = static_cast<int>((i - 2) / 2 + 1);
                sensor.voltage = voltageByte / 200.0;
                sensor.shortTermTrim = (trimByte * 100.0 / 128.0) - 100;
                sensors.push_back(sensor);
            } catch (...) {}
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::string r3 = send("0116", 400);
    auto bytes3 = splitResponse(r3);

    if (bytes3.size() >= 4 && bytes3[0] == "41" && bytes3[1] == "16") {
        for (size_t i = 2; i + 1 < bytes3.size() && i < 10; i += 2) {
            try {
                int voltageByte = std::stoi(bytes3[i], nullptr, 16);
                int trimByte = std::stoi(bytes3[i+1], nullptr, 16);
                if (voltageByte == 0xFF || voltageByte == 0x00 ||
                    trimByte == 0xFF || trimByte == 0x00) continue;
                OxygenSensor sensor;
                sensor.bank = 1;
                sensor.sensor = static_cast<int>((i - 2) / 2 + 5);
                sensor.voltage = voltageByte / 200.0;
                sensor.shortTermTrim = (trimByte * 100.0 / 128.0) - 100;
                sensors.push_back(sensor);
            } catch (...) {}
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::string r4 = send("0117", 400);
    auto bytes4 = splitResponse(r4);

    if (bytes4.size() >= 4 && bytes4[0] == "41" && bytes4[1] == "17") {
        for (size_t i = 2; i + 1 < bytes4.size() && i < 10; i += 2) {
            try {
                int voltageByte = std::stoi(bytes4[i], nullptr, 16);
                int trimByte = std::stoi(bytes4[i+1], nullptr, 16);
                if (voltageByte == 0xFF || voltageByte == 0x00 ||
                    trimByte == 0xFF || trimByte == 0x00) continue;
                OxygenSensor sensor;
                sensor.bank = 2;
                sensor.sensor = static_cast<int>((i - 2) / 2 + 5);
                sensor.voltage = voltageByte / 200.0;
                sensor.shortTermTrim = (trimByte * 100.0 / 128.0) - 100;
                sensors.push_back(sensor);
            } catch (...) {}
        }
    }

    if (sensors.empty()) {
        std::cout << "[INFO] No se detectaron sensores de oxígeno" << std::endl;
    }

    return sensors;
}

// ============================================================================
// DTCs — Códigos de error (Modo 03)
// ============================================================================

std::string ELM327::decodeDTCCode(const std::string& code) {
    if (code.length() < 4) return "Código inválido";

    static const std::map<std::string, std::string> dtcTypes = {
        {"P0", "Powertrain - Genérico"},
        {"P1", "Powertrain - Fabricante"},
        {"P2", "Powertrain - Genérico (P2xxx)"},
        {"P3", "Powertrain - Genérico (P3xxx)"},
        {"C0", "Chasis - Genérico"},
        {"C1", "Chasis - Fabricante"},
        {"C2", "Chasis - Genérico (C2xxx)"},
        {"C3", "Chasis - Genérico (C3xxx)"},
        {"B0", "Carrocería - Genérico"},
        {"B1", "Carrocería - Fabricante"},
        {"B2", "Carrocería - Genérico (B2xxx)"},
        {"B3", "Carrocería - Genérico (B3xxx)"},
        {"U0", "Red - Genérico"},
        {"U1", "Red - Fabricante"},
        {"U2", "Red - Genérico (U2xxx)"},
        {"U3", "Red - Genérico (U3xxx)"}
    };

    std::string prefix = code.substr(0, 2);
    auto it = dtcTypes.find(prefix);
    if (it != dtcTypes.end()) {
        return code + " - " + it->second;
    }

    return code;
}

std::vector<DTCCode> ELM327::getDTCs()
{
    std::vector<DTCCode> dtcs;

    std::string r = send("03", 500);

    if (r.find("NO DATA") != std::string::npos) {
        DTCCode noDTC;
        noDTC.code = "NONE";
        noDTC.description = "No hay códigos de error almacenados";
        dtcs.push_back(noDTC);
        return dtcs;
    }

    auto bytes = splitResponse(r);

    if (bytes.size() >= 2 && bytes[0] == "43") {
        int numCodes = 0;
        try {
            numCodes = std::stoi(bytes[1], nullptr, 16);
        } catch (...) {}

        int expectedBytes = 2 + (numCodes * 2);
        if (expectedBytes > (int)bytes.size()) expectedBytes = bytes.size();

        for (size_t i = 2; i + 1 < (size_t)expectedBytes; i += 2) {
            std::string code = bytes[i] + bytes[i + 1];
            if (code != "0000" && code != "FFFF") {
                DTCCode dtc;
                dtc.code = code;
                dtc.description = decodeDTCCode(code);
                dtcs.push_back(dtc);
            }
        }
    }

    if (dtcs.empty()) {
        DTCCode noDTC;
        noDTC.code = "NONE";
        noDTC.description = "No hay códigos de error almacenados";
        dtcs.push_back(noDTC);
    }

    return dtcs;
}

bool ELM327::clearDTCs()
{
    std::cout << "[INFO] Preparando ELM327 para limpiar DTCs..." << std::endl;

    send("ATZ", 2000);
    send("ATE0", 300);
    send("ATL0", 200);
    send("ATS0", 200);
    send("ATH0", 200);
    if (m_protocol > 0) {
        send("ATSP" + std::to_string(m_protocol), 500);
    } else {
        send("ATSP0", 500);
    }
    send("ATAT1", 100);
    send("ATST10", 100);
    send("ATD", 300);

    std::cout << "[INFO] Limpiando códigos de error..." << std::endl;

    for (int i = 0; i < 3; i++) {
        std::string r = send("04", 1500);

        if (r.find("44") != std::string::npos) {
            std::cout << "[OK] Códigos de error borrados correctamente" << std::endl;
            ensureNormalConfig();
            return true;
        }

        if (r.find("OK") != std::string::npos) {
            std::cout << "[OK] Comando aceptado" << std::endl;
            ensureNormalConfig();
            return true;
        }

        if (r.find("BUS INIT") != std::string::npos || r.find("ERROR") != std::string::npos) {
            std::cout << "[WARN] Error de bus, reintentando..." << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "[ERROR] No se pudieron borrar los códigos" << std::endl;
    ensureNormalConfig();
    return false;
}

// ============================================================================
// Diagnóstico — Protocolo, VIN, MIL, Info del vehículo
// ============================================================================

std::string ELM327::getProtocol()
{
    std::string r = send("ATDP", 100);
    r.erase(std::remove(r.begin(), r.end(), '\r'), r.end());
    r.erase(std::remove(r.begin(), r.end(), '\n'), r.end());
    return r;
}

std::string ELM327::getVehicleInfo()
{
    std::string info = "=== INFORMACIÓN DEL VEHÍCULO ===\n";

    info += "Protocolo: " + getProtocol() + "\n";

    try {
        std::string vin = getVIN();
        if (!vin.empty() && vin != "No disponible" && vin != "ERROR") {
            info += "VIN: " + vin + "\n";
        } else {
            info += "VIN: No disponible\n";
        }
    } catch (const std::exception& e) {
        info += "VIN: Error al leer (" + std::string(e.what()) + ")\n";
    }

    try {
        std::string ecu = getECUName();
        if (!ecu.empty() && ecu != "No disponible") {
            info += "ECU: " + ecu + "\n";
        }
    } catch (...) {}

    try {
        std::string calID = getCalibrationID();
        if (!calID.empty() && calID != "No disponible") {
            info += "CVN: " + calID + "\n";
        }
    } catch (...) {}

    try {
        std::string calDate = getCalibrationDate();
        if (!calDate.empty() && calDate != "No disponible") {
            info += "Fecha Calibracion: " + calDate + "\n";
        }
    } catch (...) {}

    try {
        if (checkMIL()) {
            info += "MIL activa - Revisar códigos de error\n";
        } else {
            info += "MIL inactiva\n";
        }
    } catch (const std::exception& e) {
        info += "MIL: Error al consultar\n";
    }

    return info;
}

std::vector<ELM327::ModuleScanResult> ELM327::autoScan() {
    std::vector<ModuleScanResult> results;

    const int moduleIds[] = {0x7E0, 0x7E1, 0x7E2, 0x7E3, 0x7E4, 0x7E5, 0x7E6, 0x7E7, 0x7C0};
    const char* moduleNames[] = {"ECU Motor", "TCM", "ABS", "Suspensión", "Airbag", "Cluster", "BCM", "TCM2", "Infotainment"};
    const int numModules = 9;

    ensureNormalConfig();
    send("ATCM 000", 50);

    for (int i = 0; i < numModules; i++) {
        int id = moduleIds[i];
        std::stringstream ss;
        ss << std::hex << id;
        std::string idStr = ss.str();

        ModuleScanResult mod;
        mod.id = id;
        mod.name = moduleNames[i];
        mod.responds = false;

        std::cout << "  Escaneando " << mod.name << " (0x" << idStr << ")... " << std::flush;

        send("AT SH " + idStr, 30);
        send("AT CRA " + std::to_string(id + 8), 30);
        usleep(80000);

        std::string r = send("0100", 200);

        if (r.find("41") != std::string::npos) {
            mod.responds = true;
            std::cout << "RESPONDE\n";

            std::string dtcResp = send("03", 500);
            auto dtcBytes = splitResponse(dtcResp);
            if (dtcBytes.size() >= 2 && dtcBytes[0] == "43") {
                int numCodes = std::stoi(dtcBytes[1], nullptr, 16);
                for (size_t j = 2; j + 1 < dtcBytes.size() && j < (size_t)(2 + numCodes * 2); j += 2) {
                    std::string code = dtcBytes[j] + dtcBytes[j + 1];
                    if (code != "0000") {
                        DTCCode dtc;
                        dtc.code = code;
                        dtc.description = decodeDTCCode(code);
                        mod.dtcs.push_back(dtc);
                    }
                }
            }
        } else {
            std::cout << "-\n";
        }

        results.push_back(mod);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    send("AT SH", 30);
    send("AT CRA", 30);
    ensureNormalConfig();

    return results;
}

// ============================================================================
// Modo 02 — Freeze Frame
// ============================================================================

std::string ELM327::getFreezeFrame() {
    std::string result;

    std::string r = send("02", 500);

    if (r.find("NO DATA") != std::string::npos || r.empty()) {
        return "No hay datos de congelación almacenados";
    }

    auto bytes = splitResponse(r);

    if (bytes.size() >= 2 && bytes[0] == "42") {
        std::string pid = "0x" + bytes[1];
        result = "Freeze Frame - PID " + pid + "\n";
        result += "Datos: ";
        for (size_t i = 2; i < bytes.size(); i++) {
            result += bytes[i] + " ";
        }

        if (bytes.size() >= 4) {
            try {
                if (bytes[1] == "05") {
                    int temp = std::stoi(bytes[2], nullptr, 16) - 40;
                    result += "\nTemp. refrigerante: " + std::to_string(temp) + "°C";
                } else if (bytes[1] == "0C") {
                    int a = std::stoi(bytes[2], nullptr, 16);
                    int b = std::stoi(bytes[3], nullptr, 16);
                    result += "\nRPM: " + std::to_string((a * 256 + b) / 4);
                } else if (bytes[1] == "0D") {
                    result += "\nVelocidad: " + std::to_string(std::stoi(bytes[2], nullptr, 16)) + " km/h";
                } else if (bytes[1] == "04") {
                    int load = (std::stoi(bytes[2], nullptr, 16) * 100) / 255;
                    result += "\nCarga motor: " + std::to_string(load) + "%";
                } else if (bytes[1] == "0F") {
                    int temp = std::stoi(bytes[2], nullptr, 16) - 40;
                    result += "\nTemp. admisión: " + std::to_string(temp) + "°C";
                }
            } catch (...) {}
        }
    } else {
        result = "Respuesta inesperada (" + r.substr(0, 40) + ")";
    }

    return result;
}

// ============================================================================
// Modo 06 — Monitoreo interno OBD
// ============================================================================

std::string ELM327::getMonitorTests() {
    std::string result;

    std::string r = send("06", 800);

    if (r.find("NO DATA") != std::string::npos || r.empty()) {
        return "Monitores OBD no disponibles o no soportados";
    }

    auto bytes = splitResponse(r);

    if (bytes.size() >= 2 && bytes[0] == "46") {
        result = "=== MONITORES OBD (MODO 06) ===\n";
        result += "Test ID: " + bytes[1] + "\n";

        std::string tidName;
        if (bytes[1] == "00") tidName = "Catalizador B1";
        else if (bytes[1] == "01") tidName = "Catalizador B2";
        else if (bytes[1] == "02") tidName = "Sensor O2 B1";
        else if (bytes[1] == "03") tidName = "Sensor O2 B2";
        else if (bytes[1] == "20") tidName = "Monitor EGR";
        else if (bytes[1] == "21") tidName = "Monitor VVT";
        else if (bytes[1] == "31") tidName = "Componente EVAP";
        else tidName = "TID 0x" + bytes[1];
        result += "Test: " + tidName + "\n";

        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            try {
                int val = std::stoi(bytes[i+1], nullptr, 16);
                result += "  MID " + bytes[i] + ": " + std::to_string(val);
                result += "\n";
            } catch (...) {
                result += "  MID " + bytes[i] + ": " + bytes[i+1] + "\n";
            }
        }
    } else {
        result = "Modo 06 no soportado por esta ECU (respuesta: " + r.substr(0, 30) + ")";
    }

    return result;
}

std::string ELM327::getTestResults() {
    return getMonitorTests();
}

// ============================================================================
// Modo 08 — Prueba de sensor O2
// ============================================================================

bool ELM327::requestO2Test(int bank, int sensor) {
    std::cout << "[INFO] Solicitando prueba de sensor O2 B" << bank << "S" << sensor << "...\n";

    int sensorPos = (bank - 1) * 4 + (sensor - 1);
    std::stringstream ss;
    ss << "08 " << std::hex << std::setw(2) << std::setfill('0') << (0x01 + sensorPos);

    std::string resp = send(ss.str(), 1000);

    if (resp.find("48") != std::string::npos) {
        std::cout << "[OK] Prueba de sensor completada\n";
        return true;
    }

    std::cout << "[WARN] Prueba no disponible o no soportada\n";
    return false;
}

// ============================================================================
// Modo 09 — Información de la ECU
// ============================================================================

static std::string extractMode09String(const std::string& response, const std::string& header) {
    size_t pos = response.find(header);
    if (pos == std::string::npos) return "No disponible";
    std::string data = response.substr(pos + 4);
    data.erase(std::remove(data.begin(), data.end(), ' '), data.end());
    data.erase(std::remove(data.begin(), data.end(), '\r'), data.end());
    data.erase(std::remove(data.begin(), data.end(), '\n'), data.end());
    std::string result;
    bool startData = false;
    for (size_t i = 0; i + 1 < data.length(); i += 2) {
        std::string byteStr = data.substr(i, 2);
        if (!startData && byteStr == "01") { startData = true; continue; }
        if (startData) {
            try {
                int val = std::stoi(byteStr, nullptr, 16);
                if (val >= 32 && val <= 126) result += static_cast<char>(val);
                else break;
            } catch (...) { break; }
        }
    }
    if (!result.empty()) return result;
    return "No disponible";
}

std::string ELM327::getECUName() {
    return extractMode09String(send("090A", 500), "490A");
}

std::string ELM327::getCalibrationID() {
    return extractMode09String(send("0904", 500), "4904");
}

std::string ELM327::getCalibrationDate() {
    std::string resp = send("0906", 500);
    std::string date = extractMode09String(resp, "4906");
    if (date == "No disponible") {
        size_t pos = resp.find("4906");
        if (pos != std::string::npos) {
            std::string data = resp.substr(pos + 4, 16);
            data.erase(std::remove(data.begin(), data.end(), ' '), data.end());
            if (data.length() >= 8) {
                try {
                    int year = std::stoi(data.substr(0, 2), nullptr, 16) + 2000;
                    int month = std::stoi(data.substr(2, 2), nullptr, 16);
                    int day = std::stoi(data.substr(4, 2), nullptr, 16);
                    return std::to_string(year) + "-" +
                           (month < 10 ? "0" : "") + std::to_string(month) + "-" +
                           (day < 10 ? "0" : "") + std::to_string(day);
                } catch (...) {}
            }
        }
    }
    return date;
}

std::string ELM327::getPerformanceTracking() {
    std::string resp = send("0908", 500);
    std::string result = extractMode09String(resp, "4908");
    if (result == "No disponible") {
        size_t pos = resp.find("4908");
        if (pos != std::string::npos) {
            std::string data = resp.substr(pos + 4, 20);
            data.erase(std::remove(data.begin(), data.end(), ' '), data.end());
            if (!data.empty()) result = "Datos: " + data;
        }
    }
    return result;
}

// ============================================================================
// getVIN — Modo 09 PID 02
// ============================================================================

std::string ELM327::getVIN()
{
    std::string fullCmd = "0902\r";
    write(sock, fullCmd.c_str(), fullCmd.size());
    usleep(500000);

    char buffer[1024];
    int n = read(sock, buffer, sizeof(buffer)-1);
    if (n <= 0) return "No disponible";

    buffer[n] = '\0';
    std::string response(buffer);

    response.erase(std::remove(response.begin(), response.end(), '\r'), response.end());
    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());

    std::cout << "[DEBUG VIN RAW] " << response << std::endl;

    std::string vin;
    size_t pos = response.find("4902");

    if (pos != std::string::npos) {
        std::string data = response.substr(pos + 4);
        data.erase(std::remove(data.begin(), data.end(), ' '), data.end());

        for (size_t i = 0; i < data.length(); i += 2) {
            if (i + 2 <= data.length()) {
                std::string byteStr = data.substr(i, 2);
                if (i >= 2) {
                    try {
                        int val = std::stoi(byteStr, nullptr, 16);
                        if (val >= 32 && val <= 126) {
                            vin += static_cast<char>(val);
                        }
                    } catch (...) {
                        break;
                    }
                }
            }
        }
    }

    if (vin.empty()) {
        std::string cleanResp;
        for (char c : response) {
            if (isxdigit(c) || c == ' ') cleanResp += c;
        }

        std::stringstream ss(cleanResp);
        std::string byte;
        std::vector<std::string> bytes;
        while (ss >> byte) {
            if (byte.length() == 2) bytes.push_back(byte);
        }

        for (size_t i = 0; i < bytes.size() - 1; i++) {
            if (bytes[i] == "49" && bytes[i+1] == "02") {
                for (size_t j = i + 2; j < bytes.size(); j++) {
                    try {
                        int val = std::stoi(bytes[j], nullptr, 16);
                        if (val >= 32 && val <= 126) vin += static_cast<char>(val);
                        else if (val == 0 || val == 0xFF) break;
                    } catch (...) { break; }
                }
                break;
            }
        }
    }

    if (!vin.empty()) {
        vin.erase(std::remove_if(vin.begin(), vin.end(),
            [](char c) { return !isprint(c); }), vin.end());
        return vin;
    }

    return "No disponible";
}

bool ELM327::checkMIL()
{
    std::string r = send("0101");
    auto bytes = splitResponse(r);

    if (bytes.size() >= 3) {
        try {
            int status = std::stoi(bytes[2], nullptr, 16);
            return (status & 0x80) != 0;
        } catch (...) {
            return false;
        }
    }

    return false;
}

bool ELM327::setProtocol(int protocol)
{
    std::string cmd = "ATSP" + std::to_string(protocol);
    std::string r = send(cmd, 500);
    return r.find("OK") != std::string::npos;
}

bool ELM327::resetELM()
{
    std::string r = send("ATZ", 2000);
    return r.find("ELM327") != std::string::npos;
}

// ============================================================================
// sendCommand — Comando personalizado (Modo 22 GM, etc.)
// ============================================================================

std::string ELM327::sendCommand(const std::string& pidHex)
{
    if (!isConnected()) return "";

    send("AT SH 7E0");
    send("AT CRA 7E8");
    usleep(50000);

    std::string cmd = "22 " + pidHex;
    std::cout << "[TX GM] " << cmd << std::endl;

    std::string full = cmd + "\r";
    ::write(sock, full.c_str(), full.size());

    char buffer[512];
    std::string response;
    fd_set fds;
    struct timeval tv;
    int ret;

    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 500000;

    while (true) {
        ret = select(sock + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) break;

        int n = ::recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;

        buffer[n] = '\0';
        response += buffer;

        if (response.find(">") != std::string::npos) break;
    }

    response.erase(std::remove(response.begin(), response.end(), '\r'), response.end());
    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
    response.erase(std::remove(response.begin(), response.end(), '>'), response.end());

    std::cout << "[RX GM] " << response << std::endl;

    if (response.find("7F") == 0) {
        std::cerr << "Error en comando GM: PID no soportado o comunicación fallida" << std::endl;
        return "";
    }

    return response;
}

// ============================================================================
// Fuel Trims
// ============================================================================

double ELM327::getShortTermTrimBank1() {
    std::string r = send("0114");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 4 && bytes[0] == "41" && bytes[1] == "14") {
        try {
            int trimByte = std::stoi(bytes[3], nullptr, 16);
            return (trimByte * 100.0 / 128.0) - 100;
        } catch (...) { return -999.0; }
    }
    return -999.0;
}

double ELM327::getShortTermTrimBank2() {
    std::string r = send("0115");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 4 && bytes[0] == "41" && bytes[1] == "15") {
        try {
            int trimByte = std::stoi(bytes[3], nullptr, 16);
            return (trimByte * 100.0 / 128.0) - 100;
        } catch (...) { return -999.0; }
    }
    return -999.0;
}

double ELM327::getLongTermTrimBank1() {
    std::string r = send("0107");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 3) {
        try {
            int trim = std::stoi(bytes[2], nullptr, 16);
            return (trim * 100.0 / 128.0) - 100;
        } catch (...) { return -999.0; }
    }
    return -999.0;
}

double ELM327::getLongTermTrimBank2() {
    std::string r = send("0108");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 3) {
        try {
            int trim = std::stoi(bytes[2], nullptr, 16);
            return (trim * 100.0 / 128.0) - 100;
        } catch (...) { return -999.0; }
    }
    return -999.0;
}

FuelTrim ELM327::getAllFuelTrims() {
    FuelTrim ft;
    ft.shortTermBank1 = getShortTermTrimBank1();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ft.shortTermBank2 = getShortTermTrimBank2();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ft.longTermBank1 = getLongTermTrimBank1();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ft.longTermBank2 = getLongTermTrimBank2();
    ft.available = (ft.shortTermBank1 != -999.0 || ft.shortTermBank2 != -999.0 ||
                    ft.longTermBank1 != -999.0 || ft.longTermBank2 != -999.0);
    return ft;
}

OxygenSensor ELM327::getO2Sensor(int bank, int sensor) {
    OxygenSensor empty;
    empty.bank = bank;
    empty.sensor = sensor;
    empty.voltage = -1.0;
    empty.shortTermTrim = -1.0;

    std::string pid;
    if (bank == 1) {
        if (sensor <= 4) pid = "0114";
        else if (sensor <= 8) pid = "0116";
        else return empty;
    } else if (bank == 2) {
        if (sensor <= 4) pid = "0115";
        else if (sensor <= 8) pid = "0117";
        else return empty;
    } else return empty;

    std::string r = send(pid);
    auto bytes = splitResponse(r);
    int index = 2 + (sensor-1)*2;
    if ((int)bytes.size() > index+1 && bytes[0] == "41" && (bytes[1] == pid.substr(2,2) || bytes[1] == pid.substr(2))) {
        try {
            int vByte = std::stoi(bytes[index], nullptr, 16);
            int tByte = std::stoi(bytes[index+1], nullptr, 16);
            empty.voltage = vByte / 200.0;
            empty.shortTermTrim = (tByte * 100.0 / 128.0) - 100;
        } catch (...) {}
    }
    return empty;
}

// ============================================================================
// PIDs adicionales
// ============================================================================

int ELM327::getCatalystTempB1S1() {
    std::string r = send("013C");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 4 && bytes[0] == "41" && bytes[1] == "3C") {
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            return (int)(((a * 256.0 + b) / 10.0) - 40.0);
        } catch (...) { return -1; }
    }
    return -1;
}

double ELM327::getControlModuleVoltage() {
    std::string r = send("0142");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 4 && bytes[0] == "41" && bytes[1] == "42") {
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            return (a * 256.0 + b) / 1000.0;
        } catch (...) { return -1.0; }
    }
    return -1.0;
}

double ELM327::getEngineTorqueStandard() {
    std::string r = send("0163");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 4 && bytes[0] == "41" && bytes[1] == "63") {
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            return ((a * 256.0 + b) / 2.0) - 250.0;
        } catch (...) { return -1.0; }
    }
    std::string r2 = send("0161");
    auto bytes2 = splitResponse(r2);
    if (bytes2.size() >= 3 && bytes2[0] == "41" && bytes2[1] == "61") {
        try {
            int a = std::stoi(bytes2[2], nullptr, 16);
            return (a * 100.0 / 255.0) * 0.5;
        } catch (...) { return -1.0; }
    }
    return -1.0;
}

int ELM327::getOdometer() {
    std::string r = send("01A6");
    auto bytes = splitResponse(r);
    if (bytes.size() >= 5 && bytes[0] == "41" && bytes[1] == "A6") {
        try {
            int a = std::stoi(bytes[2], nullptr, 16);
            int b = std::stoi(bytes[3], nullptr, 16);
            int c = std::stoi(bytes[4], nullptr, 16);
            return (a * 65536 + b * 256 + c);
        } catch (...) { return -1; }
    }
    return -1;
}

std::vector<DTCCode> ELM327::getPendingDTCs() {
    std::vector<DTCCode> dtcs;
    std::string r = send("07", 500);

    if (r.find("NO DATA") != std::string::npos) {
        DTCCode noDTC;
        noDTC.code = "NONE";
        noDTC.description = "No hay códigos pendientes";
        dtcs.push_back(noDTC);
        return dtcs;
    }

    auto bytes = splitResponse(r);
    if (bytes.size() >= 2 && bytes[0] == "47") {
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            std::string code = bytes[i] + bytes[i + 1];
            if (code != "0000" && code != "FFFF") {
                DTCCode dtc;
                dtc.code = code;
                dtc.description = decodeDTCCode(code);
                dtcs.push_back(dtc);
            }
        }
    }

    if (dtcs.empty()) {
        DTCCode noDTC;
        noDTC.code = "NONE";
        noDTC.description = "No hay códigos pendientes";
        dtcs.push_back(noDTC);
    }
    return dtcs;
}

std::vector<DTCCode> ELM327::getPermanentDTCs() {
    std::vector<DTCCode> dtcs;
    std::string r = send("0A", 500);

    if (r.find("NO DATA") != std::string::npos) {
        DTCCode noDTC;
        noDTC.code = "NONE";
        noDTC.description = "No hay códigos permanentes";
        dtcs.push_back(noDTC);
        return dtcs;
    }

    auto bytes = splitResponse(r);
    if (bytes.size() >= 2 && bytes[0] == "4A") {
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            std::string code = bytes[i] + bytes[i + 1];
            if (code != "0000" && code != "FFFF") {
                DTCCode dtc;
                dtc.code = code;
                dtc.description = decodeDTCCode(code);
                dtcs.push_back(dtc);
            }
        }
    }

    if (dtcs.empty()) {
        DTCCode noDTC;
        noDTC.code = "NONE";
        noDTC.description = "No hay códigos permanentes";
        dtcs.push_back(noDTC);
    }
    return dtcs;
}

bool ELM327::clearPendingDTCs() {
    return clearDTCs();
}

// ============================================================================
// Dashboard rápido — 10 PIDs individuales con pausas
// ============================================================================

ELM327::DashboardData ELM327::getDashboardFast() {
    DashboardData d;
    d.valid = false;
    d.rpm = -1; d.speed = -1; d.coolant = -1; d.load = -1;
    d.throttle = -1; d.maf = -1; d.fuelLevel = -1; d.timing = -1;
    d.intakeTemp = -1; d.intakePressure = -1;

    if (!isConnected()) return d;

    if (!m_configValid) {
        ensureNormalConfig();
    }

    {
        std::string r = send("010C", 300);
        auto b = splitResponse(r);
        if (b.size() >= 4) {
            try {
                int a = std::stoi(b[2], nullptr, 16);
                int b2 = std::stoi(b[3], nullptr, 16);
                d.rpm = (a * 256 + b2) / 4;
            } catch (...) {}
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::string r = send("010D", 300);
        auto b = splitResponse(r);
        if (b.size() >= 3) {
            try { d.speed = std::stoi(b[2], nullptr, 16); } catch (...) {}
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::string r = send("0105", 300);
        auto b = splitResponse(r);
        if (b.size() >= 3) {
            try { d.coolant = std::stoi(b[2], nullptr, 16) - 40; } catch (...) {}
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::string r = send("0104", 300);
        auto b = splitResponse(r);
        if (b.size() >= 3) {
            try { d.load = (std::stoi(b[2], nullptr, 16) * 100) / 255; } catch (...) {}
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::string r = send("0111", 300);
        auto b = splitResponse(r);
        if (b.size() >= 3) {
            try { d.throttle = (std::stoi(b[2], nullptr, 16) * 100.0) / 255.0; } catch (...) {}
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::string r = send("010B", 300);
        auto b = splitResponse(r);
        if (b.size() >= 3) {
            try { d.intakePressure = std::stoi(b[2], nullptr, 16); } catch (...) {}
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::string r = send("010F", 300);
        auto b = splitResponse(r);
        if (b.size() >= 3) {
            try { d.intakeTemp = std::stoi(b[2], nullptr, 16) - 40; } catch (...) {}
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::string r = send("010E", 300);
        auto b = splitResponse(r);
        if (b.size() >= 3) {
            try { d.timing = (std::stoi(b[2], nullptr, 16) / 2.0) - 64; } catch (...) {}
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::string r = send("0110", 300);
        auto b = splitResponse(r);
        if (b.size() >= 4) {
            try {
                int a = std::stoi(b[2], nullptr, 16);
                int b2 = std::stoi(b[3], nullptr, 16);
                d.maf = (a * 256 + b2) / 100.0;
            } catch (...) {}
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::string r = send("012F", 200);
        auto b = splitResponse(r);
        if (b.size() >= 3) {
            try { d.fuelLevel = (std::stoi(b[2], nullptr, 16) * 100.0) / 255.0; } catch (...) {}
        }
    }

    d.valid = (d.rpm >= 0);
    return d;
}

// ============================================================================
// fullCleanup — Libera puerto Bluetooth
// ============================================================================

void ELM327::fullCleanup() {
    std::cout << "[monitor] Liberando puerto Bluetooth..." << std::endl;
    std::cout << "[monitor] Linux: liberando rfcomm..." << std::endl;

    int ret = system("rfcomm release all 2>/dev/null");
    if (ret == 0) {
        std::cout << "[monitor] rfcomm liberado" << std::endl;
    } else {
        std::cout << "[monitor] rfcomm no requiere liberación" << std::endl;
    }

    std::cout << "[monitor] Sistema listo para conexión" << std::endl;
}

// ============================================================================
// Protocolo cache — protocol.cache
// ============================================================================

std::string ELM327::getCacheFilePath() {
    return "protocol.cache";
}

void ELM327::loadCachedProtocol() {
    std::ifstream f(getCacheFilePath());
    if (!f.is_open()) {
        m_protocol = -1;
        return;
    }
    int p = -1;
    f >> p;
    if (p >= 1 && p <= 9) {
        m_protocol = p;
        std::cout << "[CACHE] Protocolo cargado: " << m_protocol << std::endl;
    } else {
        m_protocol = -1;
    }
}

void ELM327::saveCachedProtocol(int protocol) {
    std::ofstream f(getCacheFilePath());
    if (!f.is_open()) {
        std::cerr << "[CACHE] No se pudo guardar protocol.cache" << std::endl;
        return;
    }
    f << protocol;
    std::cout << "[CACHE] Protocolo " << protocol << " guardado a " << getCacheFilePath() << std::endl;
}

void ELM327::detectAndCacheProtocol() {
    std::string r = send("ATDPN", 300);
    if (r.empty()) {
        std::cout << "[CACHE] ATDPN no respondió, no se cachea protocolo" << std::endl;
        return;
    }
    r.erase(std::remove(r.begin(), r.end(), '\r'), r.end());
    r.erase(std::remove(r.begin(), r.end(), '\n'), r.end());
    r.erase(std::remove(r.begin(), r.end(), ' '), r.end());
    size_t p = r.find('>');
    if (p != std::string::npos) r = r.substr(0, p);

    if (r.empty()) {
        std::cout << "[CACHE] ATDPN respuesta vacía" << std::endl;
        return;
    }

    try {
        int protocol = std::stoi(r);
        if (protocol >= 1 && protocol <= 9) {
            m_protocol = protocol;
            saveCachedProtocol(protocol);
            std::cout << "[CACHE] Protocolo detectado: " << protocol << std::endl;
        } else {
            std::cout << "[CACHE] Número de protocolo fuera de rango: " << protocol << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "[CACHE] No se pudo parsear ATDPN: '" << r << "' (" << e.what() << ")" << std::endl;
    }
}

// ============================================================================
// Recuperación STOPPED y configuración normal
// ============================================================================

bool ELM327::recoverFromStopped() {
    std::cout << "[RECOVERY] Recuperando ELM327 de estado STOPPED...\n";

    const char* wake = "\r";
    ::write(sock, wake, 1);
    usleep(150000);

    std::string atdResp = send("ATD", 500);

    if (atdResp.empty() || atdResp.find("OK") == std::string::npos) {
        std::cout << "[RECOVERY] ATD no respondió, intentando ATZ...\n";
        send("ATZ", 2000);
        if (m_stoppedPenaltyMs > 0) {
            m_stoppedPenaltyMs = 0;
            m_consecutiveSuccess = 0;
            std::cout << "[ADAPT] Penalidad restablecida a 0ms (ATZ completado)\n";
        }
    }

    char tmp[256];
    fd_set fds;
    struct timeval tv;

    for (int attempt = 0; attempt < 5; attempt++) {
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        int ret = select(sock + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) break;

        if (::recv(sock, tmp, sizeof(tmp) - 1, 0) <= 0) break;
    }

    send("ATE0", 150);
    send("ATH0", 150);
    send("ATS0", 150);
    send("ATL0", 150);
    if (m_protocol > 0) {
        send("ATSP" + std::to_string(m_protocol), 500);
        std::cout << "[CACHE] Re-aplicando protocolo cacheado: ATSP" << m_protocol << std::endl;
    } else {
        send("ATSP0", 500);
    }
    send("ATAT1", 100);
    send("ATST10", 100);
    send("ATAL1", 50);
    if (m_protocol >= 6 && m_protocol <= 9) {
        send("ATCF 7E8", 50);
        send("ATCM 7FF", 50);
    }

    m_configValid = true;
    std::cout << "[RECOVERY] ELM327 recuperado correctamente\n";
    return true;
}

void ELM327::ensureNormalConfig() {
    m_configValid = true;
    send("ATE0", 150);
    send("ATH0", 150);
    send("ATS0", 150);
    send("ATL0", 150);
    send("ATAT1", 100);
    send("ATST10", 100);
    send("ATAL1", 50);
    if (m_protocol >= 6 && m_protocol <= 9) {
        send("ATCF 7E8", 50);
        send("ATCM 7FF", 50);
    }
}

// ============================================================================
// Logging completo — Todos los sensores
// ============================================================================

void ELM327::logAllSensorsRaw(const std::string& filename) {
    // ensureLogsDir is in Logger class
    struct stat st;
    if (stat("logs", &st) != 0) {
        mkdir("logs", 0755);
    }

    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[ERROR] No se pudo crear " << filename << std::endl;
        return;
    }

    file << "timestamp,pid,command,hex_response,interpreted_value\n";

    auto fmt = [](double val) -> std::string {
        std::ostringstream oss;
        oss.imbue(std::locale::classic());
        oss << val;
        return oss.str();
    };

    struct SensorInfo {
        std::string name;
        std::string cmd;
        std::function<std::string()> interpreter;
    };

    std::vector<SensorInfo> sensors = {
        {"RPM", "010C", [this]() { return std::to_string(getRPM()); }},
        {"Speed", "010D", [this]() { return std::to_string(getSpeed()); }},
        {"CoolantTemp", "0105", [this]() { return std::to_string(getCoolantTemp()); }},
        {"EngineLoad", "0104", [this]() { return std::to_string(getEngineLoad()); }},
        {"Throttle", "0111", [this, fmt]() { return fmt(getThrottlePosition()); }},
        {"IntakePressure", "010B", [this, fmt]() { return fmt(getIntakePressure()); }},
        {"IntakeTemp", "010F", [this]() { return std::to_string(getIntakeTemp()); }},
        {"TimingAdvance", "010E", [this, fmt]() { return fmt(getTimingAdvance()); }},
        {"MAF", "0110", [this, fmt]() { return fmt(getMAF()); }},
        {"FuelLevel", "012F", [this, fmt]() { return fmt(getFuelLevel()); }},
        {"BaroPressure", "0133", [this, fmt]() { return fmt(getBarometricPressure()); }},
        {"LTFT_Bank1", "0107", [this, fmt]() { return fmt(getLongTermTrimBank1()); }},
        {"LTFT_Bank2", "0108", [this, fmt]() { return fmt(getLongTermTrimBank2()); }},
        {"STFT_Bank1_S1", "0114", [this, fmt]() { return fmt(getShortTermTrimBank1()); }},
        {"STFT_Bank2_S1", "0115", [this, fmt]() { return fmt(getShortTermTrimBank2()); }},
        {"O2_B1S1_Voltage", "0114", [this, fmt]() { return fmt(getO2Sensor(1,1).voltage); }},
        {"O2_B2S1_Voltage", "0115", [this, fmt]() { return fmt(getO2Sensor(2,1).voltage); }}
    };

    time_t now = time(nullptr);
    for (auto& s : sensors) {
        std::string resp = send(s.cmd, 400);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::string rawResp = resp;
        rawResp.erase(std::remove(rawResp.begin(), rawResp.end(), '\r'), rawResp.end());
        rawResp.erase(std::remove(rawResp.begin(), rawResp.end(), '\n'), rawResp.end());
        size_t p = rawResp.find('>');
        if (p != std::string::npos) rawResp = rawResp.substr(0, p);

        std::string value = s.interpreter();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        file << now << ",\"" << s.name << "\",\"" << s.cmd << "\",\""
             << rawResp << "\",\"" << value << "\"\n";
        file.flush();
    }

    file.close();
    std::cout << "[OK] Log completo guardado en " << filename << std::endl;
}

// ============================================================================
// Log P0171 — Diagnóstico de mezcla pobre
// ============================================================================

void ELM327::logP0171Diagnostic(const std::string& filename, int durationSec) {
    struct stat st;
    if (stat("logs", &st) != 0) {
        mkdir("logs", 0755);
    }

    std::ofstream file(filename);
    file.imbue(std::locale::classic());
    if (!file.is_open()) {
        std::cerr << "[ERROR] No se pudo crear " << filename << std::endl;
        return;
    }

    file << "timestamp,rpm,speed,stft_b1(%),stft_b2(%),ltft_b1(%),ltft_b2(%),o2_b1s1_voltage(V),o2_b2s1_voltage(V)\n";

    time_t start = time(nullptr);
    int count = 0;

    std::cout << "[INFO] Registrando datos para diagnóstico P0171 durante " << durationSec << " segundos...\n";
    std::cout << "Presione Ctrl+C para detener antes.\n\n";

    while (time(nullptr) - start < durationSec) {
        time_t now = time(nullptr);

        int rpm = getRPM();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        int speed = getSpeed();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        double ltft1 = -999.0;
        {
            std::string r = send("0107", 300);
            auto b = splitResponse(r);
            if (b.size() >= 3) {
                ltft1 = (std::stoi(b[2], nullptr, 16) * 100.0 / 128.0) - 100;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        double ltft2 = -999.0;
        {
            std::string r = send("0108", 300);
            auto b = splitResponse(r);
            if (b.size() >= 3) {
                ltft2 = (std::stoi(b[2], nullptr, 16) * 100.0 / 128.0) - 100;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        double stft1 = -999.0;
        double o2v1 = -1.0;
        {
            std::string r = send("0114", 400);
            auto b = splitResponse(r);
            if (b.size() >= 4 && b[0] == "41" && b[1] == "14") {
                o2v1 = std::stoi(b[2], nullptr, 16) / 200.0;
                stft1 = (std::stoi(b[3], nullptr, 16) * 100.0 / 128.0) - 100;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        double stft2 = -999.0;
        double o2v2 = -1.0;
        {
            std::string r = send("0115", 400);
            auto b = splitResponse(r);
            if (b.size() >= 4 && b[0] == "41" && b[1] == "15") {
                o2v2 = std::stoi(b[2], nullptr, 16) / 200.0;
                stft2 = (std::stoi(b[3], nullptr, 16) * 100.0 / 128.0) - 100;
            }
        }

        file << now << ","
             << rpm << ","
             << speed << ","
             << stft1 << ","
             << stft2 << ","
             << ltft1 << ","
             << ltft2 << ","
             << o2v1 << ","
             << o2v2 << "\n";
        file.flush();

        count++;
        if (count % 5 == 0) {
            std::cout << "   Registros: " << count << " | RPM=" << rpm
                      << " STFT_B1=" << std::fixed << std::setprecision(1) << stft1 << "%"
                      << " LTFT_B1=" << ltft1 << "%"
                      << "\r" << std::flush;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    file.close();
    std::cout << "\n[OK] Log P0171 guardado en " << filename << " (" << count << " registros en " << (time(nullptr)-start) << "s)\n";
}
