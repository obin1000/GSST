// =============================================================================
// GSST — Symbol table construction (full FSST algorithm)
// =============================================================================
// Implements the 5-round iterative symbol table construction from the FSST
// paper. Ported from the fsst-gpu project's build_symbol_table.
//
// The algorithm:
//   1. Sample representative data from the input
//   2. For each of 5 rounds (sampleFrac = 8, 38, 68, 98, 128):
//      a. Count symbol frequencies and pair frequencies
//      b. Estimate compression gain
//      c. Build candidate symbols by concatenating frequent pairs
//      d. Select top candidates by gain, rebuild table
//   3. Finalize: export/import decoder table for consistent code assignment
//   4. Build fast encoding lookup from decoder table
// =============================================================================

#include <algorithm>
#include <cstring>
#include <functional>
#include <gsst/detail/common.hpp>
#include <gsst/detail/fsst.cuh>
#include <gsst/detail/symbol_table.hpp>
#include <queue>
#include <unordered_set>
#include <vector>


namespace gsst::detail {

// =============================================================================
// DecoderTable import/export  (FSST-compatible serialization)
// =============================================================================

size_t DecoderTable::import_from(const uint8_t* data, size_t data_len) {
    if (data_len < 17)
        return 0;

    size_t pos = 0;
    std::memcpy(&version, data + pos, 8);
    pos += 8;

    zero_terminated = data[pos++];

    uint8_t len_histo[8];
    std::memcpy(len_histo, data + pos, 8);
    pos += 8;

    std::memset(len, 0, sizeof(len));
    std::memset(symbol, 0, sizeof(symbol));

    uint16_t code = 0;
    for (int l = 0; l < 8; ++l) {
        const uint8_t sym_len = static_cast<uint8_t>(l + 1);
        for (int s = 0; s < len_histo[l]; ++s) {
            if (code >= MAX_SYMBOLS)
                return 0;
            if (pos + sym_len > data_len)
                return 0;

            len[code] = sym_len;
            uint64_t sym = 0;
            std::memcpy(&sym, data + pos, sym_len);
            symbol[code] = sym;
            pos += sym_len;
            code++;
        }
    }
    return pos;
}

size_t DecoderTable::export_to(uint8_t* buf, size_t buf_capacity) const {
    if (buf_capacity < 17)
        return 0;

    size_t pos = 0;
    std::memcpy(buf + pos, &version, 8);
    pos += 8;

    buf[pos++] = zero_terminated;

    // Build length histogram
    uint8_t len_histo[8] = {};
    uint16_t codes_by_length[8][256];
    uint16_t counts[8] = {};

    for (uint16_t c = 0; c < MAX_SYMBOLS; ++c) {
        if (len[c] > 0 && len[c] <= 8) {
            const int idx = len[c] - 1;
            len_histo[idx]++;
            codes_by_length[idx][counts[idx]++] = c;
        }
    }

    std::memcpy(buf + pos, len_histo, 8);
    pos += 8;

    // Write symbols grouped by length (increasing: 1, 2, ..., 8)
    for (int l = 0; l < 8; ++l) {
        const uint8_t sym_len = static_cast<uint8_t>(l + 1);
        for (int s = 0; s < counts[l]; ++s) {
            const uint16_t c = codes_by_length[l][s];
            if (pos + sym_len > buf_capacity)
                return 0;
            std::memcpy(buf + pos, &symbol[c], sym_len);
            pos += sym_len;
        }
    }
    return pos;
}

// =============================================================================
// FSST table construction helpers
// =============================================================================
namespace fsst {

// Hash for QSymbol unordered_set
struct QSymbolHash {
    size_t operator()(const QSymbol& q) const noexcept {
        uint64_t k = q.symbol.val.num;
        constexpr uint64_t m = 0xc6a4a7935bd1e995ULL;
        constexpr int r = 47;
        uint64_t h = 0x8445d61a4e774912ULL ^ (8 * m);
        k *= m;
        k ^= k >> r;
        k *= m;
        h ^= k;
        h *= m;
        h ^= h >> r;
        h *= m;
        h ^= h >> r;
        return static_cast<size_t>(h);
    }
};

int compress_count_single(SymbolTable* st, Counters& counters, size_t sampleFrac, const uint8_t* line, size_t len) {
    int gain = 0;
    const uint8_t* cur = line;
    const uint8_t* start = cur;
    const uint8_t* end = cur + len;

    auto is_escape = [](uint16_t pos) -> int { return pos < CODE_BASE ? 1 : 0; };

    if (cur < end) {
        uint16_t code2 = 255, code1 = st->findLongestSymbol(cur, end);
        cur += st->symbols[code1].length();
        gain += static_cast<int>(st->symbols[code1].length()) - (1 + is_escape(code1));

        while (true) {
            counters.count1Inc(code1);

            if (st->symbols[code1].length() != 1)
                counters.count1Inc(*start);

            if (cur == end)
                break;

            start = cur;
            if (cur < end - 7) {
                uint64_t word = 0;
                std::memcpy(&word, cur, 8);
                size_t code_hash = word & 0xFFFFFF;
                size_t idx = FSST_HASH(code_hash) & (SymbolTable::hashTabSize - 1);
                Symbol s = st->hashTab[idx];
                code2 = st->shortCodes[word & 0xFFFF] & CODE_MASK;
                word &= (0xFFFFFFFFFFFFFFFF >> static_cast<uint8_t>(s.icl));
                if ((s.icl < ICL_FREE) & (s.val.num == word)) {
                    code2 = s.code();
                    cur += s.length();
                } else if (code2 >= CODE_BASE) {
                    cur += 2;
                } else {
                    code2 = st->byteCodes[*start] & CODE_MASK;
                    cur += 1;
                }
            } else {
                code2 = st->findLongestSymbol(cur, end);
                cur += st->symbols[code2].length();
            }

            gain += static_cast<int>(cur - start) - (1 + is_escape(code2));

            if (sampleFrac < 128) {
                counters.count2Inc(code1, code2);
                if ((cur - start) > 1)
                    counters.count2Inc(code1, *start);
            }
            code1 = code2;
        }
    }
    return gain;
}

void make_table(SymbolTable* st, Counters& counters, size_t sampleFrac) {
    std::unordered_set<QSymbol, QSymbolHash> cands;

    uint16_t terminator = st->nSymbols ? CODE_BASE : st->terminator;
    counters.count1Set(terminator, 65535);

    auto addOrInc = [&](Symbol s, uint64_t count) {
        QSymbol q;
        q.symbol = s;
        q.gain = static_cast<uint32_t>(count * s.length());
        auto it = cands.find(q);
        if (it != cands.end()) {
            q.gain += it->gain;
            cands.erase(it);
        }
        cands.insert(q);
    };

    for (uint32_t pos1 = 0; pos1 < CODE_BASE + static_cast<size_t>(st->nSymbols); pos1++) {
        uint32_t cnt1 = counters.count1GetNext(pos1);
        if (!cnt1)
            continue;

        Symbol s1 = st->symbols[pos1];
        addOrInc(s1, ((s1.length() == 1) ? 2ULL : 1ULL) * cnt1);

        for (uint32_t pos2 = 0; pos2 < CODE_BASE + static_cast<size_t>(st->nSymbols); pos2++) {
            uint32_t cnt2 = counters.count2GetNext(pos1, pos2);
            if (!cnt2)
                continue;

            Symbol s2 = st->symbols[pos2];
            Symbol s3 = concat(s1, s2);
            if (s2.val.str[0] != st->terminator)
                addOrInc(s3, cnt2);
        }
    }

    // Sort candidates by gain (descending)
    auto cmpGn = [](const QSymbol& q1, const QSymbol& q2) {
        return (q1.gain < q2.gain) || (q1.gain == q2.gain && q1.symbol.val.num > q2.symbol.val.num);
    };
    std::priority_queue<QSymbol, std::vector<QSymbol>, decltype(cmpGn)> pq(cmpGn);
    for (auto& q : cands)
        pq.push(q);

    st->clear();
    while (st->nSymbols < SymbolTable::maxSize && !pq.empty()) {
        QSymbol q = pq.top();
        pq.pop();
        st->add(q.symbol, sampleFrac == 128);
    }

    // In the final round, ensure byte 0xFE is a symbol (not escaped)
    if (sampleFrac == 128) {
        st->add(Symbol(Symbol::ignore, 0), true);
    }
}

size_t make_sample(uint8_t* sample_buf, const uint8_t* src, size_t len) {
    if (len <= SAMPLE_TARGET) {
        std::memcpy(sample_buf, src, len);
        return len;
    }

    size_t sampleLen = 0;
    size_t sampleRnd = FSST_HASH(4637947);
    size_t chunks = 1 + (len - 1) / SAMPLE_LINE;
    uint8_t* dst = sample_buf;
    const uint8_t* sampleLim = sample_buf + SAMPLE_TARGET;

    while (dst < sampleLim) {
        sampleRnd = FSST_HASH(sampleRnd);
        size_t chunk = SAMPLE_LINE * (sampleRnd % chunks);
        size_t chunkLen = std::min(len - chunk, SAMPLE_LINE);
        std::memcpy(dst, src + chunk, chunkLen);
        dst += chunkLen;
        sampleLen += chunkLen;
    }
    return sampleLen;
}

}  // namespace fsst

// =============================================================================
// Build fast encoding lookup from a DecoderTable
// =============================================================================
static void build_fast_lookup(EncoderTable* table) {
    std::memset(table->single_code, ESC_CODE, sizeof(table->single_code));
    std::memset(table->pair_code, ESC_CODE, sizeof(table->pair_code));
    for (auto& h : table->hash) {
        h.masked_val = 0;
        h.code = ESC_CODE;
        h.len = 0;
    }

    for (uint16_t code = 0; code < table->num_symbols; ++code) {
        const uint8_t sym_len = table->decoder.len[code];
        const uint64_t sym_val = table->decoder.symbol[code];
        if (sym_len == 0)
            continue;

        if (sym_len == 1) {
            table->single_code[sym_val & 0xFF] = static_cast<uint8_t>(code);
        } else if (sym_len == 2) {
            table->pair_code[sym_val & 0xFFFF] = static_cast<uint8_t>(code);
        } else {
            // 3+ byte symbol: hash on first 3 bytes
            uint64_t first3 = sym_val & 0xFFFFFF;
            size_t idx = FSST_HASH(first3) & 1023;
            if (table->hash[idx].len == 0) {
                uint64_t mask = (sym_len < 8) ? ((1ULL << (sym_len * 8)) - 1) : ~0ULL;
                table->hash[idx].masked_val = sym_val & mask;
                table->hash[idx].code = static_cast<uint8_t>(code);
                table->hash[idx].len = sym_len;
            }
        }
    }
}

// =============================================================================
// build_encoder_table — main entry point
// =============================================================================
EncoderTable* build_encoder_table(const uint8_t* sample_data, size_t sample_size) {
    if (!sample_data || sample_size == 0)
        return nullptr;

    // --- Sample the input ---
    auto* sample_buf = new uint8_t[fsst::SAMPLE_MAX];
    size_t sample_len = fsst::make_sample(sample_buf, sample_data, sample_size);

    // --- Run 5-round FSST table construction ---
    auto* encoder = new fsst::Encoder();
    auto* st = new fsst::SymbolTable();
    auto* bestTable = new fsst::SymbolTable();
    int bestGain = -static_cast<int>(fsst::SAMPLE_MAX);

    uint8_t bestCounters[fsst::CODE_MAX * sizeof(uint16_t)];

    for (size_t sampleFrac = 8; true; sampleFrac += 30) {
        std::memset(&encoder->counters, 0, sizeof(fsst::Counters));
        int gain = fsst::compress_count_single(st, encoder->counters, sampleFrac, sample_buf, sample_len);

        if (gain >= bestGain) {
            encoder->counters.backup1(bestCounters);
            *bestTable = *st;
            bestGain = gain;
        }

        if (sampleFrac >= 128)
            break;

        fsst::make_table(st, encoder->counters, sampleFrac);
    }

    delete st;
    encoder->counters.restore1(bestCounters);
    fsst::make_table(bestTable, encoder->counters, 128);
    bestTable->finalize_simple_decreasing();

    // --- Populate DecoderTable from finalized SymbolTable ---
    auto* table = new EncoderTable{};
    table->num_symbols = bestTable->nSymbols;
    table->decoder.version = FORMAT_VERSION;
    table->decoder.zero_terminated = 0;
    std::memset(table->decoder.len, 0, sizeof(table->decoder.len));
    std::memset(table->decoder.symbol, 0, sizeof(table->decoder.symbol));

    for (uint16_t i = 0; i < bestTable->nSymbols; ++i) {
        table->decoder.len[i] = static_cast<uint8_t>(bestTable->symbols[i].length());
        table->decoder.symbol[i] = bestTable->symbols[i].val.num;
    }

    // --- Round-trip export/import for consistent code assignment ---
    uint8_t serial_buf[2200];
    size_t serial_len = table->decoder.export_to(serial_buf, sizeof(serial_buf));
    table->decoder.import_from(serial_buf, serial_len);

    // Update num_symbols from imported table
    table->num_symbols = 0;
    for (uint16_t c = 0; c < MAX_SYMBOLS; ++c) {
        if (table->decoder.len[c] > 0)
            table->num_symbols = c + 1;
    }

    // --- Build fast encoding lookup from the imported decoder table ---
    build_fast_lookup(table);

    delete encoder;
    delete bestTable;
    delete[] sample_buf;
    return table;
}

void free_encoder_table(EncoderTable* table) {
    delete table;
}

}  // namespace gsst::detail
