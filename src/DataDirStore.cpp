#include "DataDirStore.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace {

std::string datePart(const std::string& createdAt)
{
    if (createdAt.size() >= 10) {
        return createdAt.substr(0, 10);
    }
    return "unknown-date";
}

std::string timePart(const std::string& createdAt)
{
    if (createdAt.size() >= 19) {
        return createdAt.substr(11, 8);
    }
    return "00:00:00";
}

std::string score(double value)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value;
    return out.str();
}

bool isDefaultScore(double value)
{
    return std::abs(value - DefaultObservationScore) < 0.000001;
}

void appendScore(std::ostream& out, char name, double value)
{
    if (!isDefaultScore(value)) {
        out << " " << name << score(value);
    }
}

}

DataDirStore::DataDirStore(std::string path)
    : path_(std::move(path))
{
    std::filesystem::create_directories(path_);
}

void DataDirStore::save(const Observation& observation)
{
    std::filesystem::create_directories(path_);
    const std::filesystem::path logPath = std::filesystem::path(path_) / (datePart(observation.createdAt) + ".log");

    std::ofstream out(logPath, std::ios::app);
    if (!out) {
        throw std::runtime_error("failed to open log file: " + logPath.string());
    }

    out << timePart(observation.createdAt);
    appendScore(out, 'e', observation.energy);
    appendScore(out, 'm', observation.mood);
    appendScore(out, 'g', observation.grounding);
    out << "\n";

    std::istringstream lines(observation.activity);
    std::string line;
    if (observation.activity.empty()) {
        out << "    \n";
    } else {
        while (std::getline(lines, line)) {
            out << "    " << line << "\n";
        }
    }
    out << "\n";
}

const std::string& DataDirStore::path() const
{
    return path_;
}

bool DataDirStore::shouldUseDataDir(const std::string& path)
{
    if (path.empty()) {
        return false;
    }

    std::filesystem::path fsPath(path);
    std::error_code ec;
    if (std::filesystem::is_directory(fsPath, ec)) {
        return true;
    }

    return path.back() == '/' || path.back() == '\\';
}
