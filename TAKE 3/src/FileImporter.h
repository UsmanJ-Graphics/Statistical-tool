// d:\STATS APP\TAKE 3\TAKE 3\src\FileImporter.h
#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace Statix {

    /*-------------------------------------------------------------
       Row  : column name → cell value (UTF‑8 string)
       Table: vector of rows – the raw representation of a file.
     -------------------------------------------------------------*/
    using Row = std::unordered_map<std::string, std::string>;
    using Table = std::vector<Row>;

    class FileImporter
    {
    public:
        // Load a file (CSV or XLSX). On success returns true and fills *out*.
        // On failure returns false and writes a human‑readable message to *outError*.
        static bool LoadFile(const std::string& path,
            Table& out,
            std::string& outError);

    private:
        static bool LoadCSV(const std::string& path,
            Table& out,
            std::string& outError);
        static bool LoadXLSX(const std::string& path,
            Table& out,
            std::string& outError);

        // Very small CSV tokenizer – respects quoted fields.
        static std::vector<std::string> TokeniseCSVLine(const std::string& line);
    };

} // namespace Statix