// OscCommandParser.h — grammar of the command text field (unit-tested):
//   "/address arg arg ..."
//   - the address must start with '/' and contain no whitespace
//   - args split on whitespace; quoted strings allowed ("hello world", 'single' too), \" escapes
//   - type inference: integer literal -> i, literal with '.'/exponent or 'f' suffix -> f,
//     true/false -> T/F, everything else -> s
//   - explicit prefixes override inference: i:12  f:1  s:42  b:true (bool)  h:<int64>  d:<double>
#pragma once

#include <string>
#include "osc/OscMessage.h"

namespace evobox
{

struct ParsedCommand
{
    bool ok = false;
    std::string error;   // "" when ok
    OscMessage message;
};

class OscCommandParser
{
public:
    static ParsedCommand parse(const std::string& text);
    // quick validity check used by the UI for the red-row state
    static bool valid(const std::string& text, std::string* error = nullptr);
    // tokenizer exposed for tests
    static bool tokenize(const std::string& text, std::vector<std::string>& tokens, std::vector<bool>& quoted, std::string& err);
    static OscArg inferArg(const std::string& token, bool quoted);
};

} // namespace evobox
