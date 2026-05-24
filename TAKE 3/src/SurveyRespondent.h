// d:\STATS APP\TAKE 3\TAKE 3\src\SurveyRespondent.h
#pragma once
#include <cstdio>
#include <string>
#include <array>
#include <algorithm>

namespace Statix {

    /*=====================================================================
       SurveyRespondent – one row of the survey.
       ------------------------------------------------------------------
       responses[0..14] map to Q7, Q8, Q9, Q10, Q11, Q12, Q13, Q14,
                                  Q15, Q16, Q17r, Q18, Q19, Q20, Q21
       (index 10 = Q17r, reverse-coded item)
    =====================================================================*/
    struct SurveyRespondent {
        std::string timestamp;
        std::string gender;
        std::string ageGroup;
        std::string education;
        std::string hours;
        std::string platform;
        std::string contentPref;

        std::array<int, 15> responses{};     // Q7 to Q21

        float exposureScore_ = 0.0f;
        float normalizationScore_ = 0.0f;
        float platformAttitude_ = 0.0f;
        float realWorldScore_ = 0.0f;
        float totalScore_ = 0.0f;

        std::string qualitative1;
        std::string qualitative2;

        void ComputeAnalytics()
        {
            // ---- Normalise Hours string (replace UTF-8 en-dash) ----
            std::string h = hours;
            const std::string enDash = "\xE2\x80\x93";
            size_t pos = 0;
            while ((pos = h.find(enDash, pos)) != std::string::npos) {
                h.replace(pos, enDash.length(), "-");
                ++pos;
            }

            // B-CRIT-2 FIX: Q17r is reverse-coded (negatively-worded item).
            // Sociological convention: reversed = 6 - raw, so that a higher
            // value always means MORE normalization of hate speech, matching
            // the direction of Q11–Q14 and Q15–Q18 before averaging.
            // responses[10] is Q17r (index 10 in the 15-item array: Q7=0…Q17r=10…Q21=14).
            // We operate on a local copy so the stored raw value is preserved
            // for audit/display purposes in the Raw Matrix tab.
            std::array<int, 15> r = responses;
            r[10] = 6 - r[10];   // reverse Q17r: 1↔5, 2↔4, 3=3

            // Guard against out-of-range values after reversal (shouldn't
            // happen if ParseLikert is correct, but be defensive).
            if (r[10] < 1 || r[10] > 5) r[10] = 3;

            // Helper: mean of a slice of the (reversed) response array.
            auto sliceMean = [&](size_t start, size_t count) -> float {
                float sum = 0.0f;
                for (size_t i = start; i < start + count; ++i)
                    sum += static_cast<float>(r[i]);
                return sum / static_cast<float>(count);
                };

            exposureScore_ = sliceMean(0, 4);   // Q7 –Q10  (indices 0–3)
            normalizationScore_ = sliceMean(4, 4);   // Q11–Q14  (indices 4–7)
            platformAttitude_ = sliceMean(8, 4);   // Q15–Q18  (indices 8–11, Q17r reversed)
            realWorldScore_ = sliceMean(12, 3);   // Q19–Q21  (indices 12–14)
            totalScore_ = sliceMean(0, 15);  // all 15 items
        }

        float ExposureScore()      const { return exposureScore_; }
        float NormalizationScore() const { return normalizationScore_; }
        float PlatformAttitude()   const { return platformAttitude_; }
        float RealWorldScore()     const { return realWorldScore_; }
        float TotalScore()         const { return totalScore_; }
    };

    /*=====================================================================
       Debug helper
    =====================================================================*/
    inline void PrintRespondent(const SurveyRespondent& r)
    {
        std::printf("=== %s ===\n", r.timestamp.c_str());
        std::printf("Gender: %s | Age: %s | Edu: %s | Hours: %s | Platform: %s\n",
            r.gender.c_str(), r.ageGroup.c_str(), r.education.c_str(),
            r.hours.c_str(), r.platform.c_str());
        std::printf("Exposure: %.3f  Norm: %.3f  Platform: %.3f  RealWorld: %.3f  Total: %.3f\n",
            r.ExposureScore(), r.NormalizationScore(),
            r.PlatformAttitude(), r.RealWorldScore(), r.TotalScore());
        std::printf("Qual1: %s\nQual2: %s\n---\n",
            r.qualitative1.c_str(), r.qualitative2.c_str());
    }

} // namespace Statix