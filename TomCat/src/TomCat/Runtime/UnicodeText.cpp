#include "tcpch.h"
#include "UnicodeText.h"
#include <algorithm>
#include <cmath>
#ifdef TC_PLATFORM_WINDOWS
#include <icu.h>
#pragma comment(lib, "icu.lib")
#endif

namespace TomCat::UnicodeText {
#ifdef TC_PLATFORM_WINDOWS
namespace {
std::vector<UChar> ToUTF16(std::string_view text) {
    UErrorCode error = U_ZERO_ERROR;
    int32_t length = 0;
    u_strFromUTF8(nullptr, 0, &length, text.data(), static_cast<int32_t>(text.size()), &error);
    if (error != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(error)) return {};
    std::vector<UChar> result(length + 1); error = U_ZERO_ERROR;
    u_strFromUTF8(result.data(), static_cast<int32_t>(result.size()), &length, text.data(), static_cast<int32_t>(text.size()), &error);
    if (U_FAILURE(error)) return {};
    result.resize(length); return result;
}
std::string ToUTF8(const UChar* text, int32_t length) {
    UErrorCode error = U_ZERO_ERROR; int32_t size = 0;
    u_strToUTF8(nullptr, 0, &size, text, length, &error);
    std::string result(size, '\0'); error = U_ZERO_ERROR;
    u_strToUTF8(result.data(), size, nullptr, text, length, &error);
    return U_FAILURE(error) ? std::string() : result;
}
}
#endif
std::vector<size_t> GraphemeBoundaries(std::string_view text) {
    std::vector<size_t> result{0};
#ifdef TC_PLATFORM_WINDOWS
    UErrorCode error = U_ZERO_ERROR;
    UText native = UTEXT_INITIALIZER;
    utext_openUTF8(&native, text.data(), static_cast<int64_t>(text.size()), &error);
    UBreakIterator* iterator = ubrk_open(UBRK_CHARACTER, "root", nullptr, 0, &error);
    if (iterator && U_SUCCESS(error)) {
        ubrk_setUText(iterator, &native, &error);
        if (U_SUCCESS(error)) for (int32_t next = ubrk_next(iterator); next != UBRK_DONE; next = ubrk_next(iterator)) result.push_back(static_cast<size_t>(next));
    }
    if (iterator) ubrk_close(iterator);
    utext_close(&native);
#else
    for (size_t index = 1; index < text.size(); ++index)
        if ((static_cast<unsigned char>(text[index]) & 0xc0) != 0x80) result.push_back(index);
#endif
    if (result.back() != text.size()) result.push_back(text.size());
    return result;
}
size_t Previous(std::string_view text, size_t offset) {
    const auto edges = GraphemeBoundaries(text);
    const auto found = std::lower_bound(edges.begin(), edges.end(), std::min(offset, text.size()));
    return found == edges.begin() ? 0 : *std::prev(found);
}
size_t Next(std::string_view text, size_t offset) {
    const auto edges = GraphemeBoundaries(text);
    const auto found = std::upper_bound(edges.begin(), edges.end(), offset);
    return found == edges.end() ? text.size() : *found;
}
std::vector<VisualCluster> VisualClusters(std::string_view text) {
    const auto edges = GraphemeBoundaries(text);
    std::vector<VisualCluster> result;
#ifdef TC_PLATFORM_WINDOWS
    auto logical = ToUTF16(text);
    if (logical.empty()) return result;
    UErrorCode error = U_ZERO_ERROR;
    std::vector<UChar> shaped(logical.size());
    const int32_t count = u_shapeArabic(logical.data(), static_cast<int32_t>(logical.size()), shaped.data(), static_cast<int32_t>(shaped.size()),
        U_SHAPE_LETTERS_SHAPE | U_SHAPE_LENGTH_FIXED_SPACES_NEAR, &error);
    if (U_FAILURE(error)) shaped = logical; else shaped.resize(count);
    error = U_ZERO_ERROR;
    UBiDi* bidi = ubidi_open();
    if (bidi) {
        ubidi_setPara(bidi, shaped.data(), static_cast<int32_t>(shaped.size()), UBIDI_DEFAULT_LTR, nullptr, &error);
        std::vector<std::pair<int32_t, VisualCluster>> ordered;
        int32_t offset = 0;
        for (size_t i=1; i<edges.size() && U_SUCCESS(error); ++i) {
            const auto cluster = ToUTF16(text.substr(edges[i-1], edges[i]-edges[i-1]));
            const int32_t length = static_cast<int32_t>(cluster.size());
            if (!length || offset + length > static_cast<int32_t>(shaped.size())) break;
            const bool rtl = (ubidi_getLevelAt(bidi, offset) & 1) != 0;
            auto characters = std::vector<UChar>(shaped.begin()+offset, shaped.begin()+offset+length);
            if (rtl) for (auto& ch : characters) if (!U16_IS_SURROGATE(ch)) ch=static_cast<UChar>(u_charMirror(ch));
            ordered.push_back({ubidi_getVisualIndex(bidi, offset, &error), {ToUTF8(characters.data(), length), edges[i-1], edges[i], rtl}});
            offset += length;
        }
        ubidi_close(bidi);
        if (U_SUCCESS(error) && ordered.size()+1 == edges.size()) {
            std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b){return a.first<b.first;});
            for (auto& item : ordered) result.push_back(std::move(item.second));
            return result;
        }
    }
#endif
    for (size_t i=1;i<edges.size();++i) result.push_back({std::string(text.substr(edges[i-1],edges[i]-edges[i-1])),edges[i-1],edges[i],false});
    return result;
}
std::string VisualOrder(std::string_view text) {
    std::string output;
    for (const auto& cluster : VisualClusters(text)) output += cluster.Text;
    return output;
}

std::string PluralCategory(std::string_view locale, double count) {
    if (!std::isfinite(count)) return "other";
#ifdef TC_PLATFORM_WINDOWS
    UErrorCode error = U_ZERO_ERROR;
    UPluralRules* rules = uplrules_openForType(std::string(locale).c_str(), UPLURAL_TYPE_CARDINAL, &error);
    UChar result[32]{}; int32_t length = 0;
    if (rules && U_SUCCESS(error)) length = uplrules_select(rules, count, result, 32, &error);
    if (rules) uplrules_close(rules);
    if (U_SUCCESS(error)) return ToUTF8(result, length);
#endif
    return count == 1 ? "one" : "other";
}
std::string FormatNumber(std::string_view locale, double value) {
    if (!std::isfinite(value)) return {};
#ifdef TC_PLATFORM_WINDOWS
    UErrorCode error = U_ZERO_ERROR;
    UNumberFormat* format = unum_open(UNUM_DECIMAL, nullptr, 0, std::string(locale).c_str(), nullptr, &error);
    UChar result[512]{}; int32_t length = 0;
    if (format && U_SUCCESS(error)) length = unum_formatDouble(format, value, result, 512, nullptr, &error);
    if (format) unum_close(format);
    if (U_SUCCESS(error)) return ToUTF8(result, length);
#endif
    return std::to_string(value);
}
}
