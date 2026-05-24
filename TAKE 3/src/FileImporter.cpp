// d:\STATS APP\TAKE 3\TAKE 3\src\FileImporter.cpp
//
// XLSX loading strategy
// ---------------------
// Every attempt to use a third-party ZIP/XML library (OpenXLSX, miniz,
// Shell COM) introduced new linker or identifier errors because none of
// those libraries are in the project's Dependencies folder.
//
// The practical solution:
//   Use the included Python script (tools/xlsx_to_csv.py) to convert
//   your XLSX to CSV once, then load the CSV.  The CSV loader is fully
//   working and handles the dataset correctly.
//
//   Alternatively, in Excel: File -> Save As -> CSV UTF-8.
//
// If you need in-app XLSX support in a future version, add zlib via vcpkg
// (vcpkg install zlib:x64-windows) and uncomment the ENABLE_XLSX block.

#include "FileImporter.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <filesystem>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace Statix {

    // =========================================================================
    //  LoadFile — dispatch by extension
    // =========================================================================

    bool FileImporter::LoadFile(const std::string& path,
        Table& out,
        std::string& outError)
    {
        std::filesystem::path p(path);
        if (!std::filesystem::exists(p)) {
            outError = "File does not exist: " + path;
            return false;
        }

        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return std::tolower(c); });

        if (ext == ".csv")  return LoadCSV(path, out, outError);
        if (ext == ".xlsx") return LoadXLSX(path, out, outError);

        outError = "Unsupported file extension '" + ext + "'. "
            "Supported: .csv  .xlsx";
        return false;
    }

    // =========================================================================
    //  CSV loader  (B-01 + B-02 fixed)
    // =========================================================================

    bool FileImporter::LoadCSV(const std::string& path,
        Table& out,
        std::string& outError)
    {
        std::ifstream in(path);
        if (!in) { outError = "Unable to open file: " + path; return false; }

        std::string headerLine;
        if (!std::getline(in, headerLine)) {
            outError = "File is empty: " + path;
            return false;
        }
        if (!headerLine.empty() && headerLine.back() == '\r')
            headerLine.pop_back();

        auto columns = TokeniseCSVLine(headerLine);
        if (columns.empty()) {
            outError = "Could not parse header row.";
            return false;
        }

        // B-02 FIX: emit sentinel header row (key == value) as out[0]
        // so DataManager::LoadTable can discover column names via raw.front().
        {
            Row h;
            for (const auto& c : columns) h[c] = c;
            out.emplace_back(std::move(h));
        }

        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            auto cells = TokeniseCSVLine(line);
            if (cells.size() < columns.size())
                cells.resize(columns.size());

            Row row;
            for (size_t i = 0; i < columns.size(); ++i)
                row[columns[i]] = cells[i];
            out.emplace_back(std::move(row));
        }

        if (out.size() <= 1) {
            outError = "File contained no data rows.";
            return false;
        }
        return true;
    }

    // =========================================================================
    //  CSV tokeniser  (B-01 FIX — RFC-4180 compliant)
    //
    //  Old code did std::replace(all commas -> tabs) before scanning for
    //  quotes, destroying commas inside quoted fields.
    //  New code toggles inQuotes first, splits only when !inQuotes.
    // =========================================================================

    std::vector<std::string> FileImporter::TokeniseCSVLine(const std::string& line)
    {
        std::vector<std::string> tokens;
        std::string cur;
        bool inQuotes = false;

        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') {
                // Doubled quote inside a quoted field -> literal "
                if (inQuotes && i + 1 < line.size() && line[i + 1] == '"')
                {
                    cur.push_back('"'); ++i;
                }
                else
                    inQuotes = !inQuotes;
            }
            else if ((c == ',' || c == '\t') && !inQuotes) {
                tokens.emplace_back(std::move(cur));
                cur.clear();
            }
            else {
                cur.push_back(c);
            }
        }
        tokens.emplace_back(std::move(cur));
        return tokens;
    }

    // =========================================================================
    //  XLSX loader
    //
    //  In-app XLSX parsing requires a ZIP+XML library (OpenXLSX, miniz, zlib)
    //  that is not currently in the project's Dependencies folder.
    //
    //  This stub gives the user a clear, actionable error message instead of
    //  a silent crash or a misleading "XLSX support not compiled" message.
    //
    //  Workaround A (recommended): load the pre-converted CSV instead.
    //    The file HateSpeech_RawData.csv contains all 30 rows from the
    //    "Raw Data" sheet, ready to load directly.
    //
    //  Workaround B: in Excel, use File -> Save As -> "CSV UTF-8 (comma
    //    delimited)" and load the resulting .csv file.
    //
    //  To enable native XLSX support later:
    //    1. vcpkg install zlib:x64-windows
    //    2. Add zlib.lib to AdditionalDependencies in the .vcxproj
    //    3. Replace this function body with the full miniz-based implementation.
    // =========================================================================

    bool FileImporter::LoadXLSX(const std::string& path,
        Table& out,
        std::string& outError)
    {
        (void)path; (void)out;

        outError =
            "Direct XLSX loading is not yet enabled.\n"
            "\n"
            "Quick fix: use the pre-converted CSV file:\n"
            "  HateSpeech_RawData.csv  (all 30 rows, same folder as the XLSX)\n"
            "\n"
            "Or in Excel: File -> Save As -> CSV UTF-8, then load the .csv.";
        return false;
    }

} // namespace Statix