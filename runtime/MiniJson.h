#pragma once

// A small JSON reader for the decision capture: one DOM value, strict parse,
// canonical re-serialisation. Standard library only, so the policy tests and
// the replay tool build it without the game.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace MiniJson
{

struct Value
{
    enum Kind
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Kind kind = Null;
    std::string text; // a number's exact text, or a string's decoded bytes
    bool b = false;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    const Value* Find(std::string_view key) const
    {
        for (const auto& entry : obj)
            if (entry.first == key)
                return &entry.second;
        return nullptr;
    }
};

namespace detail
{

// A string as JSON text: `" \ and bytes below 0x20 escaped, everything else
// verbatim (UTF-8 and high bytes pass through).
inline std::string Quote(std::string_view bytes)
{
    static const char* const kHex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() + 2);
    out.push_back('"');
    for (unsigned char c : bytes)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char escape[6] = {'\\', 'u', '0', '0', kHex[c >> 4], kHex[c & 0xF]};
                    out.append(escape, sizeof(escape));
                }
                else
                    out.push_back(static_cast<char>(c));
                break;
        }
    }
    out.push_back('"');
    return out;
}

struct Reader
{
    std::string_view in;
    std::size_t pos = 0;
    std::string& err;

    bool Fail(std::string_view what)
    {
        err.assign(what);
        err += " at offset ";
        err += std::to_string(pos);
        return false;
    }

    bool SkipSpace()
    {
        while (pos < in.size())
        {
            char const c = in[pos];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                break;
            ++pos;
        }
        return true;
    }

