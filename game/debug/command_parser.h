#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace altaria {

class CommandParser {
public:
    static std::string trim(const std::string &str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    static std::string to_lower(const std::string &str) {
        std::string out = str;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
            return (char)std::tolower(c);
        });
        return out;
    }

    // Tokenize raw command line with quote awareness (e.g. echo "hello world")
    static std::vector<std::string> tokenize(const std::string &line) {
        std::vector<std::string> tokens;
        std::string current;
        bool in_quotes = false;
        char quote_char = 0;

        for (size_t i = 0; i < line.size(); i++) {
            char c = line[i];
            if (in_quotes) {
                if (c == quote_char) {
                    in_quotes = false;
                    quote_char = 0;
                } else if (c == '\\' && i + 1 < line.size()) {
                    current += line[++i];
                } else {
                    current += c;
                }
            } else {
                if (c == '"' || c == '\'') {
                    in_quotes = true;
                    quote_char = c;
                } else if (std::isspace((unsigned char)c)) {
                    if (!current.empty()) {
                        tokens.push_back(current);
                        current.clear();
                    }
                } else {
                    current += c;
                }
            }
        }
        if (!current.empty()) {
            tokens.push_back(current);
        }
        return tokens;
    }

    static bool parse_bool(const std::string &str, bool &out) {
        std::string s = to_lower(trim(str));
        if (s == "on" || s == "true" || s == "1" || s == "yes" || s == "enable" || s == "enabled") {
            out = true;
            return true;
        }
        if (s == "off" || s == "false" || s == "0" || s == "no" || s == "disable" || s == "disabled") {
            out = false;
            return true;
        }
        return false;
    }

    static bool parse_int(const std::string &str, int64_t &out) {
        std::string s = trim(str);
        if (s.empty())
            return false;
        char *end = nullptr;
        errno = 0;
        long long val = std::strtoll(s.c_str(), &end, 10);
        if (errno != 0 || end == s.c_str() || *end != '\0')
            return false;
        out = (int64_t)val;
        return true;
    }

    static bool parse_float(const std::string &str, float &out) {
        std::string s = trim(str);
        if (s.empty())
            return false;
        char *end = nullptr;
        errno = 0;
        float val = std::strtof(s.c_str(), &end);
        if (errno != 0 || end == s.c_str() || *end != '\0' || !std::isfinite(val))
            return false;
        out = val;
        return true;
    }

    // Levenshtein / fuzzy distance for suggestions on typos
    static int levenshtein_distance(const std::string &s1, const std::string &s2) {
        size_t len1 = s1.size(), len2 = s2.size();
        std::vector<std::vector<int>> d(len1 + 1, std::vector<int>(len2 + 1));
        for (size_t i = 0; i <= len1; i++) d[i][0] = (int)i;
        for (size_t j = 0; j <= len2; j++) d[0][j] = (int)j;
        for (size_t i = 1; i <= len1; i++) {
            for (size_t j = 1; j <= len2; j++) {
                int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
                d[i][j] = std::min({ d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost });
            }
        }
        return d[len1][len2];
    }
};

} // namespace altaria
