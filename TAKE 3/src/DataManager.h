//// d:\STATS APP\TAKE 3\TAKE 3\src\DataManager.h
//#pragma once
//
//#include <string>
//#include <vector>
//#include <array>
//#include "SurveyRespondent.h"
//#include "FileImporter.h"
//
//namespace Statix {
//
//    class DataManager
//    {
//    public:
//        DataManager() = default;
//        ~DataManager() = default;
//
//        // Load a raw Table (produced by FileImporter). Returns true on success.
//        // On failure *outError* receives a detailed message.
//        bool LoadTable(const Table& raw, std::string& outError);
//
//        // Remove all data (called when a new file is loaded).
//        void Clear();
//
//        // -----------------------------------------------------------------
//        // Query helpers used by the UI / statistics engine
//        // -----------------------------------------------------------------
//        const std::vector<SurveyRespondent>& GetRespondents() const { return respondents_; }
//        size_t RowCount() const { return respondents_.size(); }
//
//        // Return a vector of floats for one of the five computed metrics.
//        // Accepted metric names are exactly those returned by GetMetricNames().
//        std::vector<float> GetMetricColumn(const std::string& metric) const;
//
//        // List of metric names – used to populate ImGui combo‑boxes.
//        static std::vector<std::string> GetMetricNames()
//        {
//            return {
//                "Exposure Score",
//                "Normalization Score",
//                "Platform Attitude",
//                "RealWorld Score",
//                "Total Score"
//            };
//        }
//
//        // B-24: Added for consistency with application.cpp fix
//        std::vector<std::string> GetColumnNames() const;
//
//    private:
//        std::vector<SurveyRespondent> respondents_;
//
//        // Helpers ------------------------------------------------------------
//        static std::string Normalise(const std::string& s);
//        static bool ParseLikert(const std::vector<std::string>& tokens,
//            size_t startIdx,
//            std::array<int, 15>& out);
//    };
//
//} // namespace Statix
// d:\STATS APP\TAKE 3\TAKE 3\src\DataManager.h
#pragma once

#include <string>
#include <vector>
#include <array>
#include "SurveyRespondent.h"
#include "FileImporter.h"

namespace Statix {

    class DataManager
    {
    public:
        DataManager() = default;
        ~DataManager() = default;

        // Load a raw Table (produced by FileImporter). Returns true on success.
        // On failure *outError* receives a detailed message.
        bool LoadTable(const Table& raw, std::string& outError);

        // Remove all data (called when a new file is loaded).
        void Clear();

        // -----------------------------------------------------------------
        // Query helpers used by the UI / statistics engine
        // -----------------------------------------------------------------
        const std::vector<SurveyRespondent>& GetRespondents() const { return respondents_; }
        size_t RowCount() const { return respondents_.size(); }

        // Return a vector of floats for one of the five computed metrics.
        std::vector<float> GetMetricColumn(const std::string& metric) const;

        // List of metric names – used to populate ImGui combo‑boxes.
        static std::vector<std::string> GetMetricNames()
        {
            return {
                "Exposure Score",
                "Normalization Score",
                "Platform Attitude",
                "RealWorld Score",
                "Total Score"
            };
        }

        // B-24 FIX: Made static so it can be called as DataManager::GetColumnNames()
        static std::vector<std::string> GetColumnNames();
        bool AppendTable(const Table& table, std::string& outError);
    private:
        bool ParseRow(const std::vector<std::string>& row,
            SurveyRespondent& out,
            std::string& outError);
        std::vector<SurveyRespondent> respondents_;

        // Helpers ------------------------------------------------------------
        static std::string Normalise(const std::string& s);
        static bool ParseLikert(const std::vector<std::string>& tokens,
            size_t startIdx,
            std::array<int, 15>& out);
    };

} // namespace Statix