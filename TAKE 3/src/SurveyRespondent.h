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
       * Metadata (7 fields)        : timestamp, gender, ageGroup,
                                     education, hours, platform, contentPref
       * Likert responses (15)      : Q7 … Q21 (int 1‑5)
       * Computed scores (5 floats) : exposureScore,
                                     normalizationScore,
                                     platformAttitude,
                                     realWorldScore,
                                     totalScore
       * Qualitative text (2 blocks): qualitative1, qualitative2
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
            // ---- Normalise Hours string (replace UTF-8 en-dash with '-') ----
            std::string h = hours;
            const std::string enDash = "\xE2\x80\x93";
            size_t pos = 0;
            while ((pos = h.find(enDash, pos)) != std::string::npos) {
                h.replace(pos, enDash.length(), "-");
                ++pos;
            }

            // Helper lambda: mean of a slice of the response array.
            auto sliceMean = [&](size_t start, size_t count) -> float {
                float sum = 0.0f;
                for (size_t i = start; i < start + count; ++i)
                    sum += static_cast<float>(responses[i]);
                return sum / static_cast<float>(count);
                };

            exposureScore_ = sliceMean(0, 4);   // Q7 -Q10
            normalizationScore_ = sliceMean(4, 4);   // Q11-Q14
            platformAttitude_ = sliceMean(8, 4);   // Q15-Q18
            realWorldScore_ = sliceMean(12, 3);   // Q19-Q21
            totalScore_ = sliceMean(0, 15);  // all 15
        }
        // --------------------------------------------------------------------
        // Public accessor methods (used by UI / renderer)
        // --------------------------------------------------------------------
        float ExposureScore()        const { return exposureScore_; }
        float NormalizationScore()   const { return normalizationScore_; }
        float PlatformAttitude()    const { return platformAttitude_; }
        float RealWorldScore()      const { return realWorldScore_; }
        float TotalScore()          const { return totalScore_; }
    };

    /*=====================================================================
       Tiny helper to dump a respondent to the console (useful for debugging)
    =====================================================================*/
    inline void PrintRespondent(const SurveyRespondent& r)
    {
        std::printf("=== %s ===\n", r.timestamp.c_str());
        std::printf("Gender: %s | Age: %s | Edu: %s | Hours: %s | Platform: %s\n",
            r.gender.c_str(),
            r.ageGroup.c_str(),
            r.education.c_str(),
            r.hours.c_str(),
            r.platform.c_str());
        std::printf("Exposure: %.3f  Normalization: %.3f  Platform: %.3f  RealWorld: %.3f  Total: %.3f\n",
            r.ExposureScore(),
            r.NormalizationScore(),
            r.PlatformAttitude(),
            r.RealWorldScore(),
            r.TotalScore());
        std::printf("Qual1: %s\nQual2: %s\n---\n",
            r.qualitative1.c_str(),
            r.qualitative2.c_str());
    }

} // namespace Statix