#include "DataProcessor.h"
#include "Stats.h"          // For consistent statistics implementation
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace Statix {

    // ============================================================================
    // CSV Parsing
    // ============================================================================

    bool DataProcessor::parse_csv(const std::string& csvContent) {
        m_headers.clear();
        m_dataRows.clear();

        std::stringstream ss(csvContent);
        std::string line;

        // Parse header row
        if (std::getline(ss, line)) {
            std::stringstream lineStream(line);
            std::string cell;
            while (std::getline(lineStream, cell, ',')) {
                m_headers.push_back(trim(cell));
            }
        }

        if (m_headers.empty()) return false;

        // Parse data rows
        while (std::getline(ss, line)) {
            line = trim(line);
            if (line.empty()) continue;

            std::stringstream lineStream(line);
            std::string cell;
            std::vector<float> row;

            while (std::getline(lineStream, cell, ',')) {
                try {
                    row.push_back(std::stof(trim(cell)));
                }
                catch (const std::exception&) {
                    row.push_back(0.0f);
                }
            }

            if (row.size() == m_headers.size()) {
                m_dataRows.push_back(row);
            }
        }

        return !m_dataRows.empty();
    }

    void DataProcessor::add_row(const std::vector<float>& row) {
        if (row.size() == m_headers.size()) {
            m_dataRows.push_back(row);
        }
    }

    // ============================================================================
    // Column Access
    // ============================================================================

    int DataProcessor::get_column_index(const std::string& name) const {
        for (size_t i = 0; i < m_headers.size(); ++i) {
            if (m_headers[i] == name) return static_cast<int>(i);
        }
        return -1;
    }

    std::vector<float> DataProcessor::get_column_values(const std::string& name) const {
        std::vector<float> values;
        int colIndex = get_column_index(name);
        if (colIndex == -1) return values;

        values.reserve(m_dataRows.size());
        for (const auto& row : m_dataRows) {
            values.push_back(row[colIndex]);
        }
        return values;
    }

    // ============================================================================
    // Statistics — Now delegates to shared Stats where possible (B-21)
    // ============================================================================

    float DataProcessor::calculate_mean(const std::vector<float>& values) const {
        return Stats::Mean(values);           // Use canonical implementation
    }

    float DataProcessor::calculate_median(std::vector<float> values) const {
        return Stats::Median(values);         // Use canonical implementation
    }

    float DataProcessor::calculate_variance(const std::vector<float>& values) const {
        // B-11 Note: Original used sample variance (Bessel's correction).
        // We keep original behavior here for backward compatibility with legacy UI paths.
        if (values.size() <= 1) return 0.0f;

        float mean = calculate_mean(values);
        float accum = 0.0f;
        for (float v : values) {
            accum += (v - mean) * (v - mean);
        }
        return accum / static_cast<float>(values.size() - 1); // Sample variance
    }

    // Optional: Add method using population variance for consistency with Stats.h
    float DataProcessor::calculate_population_variance(const std::vector<float>& values) const {
        return Stats::Variance(values);
    }

    // ============================================================================
    // Private Helpers
    // ============================================================================

    std::string DataProcessor::trim(const std::string& str) const {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, last - first + 1);
    }

} // namespace Statix
