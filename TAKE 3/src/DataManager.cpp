#include "DataManager.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iostream>
#include <optional>
#include <array>
#include <vector>
#include <unordered_map>

namespace Statix {

    /*-------------------------------------------------------------
       LoadTable – converts the generic *Table* into a vector of
       SurveyRespondent objects, validates data, and computes scores.
    -------------------------------------------------------------*/
    bool DataManager::LoadTable(const Table& raw, std::string& outError)
    {
        Clear();

        if (raw.empty()) {
            outError = "Empty table – nothing to load.";
            return false;
        }

        // Expected column names (must match the CSV / XLSX header exactly)
        const std::vector<std::string> required = {
            "Timestamp", "Gender", "AgeGroup", "Education", "Hours",
            "Platform", "ContentPref",
            // Q7 … Q21 (15 numeric columns)
            "Q7","Q8","Q9","Q10","Q11","Q12","Q13","Q14","Q15",
            "Q16","Q17","Q18","Q19","Q20","Q21"
        };

        // Verify that every required column exists in the first row (header)
        const Row& headerCheck = raw.front();
        // Build a map from normalized header names to actual column keys
        std::unordered_map<std::string, std::string> headerMap;
        for (const auto& [colName, _] : headerCheck) {
            std::string norm = colName;
            // trim whitespace
            norm.erase(0, norm.find_first_not_of(" \t\n\r"));
            norm.erase(norm.find_last_not_of(" \t\n\r") + 1);
            // to lower case
            std::transform(norm.begin(), norm.end(), norm.begin(), ::tolower);
            headerMap[norm] = colName;
        }

        // Debug: list detected column keys (normalized)
        std::cout << "[LoadDataset] Detected columns:";
        for (const auto& kv : headerMap) {
            std::cout << " '" << kv.first << "'";
        }
        std::cout << "\n";

        // Helper to find column key case-insensitively
        auto findKey = [&](const std::string& req) -> std::optional<std::string> {
            std::string norm = req;
            norm.erase(0, norm.find_first_not_of(" \t\n\r"));
            norm.erase(norm.find_last_not_of(" \t\n\r") + 1);
            std::transform(norm.begin(), norm.end(), norm.begin(), ::tolower);
            auto it = headerMap.find(norm);
            if (it != headerMap.end()) return it->second;
            return std::nullopt;
            };

        // Iterate over each raw row, map fields, compute scores.
        size_t rowIdx = 0;
        for (size_t i = 1; i < raw.size(); ++i) {   // skip header
            const auto& rawRow = raw[i];
            ++rowIdx;

            // Map header names to row indices for case-insensitive access
            std::unordered_map<std::string, std::string> rowMap;
            for (const auto& [colName, val] : rawRow) {
                std::string norm = colName;
                std::transform(norm.begin(), norm.end(), norm.begin(), ::tolower);
                rowMap[norm] = val;
            }

            // Helper to trim whitespace from both ends
            auto trim = [](std::string s) -> std::string {
                size_t start = s.find_first_not_of(" \t\n\r");
                size_t end = s.find_last_not_of(" \t\n\r");
                if (start == std::string::npos) return "";
                return s.substr(start, end - start + 1);
                };

            auto getField = [&](const std::string& col) -> std::string {
                std::string norm = col;
                std::transform(norm.begin(), norm.end(), norm.begin(), ::tolower);
                auto it = rowMap.find(norm);
                return (it != rowMap.end()) ? trim(it->second) : "";
                };

            SurveyRespondent rec;
            rec.timestamp = Normalise(getField("Timestamp"));
            rec.gender = Normalise(getField("Gender"));
            rec.ageGroup = Normalise(getField("AgeGroup"));
            rec.education = Normalise(getField("Education"));
            rec.hours = Normalise(getField("Hours"));
            rec.platform = Normalise(getField("Platform"));
            rec.contentPref = Normalise(getField("ContentPref"));

            // ----- Likert responses ----------------------------------------
            std::array<int, 15> likert{};
            std::vector<std::string> likertTokens;
            likertTokens.reserve(15);
            for (int i = 0; i < 15; ++i) {
                std::string col = "Q" + std::to_string(7 + i);
                likertTokens.emplace_back(getField(col));
            }

            // Debug: show the raw Likert tokens for this row
            std::cout << "[LoadDataset] Row " << rowIdx << " Likert tokens: ";
            for (const auto& t : likertTokens) std::cout << "'" << t << "' ";
            std::cout << "\n";

            if (!ParseLikert(likertTokens, 0, likert)) {
                std::cout << "[LoadDataset] Skipping row " << rowIdx << " due to invalid Likert values.\n";
                continue;
            }
            rec.responses = likert;

            // ----- Qualitative text (optional) -----------------------------
            auto it1 = rawRow.find(findKey("Qualitative1").value_or("Qualitative1"));
            auto it2 = rawRow.find(findKey("Qualitative2").value_or("Qualitative2"));
            if (it1 != rawRow.end()) rec.qualitative1 = it1->second;
            if (it2 != rawRow.end()) rec.qualitative2 = it2->second;

            // ----- Compute derived scores -----------------------------------
            rec.ComputeAnalytics();
            PrintRespondent(rec);

            respondents_.push_back(std::move(rec));
        }

        if (respondents_.empty()) {
            outError = "No valid rows were parsed – all rows may have been malformed.";
            return false;
        }
        return true;
    }

