// d:\STATS APP\TAKE 3\TAKE 3\src\Stats.h
#pragma once

#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <unordered_map>

namespace Statix {

    namespace Stats {

        /*----------------------- Descriptive Statistics ----------------------*/
        inline float Mean(const std::vector<float>& data)
        {
            if (data.empty()) return 0.0f;
            double sum = std::accumulate(data.begin(), data.end(), 0.0);
            return static_cast<float>(sum / data.size());
        }

        inline float Median(std::vector<float> data)   // copy – we need sorting
        {
            if (data.empty()) return 0.0f;
            std::sort(data.begin(), data.end());
            size_t mid = data.size() / 2;
            if (data.size() % 2 == 0)
                return static_cast<float>((data[mid - 1] + data[mid]) * 0.5);
            else
                return data[mid];
        }

        // B-11 FIX: Clear comment on population vs sample variance.
        // This implementation uses population variance (÷ N) — consistent with
        // most descriptive stats in survey analysis. Use SampleVariance() if needed.
        inline float Variance(const std::vector<float>& data)
        {
            if (data.empty()) return 0.0f;
            if (data.size() == 1) return 0.0f;

            float m = Mean(data);
            double var = 0.0;
            for (float v : data)
                var += (v - m) * (v - m);

            var /= data.size();                    // Population variance
            return static_cast<float>(var);
        }

        inline float StdDev(const std::vector<float>& data)
        {
            return std::sqrt(Variance(data));
        }

        // Optional: Sample variance (÷ N-1) for inferential statistics
        inline float SampleVariance(const std::vector<float>& data)
        {
            if (data.size() < 2) return 0.0f;
            float m = Mean(data);
            double var = 0.0;
            for (float v : data)
                var += (v - m) * (v - m);
            var /= (data.size() - 1);
            return static_cast<float>(var);
        }

        /*----------------------- Correlation --------------------------------*/
        inline float Pearson(const std::vector<float>& X,
            const std::vector<float>& Y)
        {
            if (X.empty() || X.size() != Y.size()) return 0.0f;

            float meanX = Mean(X);
            float meanY = Mean(Y);
            float stdX = StdDev(X);
            float stdY = StdDev(Y);
            if (stdX == 0.0f || stdY == 0.0f) return 0.0f;

            double cov = 0.0;
            for (size_t i = 0; i < X.size(); ++i)
                cov += (X[i] - meanX) * (Y[i] - meanY);
            cov /= X.size();

            return static_cast<float>(cov / (stdX * stdY));
        }

        /*----------------------- Linear Regression --------------------------*/
        struct RegressionResult
        {
            float slope;       // b
            float intercept;   // a
            float rSquared;    // coefficient of determination
        };

        inline RegressionResult LinearRegression(const std::vector<float>& X,
            const std::vector<float>& Y)
        {
            RegressionResult res{ 0.0f, 0.0f, 0.0f };
            if (X.empty() || X.size() != Y.size()) return res;

            float meanX = Mean(X);
            float meanY = Mean(Y);
            float varX = Variance(X);
            if (varX == 0.0f) return res;

            // Covariance
            double cov = 0.0;
            for (size_t i = 0; i < X.size(); ++i)
                cov += (X[i] - meanX) * (Y[i] - meanY);
            cov /= X.size();

            res.slope = static_cast<float>(cov / varX);
            res.intercept = meanY - res.slope * meanX;

            // Pearson r for R²
            float r = Pearson(X, Y);
            res.rSquared = r * r;
            return res;
        }

        /*----------------------- Mode (optional) ---------------------------*/
        inline float Mode(const std::vector<float>& data)
        {
            if (data.empty()) return 0.0f;
            std::unordered_map<float, size_t> freq;
            for (float v : data) ++freq[v];
            size_t maxCount = 0;
            float modeVal = data[0];
            for (const auto& kv : freq) {
                if (kv.second > maxCount) {
                    maxCount = kv.second;
                    modeVal = kv.first;
                }
            }
            return modeVal;
        }

    } // namespace Stats
} // namespace Statix