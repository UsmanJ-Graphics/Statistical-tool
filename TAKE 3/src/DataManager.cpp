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
       LoadTable – converts the generic Table into a vector of
       SurveyRespondent objects, validates data, and computes scores.
    -------------------------------------------------------------*/
    bool DataManager::LoadTable(const Table& raw, std::string& outError)
    {
        Clear();

        if (raw.empty()) {
            outError = "Empty table – nothing to load.";
            return false;
        }

        // Build a case-insensitive header map from the first row.
        const Row& headerCheck = raw.front();
        std::unordered_map<std::string, std::string> headerMap;
        for (const auto& [colName, _] : headerCheck) {
            std::string norm = colName;
            norm.erase(0, norm.find_first_not_of(" \t\n\r"));
            auto lastNonSpace = norm.find_last_not_of(" \t\n\r");
            if (lastNonSpace != std::string::npos) norm.erase(lastNonSpace + 1);
            std::transform(norm.begin(), norm.end(), norm.begin(), ::tolower);
            headerMap[norm] = colName;
        }

        std::cout << "[LoadDataset] Detected columns:";
        for (const auto& kv : headerMap)
            std::cout << " '" << kv.first << "'";
        std::cout << "\n";

        // Iterate over each data row (skip header at index 0).
        size_t rowIdx = 0;
        for (size_t i = 1; i < raw.size(); ++i) {
            const auto& rawRow = raw[i];
            ++rowIdx;

            // Build a case-insensitive value map for this row.
            std::unordered_map<std::string, std::string> rowMap;
            for (const auto& [colName, val] : rawRow) {
                std::string norm = colName;
                std::transform(norm.begin(), norm.end(), norm.begin(), ::tolower);
                rowMap[norm] = val;
            }

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

            // B-WARN-4 FIX: CSV has no Timestamp column — store respondent number
            // so the Raw Matrix tab shows something meaningful instead of blanks.
            rec.timestamp = "R" + std::to_string(rowIdx);

            rec.gender = Normalise(getField("Gender"));
            rec.education = Normalise(getField("Education"));
            rec.hours = Normalise(getField("Hours"));
            rec.platform = Normalise(getField("Platform"));

            // B-WARN-3 FIX: CSV headers are "Age" and "Content", not
            // "AgeGroup" and "ContentPref".
            rec.ageGroup = Normalise(getField("Age"));
            rec.contentPref = Normalise(getField("Content"));

            // ----- Likert responses (Q7–Q21) --------------------------------
            // B-CRIT-1 FIX: index 10 (zero-based) maps to Q17r, not Q17.
            // The column name array now matches the actual CSV headers exactly.
            static const std::array<const char*, 15> kLikertCols = {
                "Q7",  "Q8",  "Q9",  "Q10",
                "Q11", "Q12", "Q13", "Q14",
                "Q15", "Q16",
                "Q17r",   // <-- was "Q17"; CSV column is "Q17r" (reverse-coded)
                "Q18",
                "Q19", "Q20", "Q21"
            };

            std::vector<std::string> likertTokens;
            likertTokens.reserve(15);
            for (const char* col : kLikertCols)
                likertTokens.emplace_back(getField(col));

            std::cout << "[LoadDataset] Row " << rowIdx << " Likert tokens: ";
            for (const auto& t : likertTokens) std::cout << "'" << t << "' ";
            std::cout << "\n";

            std::array<int, 15> likert{};
            if (!ParseLikert(likertTokens, 0, likert)) {
                std::cout << "[LoadDataset] Skipping row " << rowIdx
                    << " due to invalid Likert values.\n";
                continue;
            }
            rec.responses = likert;

            // ----- Qualitative text (optional) ------------------------------
            auto it1 = rawRow.find("Qualitative1");
            auto it2 = rawRow.find("Qualitative2");
            if (it1 != rawRow.end()) rec.qualitative1 = it1->second;
            if (it2 != rawRow.end()) rec.qualitative2 = it2->second;

            // ----- Compute derived scores -----------------------------------
            // ComputeAnalytics() applies the Q17r reversal before scoring.
            rec.ComputeAnalytics();
            PrintRespondent(rec);

            respondents_.push_back(std::move(rec));
        }

        if (respondents_.empty()) {
            outError = "No valid rows were parsed – check that Q7–Q21 and Q17r columns exist.";
            return false;
        }
        return true;
    }

    /*-------------------------------------------------------------
       Normalise – replace UTF-8 en-dash, trim whitespace.
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
        while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front())))
            out.erase(out.begin());
        while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back())))
            out.pop_back();
        return out;
    }

    /*-------------------------------------------------------------
       ParseLikert – parse 15 consecutive tokens, must be 1-5.
    -------------------------------------------------------------*/
    bool DataManager::ParseLikert(const std::vector<std::string>& tokens,
        size_t startIdx,
        std::array<int, 15>& out)
    {
        if (tokens.size() < startIdx + 15) return false;

        for (size_t i = 0; i < 15; ++i) {
            std::string t = tokens[startIdx + i];

            size_t first = t.find_first_not_of(" \t\n\r");
            size_t last = t.find_last_not_of(" \t\n\r");
            if (first == std::string::npos) {
                out[i] = 0;
                continue;
            }
            t = t.substr(first, last - first + 1);

            try {
                int val = std::stoi(t);
                if (val >= 1 && val <= 5) {
                    out[i] = val;
                    continue;
                }
            }
            catch (...) {}

            // Text fallback
            std::string norm = Normalise(t);
            if (norm.find("strong agree") != std::string::npos) out[i] = 5;
            else if (norm.find("agree") != std::string::npos) out[i] = 4;
            else if (norm.find("neutral") != std::string::npos) out[i] = 3;
            else if (norm.find("disagree") != std::string::npos) out[i] = 2;
            else if (norm.find("strong disagree") != std::string::npos) out[i] = 1;
            else out[i] = 0;
        }
        return true;
    }

    /*-------------------------------------------------------------
       GetMetricColumn
    -------------------------------------------------------------*/
    std::vector<float> DataManager::GetMetricColumn(const std::string& metric) const
    {
        std::vector<float> out;
        out.reserve(respondents_.size());
        for (const auto& r : respondents_) {
            if (metric == "Exposure Score")       out.push_back(r.ExposureScore());
            else if (metric == "Normalization Score")  out.push_back(r.NormalizationScore());
            else if (metric == "Platform Attitude")    out.push_back(r.PlatformAttitude());
            else if (metric == "RealWorld Score")      out.push_back(r.RealWorldScore());
            else if (metric == "Total Score")          out.push_back(r.TotalScore());
            else                                       out.push_back(0.0f);
        }
        return out;
    }

    void DataManager::Clear() { respondents_.clear(); }

    // B-24: Returns the raw CSV column names used when writing pasted data.
    std::vector<std::string> DataManager::GetColumnNames()
    {
        return {
            "Timestamp", "Gender", "Age", "Education", "Hours",
            "Platform", "Content",
            "Q7","Q8","Q9","Q10",
            "Q11","Q12","Q13","Q14",
            "Q15","Q16","Q17r","Q18",
            "Q19","Q20","Q21"
        };
    }
    bool DataManager::AppendTable(const Table& table, std::string& outError)
    {
        if (table.size() < 2)
        {
            outError = "AppendTable: table is empty or has no data rows.";
            return false;
        }

        // Snapshot count so we can roll back on failure.
        const size_t beforeCount = respondents_.size();
        size_t rowIdx = respondents_.size(); // continue numbering from existing rows

        for (size_t i = 1; i < table.size(); ++i)
        {
            const auto& rawRow = table[i];
            ++rowIdx;

            // Build a case-insensitive value map — same as LoadTable.
            std::unordered_map<std::string, std::string> rowMap;
            for (const auto& [colName, val] : rawRow) {
                std::string norm = colName;
                std::transform(norm.begin(), norm.end(), norm.begin(), ::tolower);
                rowMap[norm] = val;
            }

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
            rec.timestamp = "R" + std::to_string(rowIdx);
            rec.gender = Normalise(getField("Gender"));
            rec.education = Normalise(getField("Education"));
            rec.hours = Normalise(getField("Hours"));
            rec.platform = Normalise(getField("Platform"));
            rec.ageGroup = Normalise(getField("Age"));
            rec.contentPref = Normalise(getField("Content"));

            static const std::array<const char*, 15> kLikertCols = {
                "Q7",  "Q8",  "Q9",  "Q10",
                "Q11", "Q12", "Q13", "Q14",
                "Q15", "Q16", "Q17r",
                "Q18", "Q19", "Q20", "Q21"
            };

            std::vector<std::string> likertTokens;
            likertTokens.reserve(15);
            for (const char* col : kLikertCols)
                likertTokens.emplace_back(getField(col));

            std::array<int, 15> likert{};
            if (!ParseLikert(likertTokens, 0, likert)) {
                // Roll back and report which row failed.
                respondents_.resize(beforeCount);
                outError = "AppendTable: row " + std::to_string(i)
                    + " has invalid Likert values — append cancelled.";
                return false;
            }
            rec.responses = likert;

            auto it1 = rawRow.find("Qualitative1");
            auto it2 = rawRow.find("Qualitative2");
            if (it1 != rawRow.end()) rec.qualitative1 = it1->second;
            if (it2 != rawRow.end()) rec.qualitative2 = it2->second;

            rec.ComputeAnalytics();
            respondents_.push_back(std::move(rec));
        }

        return true;
    }

} // namespace Statix