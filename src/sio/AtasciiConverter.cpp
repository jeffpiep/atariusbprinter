#include "sio/AtasciiConverter.h"

// ATASCII→ASCII lookup table (256 entries).
//
// Phase 5 mapping per spec §14.9:
//  $20–$5A  Direct (same as ASCII printable + uppercase)
//  $61–$7A  Lowercase letters (ATASCII matches ASCII)
//  $00–$1F  Control: $09→Tab, $0A→LF, $0D→CR; rest→space
//  $9B      EOL — handled by LineAssembler, never reaches this table
//  $80–$9A  Inverse video of $00–$1A → strip inverse bit, return base char
//  $A0–$FE  International/graphics → '?' placeholder (Phase 6 will expand)
//
static const char ATASCII_TABLE[256] = {
    // $00–$0F
    ' ',' ',' ',' ',' ',' ',' ',' ',' ','\t','\n',' ',' ','\r',' ',' ',
    // $10–$1F
    ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
    // $20–$2F  (space ! " # $ % & ' ( ) * + , - . /)
    ' ','!','"','#','$','%','&','\'','(',')','*','+',',','-','.','/',
    // $30–$3F  (0–9 : ; < = > ?)
    '0','1','2','3','4','5','6','7','8','9',':',';','<','=','>','?',
    // $40–$4F  (@ A–O)
    '@','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',
    // $50–$5F  (P–Z [ \ ] ^ _)
    'P','Q','R','S','T','U','V','W','X','Y','Z','[','\\',']','^','_',
    // $60–$6F  (` a–o)  — ATASCII $60 is not a standard char → '?'
    '?','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
    // $70–$7F  (p–z { | } ~  DEL→'?')
    'p','q','r','s','t','u','v','w','x','y','z','{','|','}','~','?',
    // $80–$8F  inverse video $00–$0F → strip bit, return base printable equivalent
    ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
    // $90–$9F
    // $90–$9A = inverse $10–$1A → map to base; $9B = EOL (caller strips); $9C–$9F→'?'
    ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ','?','?','?','?','?',
    // $A0–$AF  → '?' (international/graphics, Phase 6)
    '?','?','?','?','?','?','?','?','?','?','?','?','?','?','?','?',
    // $B0–$BF
    '?','?','?','?','?','?','?','?','?','?','?','?','?','?','?','?',
    // $C0–$CF
    '?','?','?','?','?','?','?','?','?','?','?','?','?','?','?','?',
    // $D0–$DF
    '?','?','?','?','?','?','?','?','?','?','?','?','?','?','?','?',
    // $E0–$EF
    '?','?','?','?','?','?','?','?','?','?','?','?','?','?','?','?',
    // $F0–$FF
    '?','?','?','?','?','?','?','?','?','?','?','?','?','?','?','?',
};

char AtasciiConverter::toAscii(uint8_t atascii) {
    return ATASCII_TABLE[atascii];
}

void AtasciiConverter::convertBuffer(uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        buf[i] = static_cast<uint8_t>(ATASCII_TABLE[buf[i]]);
    }
}
