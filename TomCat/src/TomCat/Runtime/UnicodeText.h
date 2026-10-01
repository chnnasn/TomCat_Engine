#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>
namespace TomCat::UnicodeText {
std::vector<size_t> GraphemeBoundaries(std::string_view text);
size_t Previous(std::string_view text, size_t byteOffset);
size_t Next(std::string_view text, size_t byteOffset);
struct VisualCluster { std::string Text; size_t Begin = 0, End = 0; bool RTL = false; };
std::vector<VisualCluster> VisualClusters(std::string_view text);
std::string VisualOrder(std::string_view text);
std::string PluralCategory(std::string_view locale, double count);
std::string FormatNumber(std::string_view locale, double value);
}
