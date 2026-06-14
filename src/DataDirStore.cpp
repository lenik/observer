#include "DataDirStore.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <ctime>
#include <vector>

namespace {

std::string datePart(const std::string& timestamp)
{
    if (timestamp.size() >= 10) {
        return timestamp.substr(0, 10);
    }
    return "unknown-date";
}

std::string timePart(const std::string& timestamp)
{
    if (timestamp.size() >= 19) {
        return timestamp.substr(11, 8);
    }
    return "00:00:00";
}

bool parseTimestamp(const std::string& value, std::tm& result)
{
    std::istringstream in(value.substr(0, 19));
    in >> std::get_time(&result, "%Y-%m-%dT%H:%M:%S");
    return !in.fail();
}

std::string durationPart(const std::string& promptedAt, const std::string& submittedAt)
{
    std::tm prompted{};
    std::tm submitted{};
    if (!parseTimestamp(promptedAt, prompted) || !parseTimestamp(submittedAt, submitted)) {
        return "0:00";
    }

    std::time_t promptedTime = std::mktime(&prompted);
    std::time_t submittedTime = std::mktime(&submitted);
    if (promptedTime == static_cast<std::time_t>(-1) || submittedTime == static_cast<std::time_t>(-1)) {
        return "0:00";
    }

    long seconds = static_cast<long>(std::difftime(submittedTime, promptedTime));
    if (seconds < 0) {
        seconds = 0;
    }

    std::ostringstream out;
    out << seconds / 60 << ":" << std::setw(2) << std::setfill('0') << seconds % 60;
    return out.str();
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

std::string timestampFromParts(const std::string& date, const std::string& time)
{
    return date + "T" + time + "+0000";
}

double parseScoreToken(const std::string& token, char name, double fallback)
{
    if (token.size() < 2 || token[0] != name) {
        return fallback;
    }
    try {
        return std::stod(token.substr(1));
    } catch (...) {
        return fallback;
    }
}

Observation parseRecord(const std::string& date, const std::string& header, const std::vector<std::string>& activityLines)
{
    std::istringstream in(header);
    std::string promptedTime;
    in >> promptedTime;

    Observation observation{
        timestampFromParts(date, promptedTime.empty() ? "00:00:00" : promptedTime),
        timestampFromParts(date, promptedTime.empty() ? "00:00:00" : promptedTime),
        DefaultObservationScore,
        DefaultObservationScore,
        DefaultObservationScore,
        "",
        "",
    };

    std::string token;
    while (in >> token) {
        if (token == "duration") {
            in >> token;
            continue;
        }
        if (token == "submitted") {
            std::string submittedTime;
            if (in >> submittedTime) {
                observation.submittedAt = timestampFromParts(date, submittedTime);
            }
            continue;
        }
        observation.energy = parseScoreToken(token, 'e', observation.energy);
        observation.mood = parseScoreToken(token, 'm', observation.mood);
        observation.grounding = parseScoreToken(token, 'g', observation.grounding);
    }

    for (std::size_t i = 0; i < activityLines.size(); ++i) {
        if (i > 0) {
            observation.activity += "\n";
        }
        observation.activity += activityLines[i];
    }
    return observation;
}

std::string dateFromLogPath(const std::filesystem::path& path)
{
    return path.stem().string();
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
    const std::filesystem::path logPath = std::filesystem::path(path_) / (datePart(observation.promptedAt) + ".log");

    std::ofstream out(logPath, std::ios::app);
    if (!out) {
        throw std::runtime_error("failed to open log file: " + logPath.string());
    }

    out << timePart(observation.promptedAt);
    appendScore(out, 'e', observation.energy);
    appendScore(out, 'm', observation.mood);
    appendScore(out, 'g', observation.grounding);
    out << " duration " << durationPart(observation.promptedAt, observation.submittedAt)
        << " submitted " << timePart(observation.submittedAt);
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

std::vector<Observation> DataDirStore::loadAll()
{
    std::vector<Observation> observations;
    std::error_code ec;
    if (!std::filesystem::is_directory(path_, ec)) {
        return observations;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(path_, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".log") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& file : files) {
        std::ifstream in(file);
        if (!in) {
            continue;
        }

        const std::string date = dateFromLogPath(file);
        std::string header;
        std::vector<std::string> activityLines;
        std::string line;
        auto flush = [&]() {
            if (!header.empty()) {
                observations.push_back(parseRecord(date, header, activityLines));
                header.clear();
                activityLines.clear();
            }
        };

        while (std::getline(in, line)) {
            if (line.empty()) {
                flush();
                continue;
            }
            if (line.rfind("    ", 0) == 0) {
                if (!header.empty()) {
                    activityLines.push_back(line.substr(4));
                }
                continue;
            }
            flush();
            header = line;
        }
        flush();
    }

    return observations;
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
