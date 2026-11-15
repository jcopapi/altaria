#pragma once

#include "game/debug/command_parser.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace altaria {

inline void run_command_parser_tests() {
    std::cout << "[TEST] Running CommandParser tests...\n";

    // 1. Tokenization tests
    {
        auto t1 = CommandParser::tokenize("tp 100 64 -200");
        assert(t1.size() == 4);
        assert(t1[0] == "tp");
        assert(t1[1] == "100");
        assert(t1[2] == "64");
        assert(t1[3] == "-200");

        auto t2 = CommandParser::tokenize("echo \"hello world\" 123");
        assert(t2.size() == 3);
        assert(t2[0] == "echo");
        assert(t2[1] == "hello world");
        assert(t2[2] == "123");

        auto t3 = CommandParser::tokenize("  paint   #FF5500   0.5   1.2  ");
        assert(t3.size() == 4);
        assert(t3[0] == "paint");
        assert(t3[1] == "#FF5500");
        assert(t3[2] == "0.5");
        assert(t3[3] == "1.2");

        auto t4 = CommandParser::tokenize("");
        assert(t4.empty());

        auto t5 = CommandParser::tokenize("shape at 10 20 30 \"vertical_slab\"");
        assert(t5.size() == 6);
        assert(t5[5] == "vertical_slab");
    }

    // 2. Boolean parsing tests
    {
        bool val = false;
        assert(CommandParser::parse_bool("on", val) && val == true);
        assert(CommandParser::parse_bool("true", val) && val == true);
        assert(CommandParser::parse_bool("1", val) && val == true);
        assert(CommandParser::parse_bool("yes", val) && val == true);
        assert(CommandParser::parse_bool("enable", val) && val == true);
        assert(CommandParser::parse_bool("enabled", val) && val == true);

        assert(CommandParser::parse_bool("off", val) && val == false);
        assert(CommandParser::parse_bool("false", val) && val == false);
        assert(CommandParser::parse_bool("0", val) && val == false);
        assert(CommandParser::parse_bool("no", val) && val == false);
        assert(CommandParser::parse_bool("disable", val) && val == false);
        assert(CommandParser::parse_bool("disabled", val) && val == false);

        assert(!CommandParser::parse_bool("maybe", val));
        assert(!CommandParser::parse_bool("", val));
    }

    // 3. Integer and Float parsing tests
    {
        int64_t ival = 0;
        assert(CommandParser::parse_int("123456", ival) && ival == 123456);
        assert(CommandParser::parse_int("-9876", ival) && ival == -9876);
        assert(CommandParser::parse_int("0", ival) && ival == 0);
        assert(!CommandParser::parse_int("12abc", ival));
        assert(!CommandParser::parse_int("", ival));

        float fval = 0.0f;
        assert(CommandParser::parse_float("3.1415", fval) && std::abs(fval - 3.1415f) < 0.0001f);
        assert(CommandParser::parse_float("-42.5", fval) && std::abs(fval - (-42.5f)) < 0.0001f);
        assert(CommandParser::parse_float("100", fval) && std::abs(fval - 100.0f) < 0.0001f);
        assert(!CommandParser::parse_float("xyz", fval));
        assert(!CommandParser::parse_float("", fval));
    }

    // 4. Levenshtein distance tests
    {
        assert(CommandParser::levenshtein_distance("kitten", "sitting") == 3);
        assert(CommandParser::levenshtein_distance("fly", "fly") == 0);
        assert(CommandParser::levenshtein_distance("fly", "fli") == 1);
        assert(CommandParser::levenshtein_distance("noclip", "noclp") == 1);
        assert(CommandParser::levenshtein_distance("biome", "biom") == 1);
    }

    std::cout << "[PASS] CommandParser tests passed successfully!\n";
}

} // namespace altaria
