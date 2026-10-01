#include "osc/OscCommandParser.h"
#include <cctype>
#include <cerrno>
#include <cstdlib>

namespace evobox
{

bool OscCommandParser::tokenize(const std::string& text, std::vector<std::string>& tokens,
                                std::vector<bool>& quoted, std::string& err)
{
    tokens.clear();
    quoted.clear();
    size_t i = 0, n = text.size();
    while (i < n)
    {
        while (i < n && std::isspace((unsigned char)text[i])) i++;
        if (i >= n) break;
        char c = text[i];
        if (c == '"' || c == '\'')
        {
            char q = c;
            i++;
            std::string tok;
            bool closed = false;
            while (i < n)
            {
                char d = text[i];
                if (d == '\\' && i + 1 < n) { tok.push_back(text[i + 1]); i += 2; continue; }
                if (d == q) { closed = true; i++; break; }
                tok.push_back(d);
                i++;
            }
            if (!closed) { err = "unterminated quote"; return false; }
            tokens.push_back(tok);
            quoted.push_back(true);
        }
        else
        {
            std::string tok;
            while (i < n && !std::isspace((unsigned char)text[i])) tok.push_back(text[i++]);
            tokens.push_back(tok);
            quoted.push_back(false);
        }
    }
    return true;
}

static bool parseIntStrict(const std::string& s, long long& out)
{
    if (s.empty()) return false;
    size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (i >= s.size()) return false;
    for (size_t k = i; k < s.size(); k++) if (!std::isdigit((unsigned char)s[k])) return false;
    errno = 0;
    char* end = nullptr;
    out = std::strtoll(s.c_str(), &end, 10);
    return errno == 0 && end && *end == 0;
}

static bool parseFloatStrict(const std::string& s, double& out)
{
    if (s.empty()) return false;
    // must contain a digit, and only float characters
    bool digit = false;
    for (char c : s)
    {
        if (std::isdigit((unsigned char)c)) digit = true;
        else if (c != '.' && c != '-' && c != '+' && c != 'e' && c != 'E') return false;
    }
    if (!digit) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end && *end == 0;
}

OscArg OscCommandParser::inferArg(const std::string& tokIn, bool quoted)
{
    if (quoted) return OscArg::Str(tokIn);
    const std::string& tok = tokIn;

    // explicit prefixes
    if (tok.size() >= 2 && tok[1] == ':')
    {
        std::string rest = tok.substr(2);
        long long ll; double d;
        switch (tok[0])
        {
        case 'i': return parseIntStrict(rest, ll) ? OscArg::Int((int32_t)ll) : OscArg::Str(rest);
        case 'f': return parseFloatStrict(rest, d) ? OscArg::Float((float)d) : OscArg::Str(rest);
        case 'd': return parseFloatStrict(rest, d) ? OscArg::Double(d) : OscArg::Str(rest);
        case 'h': return parseIntStrict(rest, ll) ? OscArg::Int64(ll) : OscArg::Str(rest);
        case 's': return OscArg::Str(rest);
        case 'b':
        {
            std::string lo; for (char c : rest) lo.push_back((char)std::tolower((unsigned char)c));
            return OscArg::Bool(lo == "true" || lo == "1" || lo == "on" || lo == "yes");
        }
        default: break;
        }
    }

    std::string lo; for (char c : tok) lo.push_back((char)std::tolower((unsigned char)c));
    if (lo == "true")  return OscArg::Bool(true);
    if (lo == "false") return OscArg::Bool(false);

    long long ll;
    if (parseIntStrict(tok, ll))
    {
        if (ll >= INT32_MIN && ll <= INT32_MAX) return OscArg::Int((int32_t)ll);
        return OscArg::Int64(ll);
    }
    // 'f' suffix: 1f / 2.5f
    if (tok.size() >= 2 && (tok.back() == 'f' || tok.back() == 'F'))
    {
        double d;
        if (parseFloatStrict(tok.substr(0, tok.size() - 1), d)) return OscArg::Float((float)d);
    }
    double d;
    if (parseFloatStrict(tok, d)) return OscArg::Float((float)d);
    return OscArg::Str(tok);
}

ParsedCommand OscCommandParser::parse(const std::string& text)
{
    ParsedCommand r;
    std::vector<std::string> tokens;
    std::vector<bool> quoted;
    if (!tokenize(text, tokens, quoted, r.error)) return r;
    if (tokens.empty()) { r.error = "empty command"; return r; }
    if (quoted[0] || tokens[0].empty() || tokens[0][0] != '/') { r.error = "address must start with '/'"; return r; }
    const std::string& addr = tokens[0];
    for (char c : addr)
        if (c == '#' || (unsigned char)c < 0x20) { r.error = "invalid character in address"; return r; }
    if (addr.size() > 1 && addr.back() == '/') { r.error = "address must not end with '/'"; return r; }
    r.message.address = addr;
    for (size_t i = 1; i < tokens.size(); i++)
        r.message.args.push_back(inferArg(tokens[i], quoted[i]));
    r.ok = true;
    return r;
}

bool OscCommandParser::valid(const std::string& text, std::string* error)
{
    ParsedCommand p = parse(text);
    if (error) *error = p.error;
    return p.ok;
}

} // namespace evobox
