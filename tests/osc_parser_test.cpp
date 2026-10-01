#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "osc/OscCommandParser.h"
#include "osc/OscMessage.h"

using namespace evobox;

static std::vector<uint8_t> bytes(std::initializer_list<int> l)
{
    std::vector<uint8_t> v;
    for (int x : l) v.push_back((uint8_t)x);
    return v;
}

TEST_CASE("parser: address only")
{
    auto p = OscCommandParser::parse("/sound/trigger");
    REQUIRE(p.ok);
    CHECK(p.message.address == "/sound/trigger");
    CHECK(p.message.args.empty());
    CHECK(p.message.typeTags() == ",");
}

TEST_CASE("parser: type inference")
{
    auto p = OscCommandParser::parse("/a 1 -7 2.5 1e3 3f true FALSE hello \"hello world\" 'q x' 12345678901");
    REQUIRE(p.ok);
    REQUIRE(p.message.args.size() == 11);
    CHECK(p.message.args[0].type == OscArg::Type::Int32);   CHECK(p.message.args[0].i == 1);
    CHECK(p.message.args[1].type == OscArg::Type::Int32);   CHECK(p.message.args[1].i == -7);
    CHECK(p.message.args[2].type == OscArg::Type::Float32); CHECK(p.message.args[2].f == doctest::Approx(2.5f));
    CHECK(p.message.args[3].type == OscArg::Type::Float32); CHECK(p.message.args[3].f == doctest::Approx(1000.f));
    CHECK(p.message.args[4].type == OscArg::Type::Float32); CHECK(p.message.args[4].f == doctest::Approx(3.f));
    CHECK(p.message.args[5].type == OscArg::Type::True);
    CHECK(p.message.args[6].type == OscArg::Type::False);
    CHECK(p.message.args[7].type == OscArg::Type::String);  CHECK(p.message.args[7].s == "hello");
    CHECK(p.message.args[8].type == OscArg::Type::String);  CHECK(p.message.args[8].s == "hello world");
    CHECK(p.message.args[9].type == OscArg::Type::String);  CHECK(p.message.args[9].s == "q x");
    CHECK(p.message.args[10].type == OscArg::Type::Int64);  CHECK(p.message.args[10].h == 12345678901LL);
    CHECK(p.message.typeTags() == ",iifffTFsssh");
}

TEST_CASE("parser: explicit prefixes override inference")
{
    auto p = OscCommandParser::parse("/x i:12 f:1 s:42 b:true b:0 d:2.5 h:7");
    REQUIRE(p.ok);
    REQUIRE(p.message.args.size() == 7);
    CHECK(p.message.args[0].type == OscArg::Type::Int32);   CHECK(p.message.args[0].i == 12);
    CHECK(p.message.args[1].type == OscArg::Type::Float32); CHECK(p.message.args[1].f == doctest::Approx(1.f));
    CHECK(p.message.args[2].type == OscArg::Type::String);  CHECK(p.message.args[2].s == "42");
    CHECK(p.message.args[3].type == OscArg::Type::True);
    CHECK(p.message.args[4].type == OscArg::Type::False);
    CHECK(p.message.args[5].type == OscArg::Type::Double);  CHECK(p.message.args[5].d == doctest::Approx(2.5));
    CHECK(p.message.args[6].type == OscArg::Type::Int64);   CHECK(p.message.args[6].h == 7);
}

TEST_CASE("parser: quoted numbers stay strings, escapes work")
{
    auto p = OscCommandParser::parse("/x \"1\" \"say \\\"hi\\\"\"");
    REQUIRE(p.ok);
    REQUIRE(p.message.args.size() == 2);
    CHECK(p.message.args[0].type == OscArg::Type::String); CHECK(p.message.args[0].s == "1");
    CHECK(p.message.args[1].s == "say \"hi\"");
}

TEST_CASE("parser: errors")
{
    std::string err;
    CHECK_FALSE(OscCommandParser::valid("", &err));
    CHECK_FALSE(OscCommandParser::valid("   ", &err));
    CHECK_FALSE(OscCommandParser::valid("sound/trigger 1", &err));
    CHECK(err.find("'/'") != std::string::npos);
    CHECK_FALSE(OscCommandParser::valid("/x \"unterminated", &err));
    CHECK_FALSE(OscCommandParser::valid("/x/", &err));
    CHECK(OscCommandParser::valid("/", &err)); // root address is legal
    CHECK(OscCommandParser::valid("  /light/flash   1  ", &err));
}

TEST_CASE("encoder: OSC 1.0 bytes")
{
    OscMessage m;
    m.address = "/oscillator/4/frequency";
    m.args.push_back(OscArg::Float(440.0f));
    auto b = m.encode();
    // From the OSC 1.0 spec example
    std::vector<uint8_t> expect = bytes({
        '/', 'o', 's', 'c', 'i', 'l', 'l', 'a', 't', 'o', 'r', '/', '4', '/', 'f', 'r', 'e', 'q', 'u', 'e', 'n', 'c', 'y', 0,
        ',', 'f', 0, 0,
        0x43, 0xdc, 0, 0 });
    CHECK(b == expect);
    CHECK(b.size() % 4 == 0);
}

TEST_CASE("encoder: mixed args and padding")
{
    OscMessage m;
    m.address = "/foo";
    m.args = { OscArg::Int(1000), OscArg::Int(-1), OscArg::Str("hello"), OscArg::Float(1.234f), OscArg::Float(5.678f) };
    auto b = m.encode();
    std::vector<uint8_t> expect = bytes({
        '/', 'f', 'o', 'o', 0, 0, 0, 0,
        ',', 'i', 'i', 's', 'f', 'f', 0, 0,
        0, 0, 0x03, 0xe8,
        0xff, 0xff, 0xff, 0xff,
        'h', 'e', 'l', 'l', 'o', 0, 0, 0,
        0x3f, 0x9d, 0xf3, 0xb6,
        0x40, 0xb5, 0xb2, 0x2d });
    CHECK(b == expect);
}

TEST_CASE("encoder: bools carry no payload; strings pad to 4 incl. terminator")
{
    OscMessage m;
    m.address = "/ab"; // 3 chars + NUL = 4, no extra padding
    m.args = { OscArg::Bool(true), OscArg::Bool(false), OscArg::Str("abcd") };
    auto b = m.encode();
    CHECK(b.size() == 4 + 8 /* ",TFs" + pad */ + 8 /* "abcd" + NUL + 3 pad */);
    CHECK(m.typeTags() == ",TFs");
    CHECK(m.toString() == "/ab true false \"abcd\"");
}

TEST_CASE("encoder: bundle")
{
    OscMessage a; a.address = "/a"; a.args = { OscArg::Int(1) };
    OscMessage b; b.address = "/b";
    auto bytesOut = oscEncodeBundle({ a, b });
    REQUIRE(bytesOut.size() >= 16);
    CHECK(std::string((const char*)bytesOut.data()) == "#bundle");
    CHECK(bytesOut[15] == 1); // immediate time tag
    CHECK(bytesOut.size() == 16 + 4 + a.encode().size() + 4 + b.encode().size());
}
