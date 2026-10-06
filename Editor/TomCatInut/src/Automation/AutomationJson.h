#pragma once
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace TomCat::AutomationJson {
inline std::string Quote(const std::string& value)
{
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value)
    {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += c;
    }
    return out + '"';
}
inline std::string Number(double value)
{
    if (!std::isfinite(value)) throw std::runtime_error("Non-finite property value");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << value;
    return out.str();
}
inline std::string Array(const std::vector<std::string>& values)
{
    std::string out = "[";
    for (const auto& value : values) { if (out.size() > 1) out += ','; out += value; }
    return out + ']';
}
inline std::string Object(std::initializer_list<std::pair<std::string, std::string>> fields)
{
    std::string out = "{";
    for (const auto& [key, value] : fields)
    { if (out.size() > 1) out += ','; out += Quote(key) + ':' + value; }
    return out + '}';
}
inline std::string Error(const std::string& code, const std::string& message)
{
    return Object({{"ok", "false"}, {"error", Object({{"code", Quote(code)}, {"message", Quote(message)}})}});
}
}
