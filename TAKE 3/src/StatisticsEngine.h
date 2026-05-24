#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <sstream>
#include <cmath>
#include "SurveyRespondent.h"
#include "Stats.h"                    

namespace Statix {

    /**
     * StatisticsEngine – compute descriptive statistics, Pearson correlation, and
     * simple linear regression for any numeric metric extracted from a collection
     * of SurveyRespondent objects.
     */
    class StatisticsEngine
    {
    public:
        // -----------------------------------------------------------------
        // 1️⃣ Descriptive statistics 
        // -----------------------------------------------------------------
        template <typename ExtractFunc>
        static std::string Compute(const std::vector<SurveyRespondent>& data,
            ExtractFunc&& accessor,
            const std::string& metricName)
        {
            std::vector<float> values;
            values.reserve(data.size());
            for (const auto& r : data)
                values.push_back(static_cast<float>(accessor(r)));

            if (values.empty())
                return FormatTable(metricName, {}, true);

            std::vector<float> sorted = values;
            std::sort(sorted.begin(), sorted.end());

            const float mean = std::accumulate(values.begin(), values.end(), 0.0f) / static_cast<float>(values.size());
            const float median = ComputeMedian(sorted);

            // Calculate Sample Variance safely inline (B-11 Math alignment)
            float sumSqDiff = 0.0f;
            for (float v : values) {
                float diff = v - mean;
                sumSqDiff += diff * diff;
            }

            // Survey data benchmarks use Sample Variance (N - 1) when N > 1
            const size_t n = values.size();
            const float var = sumSqDiff / static_cast<float>(n);
            const float stddev = std::sqrt(var);

            const float minVal = sorted.front();
            const float maxVal = sorted.back();
            const float range = maxVal - minVal;

            Stats s{ mean, median, var, stddev, minVal, maxVal, range };
            return FormatTable(metricName, s, false);
        }

        // -----------------------------------------------------------------
        // 2️⃣ Pearson correlation between two metrics.
        // -----------------------------------------------------------------
        struct CorrelationResult {
            float r;               // Pearson coefficient
            std::string summary;   // Human‑readable, e.g. "r = 0.73"
        };

        template <typename ExtractFuncX, typename ExtractFuncY>
        static CorrelationResult PearsonCorrelation(const std::vector<SurveyRespondent>& data,
            ExtractFuncX&& xAccessor,
            ExtractFuncY&& yAccessor,
            const std::string& labelX = "X",
            const std::string& labelY = "Y")
        {
            std::vector<float> xs, ys;
            xs.reserve(data.size());
            ys.reserve(data.size());
            for (const auto& r : data) {
                xs.push_back(static_cast<float>(xAccessor(r)));
                ys.push_back(static_cast<float>(yAccessor(r)));
            }
            if (xs.empty() || ys.empty()) {
                return { 0.0f, "r = N/A (empty data)" };
            }

            // B-21: Delegate to shared Stats::Pearson for consistency
            const float r = Stats::Pearson(xs, ys);

            std::ostringstream oss;
            oss << "Pearson " << labelX << " vs " << labelY
                << " : r = " << std::fixed << std::setprecision(4) << r;
            return { r, oss.str() };
        }

        // -----------------------------------------------------------------
        // 3️⃣ Simple linear regression (Y = mX + c).
        // -----------------------------------------------------------------
        struct RegressionResult {
            float slope;          // m
            float intercept;      // c
            float rSquared;       // R^2
            std::string formula;  // e.g. "Y = 0.73X + 1.24"
            std::string summary;  // detailed text
        };

        template <typename ExtractFuncX, typename ExtractFuncY>
        static RegressionResult LinearRegression(const std::vector<SurveyRespondent>& data,
            ExtractFuncX&& xAccessor,
            ExtractFuncY&& yAccessor,
            const std::string& labelX = "X",
            const std::string& labelY = "Y")
        {
            std::vector<float> xs, ys;
            xs.reserve(data.size());
            ys.reserve(data.size());
            for (const auto& r : data) {
                xs.push_back(static_cast<float>(xAccessor(r)));
                ys.push_back(static_cast<float>(yAccessor(r)));
            }
            if (xs.empty() || ys.empty()) {
                return { 0.0f, 0.0f, 0.0f, "Y = N/A", "Insufficient data for regression" };
            }

            // B-21: Delegate to shared Stats::LinearRegression
            Stats::RegressionResult base = Stats::LinearRegression(xs, ys);

            std::ostringstream formulaOSS;
            formulaOSS << labelY << " = " << std::fixed << std::setprecision(4)
                << base.slope << " * " << labelX << " + " << base.intercept;

            std::ostringstream summaryOSS;
            summaryOSS << "Linear regression (" << labelY << " on " << labelX << "): "
                << "m=" << base.slope << ", c=" << base.intercept
                << ", R^2=" << base.rSquared;

            return { base.slope, base.intercept, base.rSquared,
                    formulaOSS.str(), summaryOSS.str() };
        }

    private:
        struct StatsInternal {
            float mean, median, variance, stddev, min, max, range;
        };

        static float ComputeMedian(const std::vector<float>& sorted)
        {
            size_t n = sorted.size();
            if (n == 0) return 0.0f;
            if (n % 2 == 1) return sorted[n / 2];
            return (sorted[n / 2 - 1] + sorted[n / 2]) * 0.5f;
        }

        // Pretty‑print an ASCII table for the descriptive stats.
        static std::string FormatTable(const std::string& metric,
            const StatsInternal& s,
            bool empty)
        {
            std::ostringstream out;
            const int wName = 25;
            const int wVal = 10;

            out << "+" << std::string(wName, '-') << "+" << std::string((wVal + 3) * 7 + 1, '-') << "+\n";
            out << "| " << std::left << std::setw(wName - 1) << metric << " |"
                << "   Mean   |  Median  | Variance |  StdDev  |   Min    |   Max    |  Range   |\n";
            out << "+" << std::string(wName, '-') << "+" << std::string((wVal + 3) * 7 + 1, '-') << "+\n";

            if (empty) {
                out << "| " << std::left << std::setw(wName - 1) << "" << " |";
                const std::string na = "N/A";
                for (int i = 0; i < 7; ++i)
                    out << " " << std::right << std::setw(wVal) << na << "   |";
                out << "\n";
            }
            else {
                out << "| " << std::left << std::setw(wName - 1) << "" << " |"
                    << " " << std::right << std::setw(wVal) << std::fixed << std::setprecision(3) << s.mean << "   |"
                    << " " << std::right << std::setw(wVal) << s.median << "   |"
                    << " " << std::right << std::setw(wVal) << s.variance << "   |"
                    << " " << std::right << std::setw(wVal) << s.stddev << "   |"
                    << " " << std::right << std::setw(wVal) << s.min << "   |"
                    << " " << std::right << std::setw(wVal) << s.max << "   |"
                    << " " << std::right << std::setw(wVal) << s.range << "   |\n";
            }
            out << "+" << std::string(wName, '-') << "+" << std::string((wVal + 3) * 7 + 1, '-') << "+";
            return out.str();
        }
    };

} // namespace Statix