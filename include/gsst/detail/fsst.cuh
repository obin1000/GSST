// =============================================================================
// GSST — Internal: FSST algorithm types and structures
// =============================================================================
// Core types for the FSST (Fast Static Symbol Table) compression algorithm.
// Implements the 5-round iterative symbol table construction from the FSST
// paper, plus GPU-optimized encoding table formats.
//
// This file is organized in two parts:
//   1. Host/device types for encoding lookup (used in both CPU and GPU paths)
//   2. Host-only types for table construction (SymbolTable, Counters, etc.)
// =============================================================================
#pragma once

#include <cassert>
#include <cstring>

#include "common.hpp"


namespace gsst::detail::fsst {

// =============================================================================
// FSST constants
// =============================================================================
inline constexpr uint16_t CODE_BASE = 256;  // first 256 codes are pseudo (escape)
inline constexpr uint16_t CODE_MAX = 512;
inline constexpr uint16_t CODE_MASK = CODE_MAX - 1;
inline constexpr uint32_t HASH_TAB_SIZE = 1024;
inline constexpr uint32_t MAX_HASH_USAGE = 127;
inline constexpr size_t SAMPLE_TARGET = 1 << 14;  // 16 KB
inline constexpr size_t SAMPLE_MAX = 2 * SAMPLE_TARGET;
inline constexpr size_t SAMPLE_LINE = 512;

// Hash function (from FSST paper)
#define FSST_HASH(w) (((w) * 2971215073LL) ^ (((w) * 2971215073LL) >> 15))

inline constexpr uint64_t ICL_FREE = ((uint64_t)15 << 28) | (((uint64_t)CODE_MASK) << 16);

// =============================================================================
// Symbol — 8-byte character sequence with embedded code and length
// =============================================================================
struct Symbol {
    static constexpr unsigned maxLength = 8;
    static constexpr uint8_t escape = ESC_CODE;  // 255
    static constexpr uint8_t ignore = PAD_CODE;  // 254
    static constexpr uint8_t skip = ignore - 1;  // 253

    union {
        char str[maxLength];
        uint64_t num;
    } val;

    // icl = ignoredBits:16 | code:12 | length:4 | unused:32
    uint64_t icl;

    Symbol() : icl(0) { val.num = 0; }

    explicit Symbol(uint8_t c, uint16_t code) : icl((1ULL << 28) | ((uint64_t)code << 16) | 56) { val.num = c; }

    explicit Symbol(const uint8_t* input, uint32_t len) {
        val.num = 0;
        if (len >= 8) {
            len = 8;
            std::memcpy(val.str, input, 8);
        } else {
            std::memcpy(val.str, input, len);
        }
        set_code_len(CODE_MAX, len);
    }

    void set_code_len(uint32_t code, uint32_t len) {
        icl = ((uint64_t)len << 28) | ((uint64_t)code << 16) | ((8 - len) * 8);
    }

    uint32_t length() const { return static_cast<uint32_t>(icl >> 28); }
    uint16_t code() const { return (icl >> 16) & CODE_MASK; }
    uint32_t ignoredBits() const { return static_cast<uint32_t>(icl); }
    uint8_t first() const { return 0xFF & val.num; }
    uint16_t first2() const { return 0xFFFF & val.num; }
    uint8_t second() const { return (0xFF00 & val.num) >> 8; }

    size_t hash() const {
        size_t v = 0xFFFFFF & val.num;
        return FSST_HASH(v);
    }
};

// =============================================================================
// QSymbol — Symbol with compression gain score (for priority queue)
// =============================================================================
struct QSymbol {
    Symbol symbol;
    mutable uint32_t gain;

    bool operator==(const QSymbol& other) const {
        return symbol.val.num == other.symbol.val.num && symbol.length() == other.symbol.length();
    }
};

// =============================================================================
// Counters — frequency counts for symbol table construction
// =============================================================================
struct Counters {
    uint16_t count1[CODE_MAX];
    uint16_t count2[CODE_MAX][CODE_MAX];

    void count1Set(uint32_t pos1, uint16_t val) { count1[pos1] = val; }
    void count1Inc(uint32_t pos1) { count1[pos1]++; }
    void count2Inc(uint32_t pos1, uint32_t pos2) { count2[pos1][pos2]++; }

    uint32_t count1GetNext(uint32_t& pos1) { return count1[pos1]; }
    uint32_t count2GetNext(uint32_t pos1, uint32_t& pos2) { return count2[pos1][pos2]; }

