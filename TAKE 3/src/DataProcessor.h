// d:\STATS APP\TAKE 3\TAKE 3\src\DataProcessor.h
#pragma once
#include <string>
#include <vector>

namespace Statix {

    class DataProcessor {
    public:
        DataProcessor() = default;
        ~DataProcessor() = default;

        // Parses a CSV string (first line = headers, subsequent lines = data rows)
        bool parse_csv(const std::string& csvContent);

        // Appends a new row; ignored if column count doesn't match headers
        void add_row(const std::vector<float>& row);

        // Returns column index by header name, or -1 if not found
        int get_column_index(const std::string& name) const;

        // Returns all values in the named column as a flat vector
        std::vector<float> get_column_values(const std::string& name) const;

        // Returns copy of header names
        const std::vector<std::string>& get_headers() const { return m_headers; }

        // Returns number of data rows
        size_t row_count() const { return m_dataRows.size(); }

        // Returns raw data rows (each row is a vector of float values)
        const std::vector<std::vector<float>>& get_rows() const { return m_dataRows; }

        // Cached score accessors
        float get_exposure_score() const { return m_exposureScore; }
        float get_normalization_score() const { return m_normalizationScore; }
        float get_platform_attitude() const { return m_platformAttitude; }
        float get_real_world_score() const { return m_realWorldScore; }
        float get_total_score() const { return m_totalScore; }
        float get_hours_group() const { return m_hoursGroup; }

        // --- Statistical Methods ---
        float calculate_mean(const std::vector<float>& values) const;
        float calculate_median(std::vector<float> values) const;  // by value: sorts a copy
        float calculate_variance(const std::vector<float>& values) const;

        // B-11: Added for consistency with Stats.h (population variance)
        float calculate_population_variance(const std::vector<float>& values) const;

    private:
        std::vector<std::string>         m_headers;
        std::vector<std::vector<float>>  m_dataRows;

        float m_exposureScore = 0.0f;
        float m_normalizationScore = 0.0f;
        float m_platformAttitude = 0.0f;
        float m_realWorldScore = 0.0f;
        float m_totalScore = 0.0f;
        float m_hoursGroup = 0.0f;

        std::string trim(const std::string& str) const;
    };

} // namespace Statix