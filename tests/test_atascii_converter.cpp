#include <catch2/catch_test_macros.hpp>
#include "sio/AtasciiConverter.h"
#include <cstdint>
#include <cstring>

// ── toAscii() single-byte mappings ────────────────────────────────────────────

TEST_CASE("AtasciiConverter: printable ASCII $20–$5A direct", "[atascii]") {
    // Space and basic printable range
    REQUIRE(AtasciiConverter::toAscii(0x20) == ' ');
    REQUIRE(AtasciiConverter::toAscii('A') == 'A');
    REQUIRE(AtasciiConverter::toAscii('Z') == 'Z');
    REQUIRE(AtasciiConverter::toAscii('0') == '0');
    REQUIRE(AtasciiConverter::toAscii('9') == '9');
    REQUIRE(AtasciiConverter::toAscii('!') == '!');
}

TEST_CASE("AtasciiConverter: lowercase $61–$7A direct", "[atascii]") {
    REQUIRE(AtasciiConverter::toAscii('a') == 'a');
    REQUIRE(AtasciiConverter::toAscii('z') == 'z');
    REQUIRE(AtasciiConverter::toAscii('m') == 'm');
}

TEST_CASE("AtasciiConverter: control codes", "[atascii]") {
    REQUIRE(AtasciiConverter::toAscii(0x09) == '\t'); // Tab
    REQUIRE(AtasciiConverter::toAscii(0x0A) == '\n'); // LF
    REQUIRE(AtasciiConverter::toAscii(0x0D) == '\r'); // CR
}

TEST_CASE("AtasciiConverter: other control codes map to space", "[atascii]") {
    // $00 → space
    REQUIRE(AtasciiConverter::toAscii(0x00) == ' ');
    // $01 → space
    REQUIRE(AtasciiConverter::toAscii(0x01) == ' ');
    // $1F → space
    REQUIRE(AtasciiConverter::toAscii(0x1F) == ' ');
}

TEST_CASE("AtasciiConverter: inverse-video $80–$9A strip bit", "[atascii]") {
    // $80 = inverse of $00 → space (same as $00)
    REQUIRE(AtasciiConverter::toAscii(0x80) == ' ');
    // $81 = inverse of $01 → space
    REQUIRE(AtasciiConverter::toAscii(0x81) == ' ');
    // $89 = inverse of $09 → space (not tab — control code range)
    // According to table: $80–$8F all map to ' '
    REQUIRE(AtasciiConverter::toAscii(0x89) == ' ');
    // $90–$9A also space
    REQUIRE(AtasciiConverter::toAscii(0x90) == ' ');
    REQUIRE(AtasciiConverter::toAscii(0x9A) == ' ');
}

TEST_CASE("AtasciiConverter: $9B (ATASCII EOL) maps to '?'", "[atascii]") {
    // $9B is the EOL marker; the table maps it to '?' since LineAssembler strips it
    REQUIRE(AtasciiConverter::toAscii(0x9B) == '?');
}

TEST_CASE("AtasciiConverter: $9C–$9F map to '?'", "[atascii]") {
    REQUIRE(AtasciiConverter::toAscii(0x9C) == '?');
    REQUIRE(AtasciiConverter::toAscii(0x9F) == '?');
}

TEST_CASE("AtasciiConverter: international/graphics $A0–$FF map to '?'", "[atascii]") {
    REQUIRE(AtasciiConverter::toAscii(0xA0) == '?');
    REQUIRE(AtasciiConverter::toAscii(0xC0) == '?');
    REQUIRE(AtasciiConverter::toAscii(0xFF) == '?');
}

TEST_CASE("AtasciiConverter: $60 (ATASCII backtick) maps to '?'", "[atascii]") {
    // ATASCII $60 is not a standard char
    REQUIRE(AtasciiConverter::toAscii(0x60) == '?');
}

// ── convertBuffer() ──────────────────────────────────────────────────────────

TEST_CASE("AtasciiConverter::convertBuffer converts in-place", "[atascii]") {
    uint8_t buf[] = {'H','E','L','L','O'};
    AtasciiConverter::convertBuffer(buf, 5);
    REQUIRE(buf[0] == 'H');
    REQUIRE(buf[1] == 'E');
    REQUIRE(buf[2] == 'L');
    REQUIRE(buf[3] == 'L');
    REQUIRE(buf[4] == 'O');
}

TEST_CASE("AtasciiConverter::convertBuffer handles mixed bytes", "[atascii]") {
    uint8_t buf[] = {'A', 0x09, 0xA0, 0x9B};
    AtasciiConverter::convertBuffer(buf, 4);
    REQUIRE((char)buf[0] == 'A');
    REQUIRE((char)buf[1] == '\t');
    REQUIRE((char)buf[2] == '?');
    REQUIRE((char)buf[3] == '?'); // $9B → '?'
}

TEST_CASE("AtasciiConverter::convertBuffer zero length is safe", "[atascii]") {
    uint8_t buf[] = {0xA0};
    AtasciiConverter::convertBuffer(buf, 0);
    REQUIRE(buf[0] == 0xA0); // unchanged
}
