// OscMessage.h — OSC 1.0 message model + binary encoder (no external library).
// Supported argument tags: i (int32), f (float32), s (string), T/F (bool), b (blob), h (int64), d (double).
#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace evobox
{

struct OscBlob { std::vector<uint8_t> data; };

struct OscArg
{
    enum class Type { Int32, Float32, String, True, False, Blob, Int64, Double };
    Type type = Type::Int32;
    int32_t i = 0;
    float f = 0.f;
    std::string s;
    OscBlob blob;
    int64_t h = 0;
    double d = 0.0;

    static OscArg Int(int32_t v)            { OscArg a; a.type = Type::Int32; a.i = v; return a; }
    static OscArg Float(float v)            { OscArg a; a.type = Type::Float32; a.f = v; return a; }
    static OscArg Str(const std::string& v) { OscArg a; a.type = Type::String; a.s = v; return a; }
    static OscArg Bool(bool v)              { OscArg a; a.type = v ? Type::True : Type::False; return a; }
    static OscArg Int64(int64_t v)          { OscArg a; a.type = Type::Int64; a.h = v; return a; }
    static OscArg Double(double v)          { OscArg a; a.type = Type::Double; a.d = v; return a; }
    static OscArg Blob(std::vector<uint8_t> v) { OscArg a; a.type = Type::Blob; a.blob.data = std::move(v); return a; }

    char tag() const;
    std::string toString() const; // human readable ("1", "0.500000", "\"hello world\"", "true")
};

struct OscMessage
{
    std::string address;      // must start with '/'
    std::vector<OscArg> args;

    std::string typeTags() const; // ",ifs"
    // Encodes to a complete OSC packet (address, type tags, args, 4-byte padding).
    std::vector<uint8_t> encode() const;
    // Human readable "/address 1 0.5 \"str\""
    std::string toString() const;
};

// OSC 1.0 helpers
void oscWriteString(std::vector<uint8_t>& out, const std::string& s);  // padded to 4
void oscWriteInt32(std::vector<uint8_t>& out, int32_t v);              // big endian
void oscWriteFloat32(std::vector<uint8_t>& out, float v);
void oscWriteInt64(std::vector<uint8_t>& out, int64_t v);
void oscWriteDouble(std::vector<uint8_t>& out, double v);
void oscWriteBlob(std::vector<uint8_t>& out, const std::vector<uint8_t>& b);

// Bundle of messages with an immediate time tag (used by "Test" and grouped sends later).
std::vector<uint8_t> oscEncodeBundle(const std::vector<OscMessage>& msgs, uint64_t timetag = 1);

} // namespace evobox