    /*-------------------------------------------------------------
       Simple normalisation – replace UTF‑8 en‑dash with ordinary hyphen,
       trim surrounding whitespace.
    -------------------------------------------------------------*/
    std::string DataManager::Normalise(const std::string& s)
    {
        std::string out = s;
        const std::string enDash = "\xE2\x80\x93";
        size_t pos = 0;
        while ((pos = out.find(enDash, pos)) != std::string::npos) {
            out.replace(pos, enDash.length(), "-");
            ++pos;
        }
        // Trim left
        while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front())))
            out.erase(out.begin());
        // Trim right
        while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back())))
            out.pop_back();
        return out;
    }

    /*-------------------------------------------------------------
       Parse 15 consecutive integer tokens (must be 1‑5).
       B-16 FIX: Made stricter while keeping robustness.
    -------------------------------------------------------------*/
    bool DataManager::ParseLikert(const std::vector<std::string>& tokens,
        size_t startIdx,
        std::array<int, 15>& out)
    {
        if (tokens.size() < startIdx + 15) return false;

        for (size_t i = 0; i < 15; ++i) {
            std::string t = tokens[startIdx + i];

            // Trim whitespace
            size_t first = t.find_first_not_of(" \t\n\r");
            size_t last = t.find_last_not_of(" \t\n\r");
            if (first == std::string::npos) {
                out[i] = 0;
                continue;
            }
            t = t.substr(first, last - first + 1);

            // Strict: must be 1-5 after cleaning
            try {
                int val = std::stoi(t);
                if (val >= 1 && val <= 5) {
                    out[i] = val;
                    continue;
                }
            }
            catch (...) {}

            // Text fallback (still supported but stricter)
            std::string norm = Normalise(t);
            if (norm.find("strong agree") != std::string::npos) out[i] = 5;
            else if (norm.find("agree") != std::string::npos) out[i] = 4;
            else if (norm.find("neutral") != std::string::npos) out[i] = 3;
            else if (norm.find("disagree") != std::string::npos) out[i] = 2;
            else if (norm.find("strong disagree") != std::string::npos) out[i] = 1;
            else {
                out[i] = 0;        // invalid → 0
            }
        }
        return true;
    }

    /*-------------------------------------------------------------
       Return a vector of floats for a computed metric.
    -------------------------------------------------------------*/
    std::vector<float> DataManager::GetMetricColumn(const std::string& metric) const
    {
        std::vector<float> out;
        out.reserve(respondents_.size());

        for (const auto& r : respondents_) {
            if (metric == "Exposure Score")          out.push_back(r.ExposureScore());
            else if (metric == "Normalization Score") out.push_back(r.NormalizationScore());
            else if (metric == "Platform Attitude")   out.push_back(r.PlatformAttitude());
            else if (metric == "RealWorld Score")    out.push_back(r.RealWorldScore());
            else if (metric == "Total Score")        out.push_back(r.TotalScore());
            else                                    out.push_back(0.0f);
        }
        return out;
    }

    /*-------------------------------------------------------------
       Remove all stored respondents.
    -------------------------------------------------------------*/
    void DataManager::Clear()
    {
        respondents_.clear();
    }

    // B-24: Added for application.cpp compatibility
    std::vector<std::string> DataManager::GetColumnNames()
    {
        // Return the original column names from last load if you want full fidelity.
        // For now returning metric names is safe and matches UI expectations.
        return GetMetricNames();
    }
    
} // namespace Statix
