#include "osc/OscMessage.h"
#include <cstdio>
#include <cstring>

namespace evobox
{

char OscArg::tag() const
{
    switch (type)
    {
    case Type::Int32:   return 'i';
    case Type::Float32: return 'f';
    case Type::String:  return 's';
    case Type::True:    return 'T';
    case Type::False:   return 'F';
    case Type::Blob:    return 'b';
    case Type::Int64:   return 'h';
    case Type::Double:  return 'd';
    }
    return 'i';
}

std::string OscArg::toString() const
{
    char buf[64];
    switch (type)
    {
    case Type::Int32:   snprintf(buf, sizeof(buf), "%d", i); return buf;
    case Type::Float32: snprintf(buf, sizeof(buf), "%g", (double)f); return buf;
    case Type::String:  return "\"" + s + "\"";
    case Type::True:    return "true";
    case Type::False:   return "false";
    case Type::Blob:    snprintf(buf, sizeof(buf), "<blob %zu B>", blob.data.size()); return buf;
    case Type::Int64:   snprintf(buf, sizeof(buf), "%lld", (long long)h); return buf;
    case Type::Double:  snprintf(buf, sizeof(buf), "%g", d); return buf;
    }
    return "";
}

static void pad4(std::vector<uint8_t>& out)
{
    while (out.size() % 4) out.push_back(0);
}

void oscWriteString(std::vector<uint8_t>& out, const std::string& s)
{
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
    pad4(out);
}

void oscWriteInt32(std::vector<uint8_t>& out, int32_t v)
{
    uint32_t u = (uint32_t)v;
    out.push_back((uint8_t)(u >> 24)); out.push_back((uint8_t)(u >> 16));
    out.push_back((uint8_t)(u >> 8));  out.push_back((uint8_t)u);
}

void oscWriteFloat32(std::vector<uint8_t>& out, float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    oscWriteInt32(out, (int32_t)u);
}

void oscWriteInt64(std::vector<uint8_t>& out, int64_t v)
{
    uint64_t u = (uint64_t)v;
    for (int sh = 56; sh >= 0; sh -= 8) out.push_back((uint8_t)(u >> sh));
}

void oscWriteDouble(std::vector<uint8_t>& out, double v)
{
    uint64_t u;
    memcpy(&u, &v, 8);
    oscWriteInt64(out, (int64_t)u);
}

void oscWriteBlob(std::vector<uint8_t>& out, const std::vector<uint8_t>& b)
{
    oscWriteInt32(out, (int32_t)b.size());
    out.insert(out.end(), b.begin(), b.end());
    pad4(out);
}

std::string OscMessage::typeTags() const
{
    std::string t = ",";
    for (auto& a : args) t.push_back(a.tag());
    return t;
}

std::vector<uint8_t> OscMessage::encode() const
{
    std::vector<uint8_t> out;
    out.reserve(64);
    oscWriteString(out, address);
    oscWriteString(out, typeTags());
    for (auto& a : args)
    {
        switch (a.type)
        {
        case OscArg::Type::Int32:   oscWriteInt32(out, a.i); break;
        case OscArg::Type::Float32: oscWriteFloat32(out, a.f); break;
        case OscArg::Type::String:  oscWriteString(out, a.s); break;
        case OscArg::Type::True:
        case OscArg::Type::False:   break; // no payload
        case OscArg::Type::Blob:    oscWriteBlob(out, a.blob.data); break;
        case OscArg::Type::Int64:   oscWriteInt64(out, a.h); break;
        case OscArg::Type::Double:  oscWriteDouble(out, a.d); break;
        }
    }
    return out;
}

std::string OscMessage::toString() const
{
    std::string s = address;
    for (auto& a : args) { s += " "; s += a.toString(); }
    return s;
}

std::vector<uint8_t> oscEncodeBundle(const std::vector<OscMessage>& msgs, uint64_t timetag)
{
    std::vector<uint8_t> out;
    oscWriteString(out, "#bundle");
    oscWriteInt64(out, (int64_t)timetag);
    for (auto& m : msgs)
    {
        auto bytes = m.encode();
        oscWriteInt32(out, (int32_t)bytes.size());
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    return out;
}

} // namespace evobox