    void backup1(uint8_t* buf) { std::memcpy(buf, count1, CODE_MAX * sizeof(uint16_t)); }
    void restore1(uint8_t* buf) { std::memcpy(count1, buf, CODE_MAX * sizeof(uint16_t)); }
};

// =============================================================================
// Encoder — wraps Counters allocation
// =============================================================================
struct Encoder {
    Counters counters;
};

// =============================================================================
// SymbolTable — CPU lookup table for FSST table construction
// =============================================================================
// This is a large structure (~200KB) used only during table construction on
// the CPU. It is NOT suitable for GPU shared memory.
struct SymbolTable {
    static constexpr uint32_t hashTabSize = HASH_TAB_SIZE;
    static constexpr uint8_t maxSize = Symbol::skip;  // 253
    static constexpr uint8_t maxSameRowTwo = 8;
    static constexpr uint8_t maxRowsTwo = 32;
    static constexpr uint8_t maxHashUsage = MAX_HASH_USAGE;

    uint16_t shortCodes[65536];
    uint16_t byteCodes[256];
    Symbol symbols[CODE_MAX];
    Symbol hashTab[hashTabSize];

    uint16_t nSymbols;
    uint16_t suffixLim;
    uint16_t terminator;
    bool zeroTerminated;
    uint16_t lenHisto[9];  // lenHisto[x] = symbols of byte-length (x+1)

    uint8_t maxRow[256];
    uint8_t maxHash;
    uint8_t rows_used;

    SymbolTable() : nSymbols(0), suffixLim(CODE_MAX), terminator(0), zeroTerminated(false), maxHash(0), rows_used(0) {
        for (uint32_t i = 0; i < 256; i++)
            symbols[i] = Symbol(static_cast<uint8_t>(i), static_cast<uint16_t>(i | (1 << 12)));
        Symbol unused = Symbol(static_cast<uint8_t>(0), CODE_MASK);
        for (uint32_t i = 256; i < CODE_MAX; i++)
            symbols[i] = unused;

        Symbol s;
        s.val.num = 0;
        s.icl = ICL_FREE;
        for (uint32_t i = 0; i < hashTabSize; i++)
            hashTab[i] = s;

        for (uint32_t i = 0; i < 256; i++)
            byteCodes[i] = (1 << 12) | i;
        for (uint32_t i = 0; i < 65536; i++)
            shortCodes[i] = (1 << 12) | (i & 255);

        std::memset(lenHisto, 0, sizeof(lenHisto));
        std::memset(maxRow, 0, sizeof(maxRow));
    }

    void clear() {
        std::memset(lenHisto, 0, sizeof(lenHisto));
        std::memset(maxRow, 0, sizeof(maxRow));
        maxHash = 0;
        rows_used = 0;
        for (uint32_t i = CODE_BASE; i < CODE_BASE + nSymbols; i++) {
            if (symbols[i].length() == 1) {
                uint16_t v = symbols[i].first();
                byteCodes[v] = (1 << 12) | v;
            } else if (symbols[i].length() == 2) {
                uint16_t v = symbols[i].first2();
                shortCodes[v] = (1 << 12) | (v & 255);
            } else {
                uint32_t idx = symbols[i].hash() & (hashTabSize - 1);
                hashTab[idx].val.num = 0;
                hashTab[idx].icl = ICL_FREE;
            }
        }
        nSymbols = 0;
    }

    bool hashInsert(Symbol s) {
        if (maxHash >= maxHashUsage)
            return false;
        uint32_t idx = s.hash() & (hashTabSize - 1);
        if (hashTab[idx].icl < ICL_FREE)
            return false;  // collision
        hashTab[idx].icl = s.icl;
        hashTab[idx].val.num = s.val.num & (0xFFFFFFFFFFFFFFFF >> static_cast<uint8_t>(s.icl));
        maxHash += 1;
        return true;
    }

    bool add(Symbol s, bool final_round = false) {
        assert(CODE_BASE + nSymbols < CODE_MAX);
        uint32_t len = s.length();
        s.set_code_len(CODE_BASE + nSymbols, len);
        assert(s.code() != Symbol::ignore);

        if (len == 1) {
            byteCodes[s.first()] = CODE_BASE + nSymbols + (1 << 12);
        } else if (len == 2) {
            if (final_round && maxRow[s.first()] >= maxSameRowTwo)
                return false;
            if (final_round && rows_used >= maxRowsTwo)
                return false;
            if (maxRow[s.first()] == 0)
                rows_used += 1;
            maxRow[s.first()] += 1;
            shortCodes[s.first2()] = CODE_BASE + nSymbols + (2 << 12);
        } else if (!hashInsert(s)) {
            return false;
        }

        symbols[CODE_BASE + nSymbols++] = s;
        lenHisto[len - 1]++;
        return true;
    }