    bool ParseValue(Value& out, int depth)
    {
        if (depth > 64)
            return Fail("nesting deeper than 64");
        if (!SkipSpace())
            return false;
        if (pos >= in.size())
            return Fail("expected a value");
        char const c = in[pos];
        if (c == '{')
            return ParseObject(out, depth);
        if (c == '[')
            return ParseArray(out, depth);
        if (c == '"')
        {
            out.kind = Value::String;
            return ParseString(out.text);
        }
        if (c == 't' || c == 'f' || c == 'n')
        {
            std::string_view word = c == 't' ? "true" : (c == 'f' ? "false" : "null");
            if (in.substr(pos, word.size()) != word)
                return Fail("expected true, false or null");
            pos += word.size();
            if (c == 'n')
                out.kind = Value::Null;
            else
            {
                out.kind = Value::Bool;
                out.b = c == 't';
            }
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9'))
        {
            std::size_t const start = pos;
            if (!ParseNumber())
                return false;
            out.kind = Value::Number;
            out.text.assign(in.substr(start, pos - start));
            return true;
        }
        return Fail("expected a value");
    }

    bool ParseNumber()
    {
        if (pos < in.size() && in[pos] == '-')
            ++pos;
        if (pos >= in.size())
            return Fail("truncated number");
        if (in[pos] == '0')
            ++pos;
        else if (in[pos] >= '1' && in[pos] <= '9')
        {
            while (pos < in.size() && in[pos] >= '0' && in[pos] <= '9')
                ++pos;
        }
        else
            return Fail("bad number");
        if (pos < in.size() && in[pos] == '.')
        {
            ++pos;
            if (pos >= in.size() || in[pos] < '0' || in[pos] > '9')
                return Fail("bad number fraction");
            while (pos < in.size() && in[pos] >= '0' && in[pos] <= '9')
                ++pos;
        }
        if (pos < in.size() && (in[pos] == 'e' || in[pos] == 'E'))
        {
            ++pos;
            if (pos < in.size() && (in[pos] == '+' || in[pos] == '-'))
                ++pos;
            if (pos >= in.size() || in[pos] < '0' || in[pos] > '9')
                return Fail("bad number exponent");
            while (pos < in.size() && in[pos] >= '0' && in[pos] <= '9')
                ++pos;
        }
        return true;
    }

    static void AppendUtf8(std::string& out, unsigned code)
    {
        if (code < 0x80)
            out.push_back(static_cast<char>(code));
        else if (code < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        else if (code < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool ParseHex4(unsigned& out)
    {
        if (pos + 4 > in.size())
            return Fail("truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i)
        {
            char const c = in[pos++];
            out <<= 4;
            if (c >= '0' && c <= '9')
                out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                out |= static_cast<unsigned>(c - 'A' + 10);
            else
                return Fail("bad \\u escape");
        }
        return true;
    }

    bool ParseString(std::string& out)
    {
        ++pos; // '"'
        out.clear();
        while (true)
        {
            if (pos >= in.size())
                return Fail("unterminated string");
            unsigned char const c = static_cast<unsigned char>(in[pos++]);
            if (c == '"')
                return true;
            if (c != '\\')
            {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (pos >= in.size())
                return Fail("unterminated escape");
            char const escape = in[pos++];
            switch (escape)
            {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                {
                    unsigned code = 0;
                    if (!ParseHex4(code))
                        return false;
                    if (code >= 0xD800 && code <= 0xDBFF)
                    {
                        if (in.substr(pos, 2) != "\\u")
                            return Fail("lone high surrogate");
                        pos += 2;
                        unsigned low = 0;
                        if (!ParseHex4(low))
                            return false;
                        if (low < 0xDC00 || low > 0xDFFF)
                            return Fail("bad low surrogate");
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    else if (code >= 0xDC00 && code <= 0xDFFF)
                        return Fail("lone low surrogate");
                    AppendUtf8(out, code);
                    break;
                }
                default:
                    return Fail("bad escape");
            }
        }
    }

    bool ParseArray(Value& out, int depth)
    {
        ++pos; // '['
        out.kind = Value::Array;
        out.arr.clear();
        if (!SkipSpace())
            return false;
        if (pos < in.size() && in[pos] == ']')
        {
            ++pos;
            return true;
        }
        while (true)
        {
            Value element;
            if (!ParseValue(element, depth + 1))
                return false;
            out.arr.push_back(std::move(element));
            if (!SkipSpace())
                return false;
            if (pos >= in.size())
                return Fail("unterminated array");
            if (in[pos] == ',')
            {
                ++pos;
                continue;
            }
            if (in[pos] == ']')
            {
                ++pos;
                return true;
            }
            return Fail("expected ',' or ']'");
        }
    }

    bool ParseObject(Value& out, int depth)
    {
        ++pos; // '{'
        out.kind = Value::Object;
        out.obj.clear();
        if (!SkipSpace())
            return false;
        if (pos < in.size() && in[pos] == '}')
        {
            ++pos;
            return true;
        }
        while (true)
        {
            if (!SkipSpace())
                return false;
            if (pos >= in.size() || in[pos] != '"')
                return Fail("expected a key");
            std::string key;
            if (!ParseString(key))
                return false;
            if (!SkipSpace())
                return false;
            if (pos >= in.size() || in[pos] != ':')
                return Fail("expected ':'");
            ++pos;
            Value value;
            if (!ParseValue(value, depth + 1))
                return false;
            out.obj.emplace_back(std::move(key), std::move(value));
            if (!SkipSpace())
                return false;
            if (pos >= in.size())
                return Fail("unterminated object");
            if (in[pos] == ',')
            {
                ++pos;
                continue;
            }
            if (in[pos] == '}')
            {
                ++pos;
                return true;
            }
            return Fail("expected ',' or '}'");
        }
    }
};

inline void RawInto(const Value& v, std::string& out)
{
    switch (v.kind)
    {
        case Value::Null: out += "null"; break;
        case Value::Bool: out += v.b ? "true" : "false"; break;
        case Value::Number: out += v.text; break;
        case Value::String: out += Quote(v.text); break;
        case Value::Array:
            out.push_back('[');
            for (std::size_t i = 0; i < v.arr.size(); ++i)
            {
                if (i)
                    out.push_back(',');
                RawInto(v.arr[i], out);
            }
            out.push_back(']');
            break;
        case Value::Object:
            out.push_back('{');
            for (std::size_t i = 0; i < v.obj.size(); ++i)
            {
                if (i)
                    out.push_back(',');
                out += Quote(v.obj[i].first);
                out.push_back(':');
                RawInto(v.obj[i].second, out);
            }
            out.push_back('}');
            break;
    }
}

} // namespace detail

// Parse one JSON value; trailing non-space text is an error.
inline bool Parse(std::string_view in, Value& out, std::string& err)
{
    detail::Reader reader{in, 0, err};
    out = Value();
    Value parsed;
    if (!reader.ParseValue(parsed, 0))
        return false;
    if (!reader.SkipSpace())
        return false;
    if (reader.pos != in.size())
        return reader.Fail("trailing text");
    out = std::move(parsed);
    return true;
}

// Canonical text: numbers keep their exact text, strings are re-escaped, object
// members keep their order.
inline std::string Raw(const Value& v)
{
    std::string out;
    detail::RawInto(v, out);
    return out;
}

inline std::string Quote(std::string_view bytes)
{
    return detail::Quote(bytes);
}

} // namespace MiniJson
} // namespace TortoiseBots