    /// Find longest matching symbol. Returns internal code (>= CODE_BASE for
    /// real symbols, < CODE_BASE for escaped pseudo-symbols).
    uint16_t findLongestSymbol(Symbol s) const {
        size_t idx = s.hash() & (hashTabSize - 1);
        Symbol hs = hashTab[idx];
        uint8_t ignoredBits = static_cast<uint8_t>(hs.icl);
        uint64_t num = s.val.num & (0xFFFFFFFFFFFFFFFF >> ignoredBits);

        if (hs.icl <= s.icl && hs.val.num == num) {
            uint16_t codeAndLen = hs.icl >> 16;
            return codeAndLen & CODE_MASK;
        }
        if (s.length() >= 2) {
            uint16_t code = shortCodes[s.first2()] & CODE_MASK;
            if (code >= CODE_BASE)
                return code;
        }
        return byteCodes[s.first()] & CODE_MASK;
    }

    uint16_t findLongestSymbol(const uint8_t* cur, const uint8_t* end) const {
        return findLongestSymbol(Symbol(cur, static_cast<uint32_t>(end - cur)));
    }

    /// Renumber codes by decreasing length (8-byte first, 1-byte last).
    /// After this, symbols[0..nSymbols-1] have codes 0..nSymbols-1.
    void finalize_simple_decreasing() {
        assert(nSymbols <= maxSize + 1);
        uint8_t newCode[256];
        uint8_t rsum[8];

        rsum[7] = 0;
        for (int i = 7; i > 0; i--)
            rsum[i - 1] = rsum[i] + static_cast<uint8_t>(lenHisto[i]);

        for (uint32_t i = 0; i < nSymbols; i++) {
            Symbol s1 = symbols[CODE_BASE + i];
            uint32_t len = s1.length();
            newCode[i] = rsum[len - 1]++;
            s1.set_code_len(newCode[i], len);
            symbols[newCode[i]] = s1;
        }

        for (uint32_t i = 0; i < 256; i++)
            if ((byteCodes[i] & CODE_MASK) >= CODE_BASE)
                byteCodes[i] = newCode[static_cast<uint8_t>(byteCodes[i])] + (1 << 12);
            else
                byteCodes[i] = 511 + (1 << 12);

        for (uint32_t i = 0; i < 65536; i++)
            if ((shortCodes[i] & CODE_MASK) >= CODE_BASE)
                shortCodes[i] = newCode[static_cast<uint8_t>(shortCodes[i])] + (shortCodes[i] & (15 << 12));
            else
                shortCodes[i] = byteCodes[i & 0xFF];

        for (uint32_t i = 0; i < hashTabSize; i++)
            if (hashTab[i].icl < ICL_FREE)
                hashTab[i] = symbols[newCode[static_cast<uint8_t>(hashTab[i].code())]];
    }
};

// =============================================================================
// Table construction functions (implemented in symbol_table.cu)
// =============================================================================

/// Count symbol frequencies and estimate compression gain.
int compress_count_single(SymbolTable* st, Counters& counters, size_t sampleFrac, const uint8_t* line, size_t len);

/// Build candidate symbols from frequency counts and add to table.
void make_table(SymbolTable* st, Counters& counters, size_t sampleFrac);

/// Concatenate two symbols (max 8 bytes total).
inline Symbol concat(Symbol a, Symbol b) {
    Symbol s;
    uint32_t length = a.length() + b.length();
    if (length > Symbol::maxLength) {
        length = Symbol::maxLength;
        b.set_code_len(CODE_MASK, length - a.length());
        b.val.num = b.val.num & ((1ULL << (b.length() * 8)) - 1);
    }
    s.set_code_len(CODE_MASK, length);
    s.val.num = (b.val.num << (8 * a.length())) | a.val.num;
    return s;
}

/// Create a random sample from input data.
size_t make_sample(uint8_t* sample_buf, const uint8_t* src, size_t len);

}  // namespace gsst::detail::fsst
