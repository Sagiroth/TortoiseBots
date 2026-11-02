#pragma once

// JSON encode/decode and top-level field deltas for one captured `Facts` or
// `Run` (spec: decision capture and replay). Header-only and standard library
// only: the policy tests, the replay tool and the recorder all build it without
// the game.
//
// Exactness rules (they are the format):
//   - float `%.9g`, double `%.17g`; nan/inf/-inf as the JSON strings "nan",
//     "inf", "-inf", so every value round-trips bit-exactly.
//   - integers as JSON integers; the reader keeps the number text and converts
//     by the target type (exact up to uint64).
//   - strings escape " \ and bytes below 0x20 only; high bytes pass through.
//   - sets and (unordered) maps are written sorted by their encoded key text,
//     maps as [[key, value]], so equal values give equal text.
//   - structs encode through the generated X-macros in declaration order, so a
//     FieldMap is a struct's fields in order and a delta is a subset.
//   - `run.plan` is {"name", "hash"}, `run.liveTactics` is a bool; they are the
//     only pointers in a captured struct.

#include "MiniJson.h"
#include "DecisionCaptureFields.gen.h"
#include "DungeonRunPolicy.h"
#include "../ai/playerbot/BossTacticsPolicy.h"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace DecisionCapture
{

constexpr int kRecordVersion = 1;

// A struct's fields as (name, raw JSON) in declaration order.
using FieldMap = std::vector<std::pair<std::string, std::string>>;

struct DecodeContext
{
    const ai::DungeonPlan* plan = nullptr;
    const ai::BossTacticTable* tactics = nullptr;
};

// -- traits --------------------------------------------------------------

template <typename T>
struct IsPair : std::false_type
{
};
template <typename A, typename B>
struct IsPair<std::pair<A, B>> : std::true_type
{
};

template <typename T>
struct IsStdArray : std::false_type
{
};
template <typename T, std::size_t N>
struct IsStdArray<std::array<T, N>> : std::true_type
{
};

template <typename T>
struct IsOptional : std::false_type
{
};
template <typename T>
struct IsOptional<std::optional<T>> : std::true_type
{
};

template <typename T, typename = void>
struct HasMapped : std::false_type
{
};
template <typename T>
struct HasMapped<T, std::void_t<typename T::key_type, typename T::mapped_type>> : std::true_type
{
};

template <typename T, typename = void>
struct HasKey : std::false_type
{
};
template <typename T>
struct HasKey<T, std::void_t<typename T::key_type>> : std::true_type
{
};

template <typename T, typename = void>
struct HasValue : std::false_type
{
};
template <typename T>
struct HasValue<T, std::void_t<typename T::value_type>> : std::true_type
{
};

// -- encoding ------------------------------------------------------------

inline std::uint64_t Fnv1a64(std::string_view s)
{
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (unsigned char c : s)
    {
        hash ^= c;
        hash *= 0x100000001B3ull;
    }
    return hash;
}

inline std::string JsonString(std::string_view bytes)
{
    return MiniJson::Quote(bytes);
}

inline std::string Hex64(std::uint64_t value)
{
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
    return buf;
}

inline std::string EncodeFloating(float value)
{
    if (std::isnan(value))
        return "\"nan\"";
    if (std::isinf(value))
        return value < 0.0f ? std::string("\"-inf\"") : std::string("\"inf\"");
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(value));
    return buf;
}

inline std::string EncodeFloating(double value)
{
    if (std::isnan(value))
        return "\"nan\"";
    if (std::isinf(value))
        return value < 0.0 ? std::string("\"-inf\"") : std::string("\"inf\"");
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", value);
    return buf;
}

template <typename T>
struct StructFields;

inline std::string EncodePlanPointer(const ai::DungeonPlan* plan);

template <typename T>
std::string EncodeValue(const T& value);

template <typename T>
std::string EncodeStructObject(const T& value)
{
    std::string out = "{";
    bool first = true;
    StructFields<T>::Each(value, [&](const char* name, const auto& field)
    {
        if (!first)
            out.push_back(',');
        first = false;
        out += JsonString(name);
        out.push_back(':');
        out += EncodeValue(field);
    });
    out.push_back('}');
    return out;
}

template <typename T>
std::string EncodeValue(const T& value)
{
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<U, bool>)
        return value ? "true" : "false";
    else if constexpr (std::is_enum_v<U>)
    {
        using I = std::underlying_type_t<U>;
        if constexpr (std::is_signed_v<I>)
            return std::to_string(static_cast<long long>(static_cast<I>(value)));
        else
            return std::to_string(static_cast<unsigned long long>(static_cast<I>(value)));
    }
    else if constexpr (std::is_integral_v<U>)
    {
        if constexpr (std::is_signed_v<U>)
            return std::to_string(static_cast<long long>(value));
        else
            return std::to_string(static_cast<unsigned long long>(value));
    }
    else if constexpr (std::is_same_v<U, float>)
        return EncodeFloating(value);
    else if constexpr (std::is_same_v<U, double>)
        return EncodeFloating(value);
    else if constexpr (std::is_same_v<U, std::string>)
        return JsonString(value);
    else if constexpr (std::is_pointer_v<U>)
    {
        if constexpr (std::is_same_v<U, const ai::DungeonPlan*>)
            return EncodePlanPointer(value);
        else if constexpr (std::is_same_v<U, const ai::BossTacticTable*>)
            return value ? "true" : "false";
        else
            return "null";
    }
    else if constexpr (std::is_array_v<U>)
    {
        constexpr std::size_t count = std::extent_v<U>;
        std::string out = "[";
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i)
                out.push_back(',');
            out += EncodeValue(value[i]);
        }
        out.push_back(']');
        return out;
    }
    else if constexpr (IsPair<U>::value)
    {
        return "[" + EncodeValue(value.first) + "," + EncodeValue(value.second) + "]";
    }
    else if constexpr (IsStdArray<U>::value)
    {
        std::string out = "[";
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            if (i)
                out.push_back(',');
            out += EncodeValue(value[i]);
        }
        out.push_back(']');
        return out;
    }
    else if constexpr (IsOptional<U>::value)
    {
        return value ? EncodeValue(*value) : "null";
    }
    else if constexpr (HasMapped<U>::value)
    {
        std::vector<std::pair<std::string, std::string>> entries;
        entries.reserve(value.size());
        for (const auto& entry : value)
            entries.emplace_back(EncodeValue(entry.first), EncodeValue(entry.second));
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        std::string out = "[";
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (i)
                out.push_back(',');
            out += "[" + entries[i].first + "," + entries[i].second + "]";
        }
        out.push_back(']');
        return out;
    }
    else if constexpr (HasKey<U>::value)
    {
        std::vector<std::string> elements;
        elements.reserve(value.size());
        for (const auto& element : value)
            elements.push_back(EncodeValue(element));
        std::sort(elements.begin(), elements.end());
        std::string out = "[";
        for (std::size_t i = 0; i < elements.size(); ++i)
        {
            if (i)
                out.push_back(',');
            out += elements[i];
        }
        out.push_back(']');
        return out;
    }
    else if constexpr (HasValue<U>::value)
    {
        std::string out = "[";
        bool first = true;
        for (const auto& element : value)
        {
            if (!first)
                out.push_back(',');
            first = false;
            out += EncodeValue(element);
        }
        out.push_back(']');
        return out;
    }
    else
        return EncodeStructObject(value);
}

template <typename T>
FieldMap EncodeFieldsOf(const T& value)
{
    FieldMap out;
    StructFields<T>::Each(value, [&](const char* name, const auto& field)
    {
        out.emplace_back(name, EncodeValue(field));
    });
    return out;
}

inline FieldMap EncodeFields(const DungeonRun::Facts& facts)
{
    return EncodeFieldsOf(facts);
}

inline FieldMap EncodeFields(const DungeonRun::Run& run)
{
    return EncodeFieldsOf(run);
}

inline std::string EncodePlanPointer(const ai::DungeonPlan* plan);

inline std::string PlanHash(const ai::DungeonPlan& plan)
{
    return Hex64(Fnv1a64(EncodeStructObject(plan)));
}

inline std::string EncodePlanPointer(const ai::DungeonPlan* plan)
{
    if (!plan)
        return "null";
    return "{\"name\":" + JsonString(plan->name) + ",\"hash\":\"" + PlanHash(*plan) + "\"}";
}

inline std::string EncodePlan(const ai::DungeonPlan& plan)
{
    std::string body = EncodeStructObject(plan);
    return "{\"name\":" + JsonString(plan.name) + ",\"map\":" + std::to_string(plan.map) +
           ",\"hash\":\"" + Hex64(Fnv1a64(body)) + "\",\"body\":" + body + "}";
}

inline std::string EncodeTactics(const ai::BossTacticTable* table, std::uint32_t map)
{
    if (!table)
        return "{\"set\":false,\"rows\":[]}";
    std::vector<ai::BossTacticRow> const rows = table->RowsOnMap(map);
    std::string out = "{\"set\":true,\"rows\":[";
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        if (i)
            out.push_back(',');
        out += EncodeValue(rows[i]);
    }
    out += "]}";
    return out;
}

inline std::string FullObject(const FieldMap& fields)
{
    std::string out = "{";
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        if (i)
            out.push_back(',');
        out += JsonString(fields[i].first);
        out.push_back(':');
        out += fields[i].second;
    }
    out.push_back('}');
    return out;
}

inline std::string DeltaObject(const FieldMap& previous, const FieldMap& now)
{
    std::string out = "{";
    bool first = true;
    for (const auto& field : now)
    {
        const std::string* before = nullptr;
        for (const auto& old : previous)
            if (old.first == field.first)
            {
                before = &old.second;
                break;
            }
        if (before && *before == field.second)
            continue;
        if (!first)
            out.push_back(',');
        first = false;
        out += JsonString(field.first);
        out.push_back(':');
        out += field.second;
    }
    out.push_back('}');
    return out;
}

inline void ApplyDelta(FieldMap& fields, const MiniJson::Value& delta)
{
    for (const auto& entry : delta.obj)
    {
        std::string text = MiniJson::Raw(entry.second);
        bool found = false;
        for (auto& field : fields)
            if (field.first == entry.first)
            {
                field.second = text;
                found = true;
                break;
            }
        if (!found)
            fields.emplace_back(entry.first, std::move(text));
    }
}

// -- decoding ------------------------------------------------------------

template <typename T>
bool DecodeValue(const MiniJson::Value& value, T& out, const DecodeContext& context, std::string& error);

template <typename I>
bool DecodeInteger(const MiniJson::Value& value, I& out, std::string& error)
{
    if (value.kind != MiniJson::Value::Number)
    {
        error = "expected a number";
        return false;
    }
    errno = 0;
    char* end = nullptr;
    if constexpr (std::is_signed_v<I>)
    {
        long long const parsed = std::strtoll(value.text.c_str(), &end, 10);
        if (errno != 0 || end == value.text.c_str() || *end != '\0')
        {
            error = "bad integer '" + value.text + "'";
            return false;
        }
        out = static_cast<I>(parsed);
    }
    else
    {
        unsigned long long const parsed = std::strtoull(value.text.c_str(), &end, 10);
        if (errno != 0 || end == value.text.c_str() || *end != '\0')
        {
            error = "bad integer '" + value.text + "'";
            return false;
        }
        out = static_cast<I>(parsed);
    }
    return true;
}

template <typename F>
bool DecodeFloating(const MiniJson::Value& value, F& out, std::string& error)
{
    if (value.kind == MiniJson::Value::String)
    {
        if (value.text == "nan")
            out = std::numeric_limits<F>::quiet_NaN();
        else if (value.text == "inf")
            out = std::numeric_limits<F>::infinity();
        else if (value.text == "-inf")
            out = -std::numeric_limits<F>::infinity();
        else
        {
            error = "bad float string '" + value.text + "'";
            return false;
        }
        return true;
    }
    if (value.kind != MiniJson::Value::Number)
    {
        error = "expected a number";
        return false;
    }
    char* end = nullptr;
    if constexpr (std::is_same_v<F, float>)
        out = std::strtof(value.text.c_str(), &end);
    else
        out = std::strtod(value.text.c_str(), &end);
    if (end == value.text.c_str() || *end != '\0')
    {
        error = "bad float '" + value.text + "'";
        return false;
    }
    return true;
}

template <typename T>
bool DecodeStruct(const MiniJson::Value& value, T& out, const DecodeContext& context, std::string& error)
{
    if (value.kind != MiniJson::Value::Object)
    {
        error = "expected an object";
        return false;
    }
    return StructFields<T>::Decode(value, out, context, error);
}

template <typename T>
bool DecodeValue(const MiniJson::Value& value, T& out, const DecodeContext& context, std::string& error)
{
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<U, bool>)
    {
        if (value.kind != MiniJson::Value::Bool)
        {
            error = "expected a boolean";
            return false;
        }
        out = value.b;
        return true;
    }
    else if constexpr (std::is_pointer_v<U>)
    {
        if constexpr (std::is_same_v<U, const ai::DungeonPlan*>)
        {
            if (value.kind == MiniJson::Value::Null)
            {
                out = nullptr;
                return context.plan == nullptr;
            }
            if (value.kind != MiniJson::Value::Object)
            {
                error = "expected an object";
                return false;
            }
            if (!context.plan)
            {
                error = "no plan passed to the decoder";
                return false;
            }
            const MiniJson::Value* name = value.Find("name");
            const MiniJson::Value* hash = value.Find("hash");
            if (!name || name->kind != MiniJson::Value::String || !hash || hash->kind != MiniJson::Value::String)
            {
                error = "expected {\"name\",\"hash\"}";
                return false;
            }
            if (name->text != context.plan->name || hash->text != PlanHash(*context.plan))
            {
                error = "plan does not match the capture's plan '" + context.plan->name + "'";
                return false;
            }
            out = context.plan;
            return true;
        }
        else if constexpr (std::is_same_v<U, const ai::BossTacticTable*>)
        {
            if (value.kind != MiniJson::Value::Bool)
            {
                error = "expected a boolean";
                return false;
            }
            if (value.b && !context.tactics)
            {
                error = "tactics set but none passed to the decoder";
                return false;
            }
            out = value.b ? context.tactics : nullptr;
            return true;
        }
        else
        {
            error = "unsupported pointer field";
            return false;
        }
    }
    else if constexpr (std::is_enum_v<U>)
    {
        using I = std::underlying_type_t<U>;
        I parsed{};
        if (!DecodeInteger(value, parsed, error))
            return false;
        out = static_cast<U>(parsed);
        return true;
    }
    else if constexpr (std::is_integral_v<U>)
        return DecodeInteger(value, out, error);
    else if constexpr (std::is_same_v<U, float> || std::is_same_v<U, double>)
        return DecodeFloating(value, out, error);
    else if constexpr (std::is_same_v<U, std::string>)
    {
        if (value.kind != MiniJson::Value::String)
        {
            error = "expected a string";
            return false;
        }
        out = value.text;
        return true;
    }
    else if constexpr (std::is_array_v<U>)
    {
        constexpr std::size_t count = std::extent_v<U>;
        if (value.kind != MiniJson::Value::Array)
        {
            error = "expected an array";
            return false;
        }
        if (value.arr.size() > count)
        {
            error = "too many array elements";
            return false;
        }
        for (std::size_t i = 0; i < value.arr.size(); ++i)
            if (!DecodeValue(value.arr[i], out[i], context, error))
                return false;
        return true;
    }
    else if constexpr (IsPair<U>::value)
    {
        if (value.kind != MiniJson::Value::Array || value.arr.size() != 2)
        {
            error = "expected [key, value]";
            return false;
        }
        if (!DecodeValue(value.arr[0], out.first, context, error))
            return false;
        return DecodeValue(value.arr[1], out.second, context, error);
    }
    else if constexpr (IsStdArray<U>::value)
    {
        constexpr std::size_t count = std::tuple_size<U>::value;
        if (value.kind != MiniJson::Value::Array)
        {
            error = "expected an array";
            return false;
        }
        if (value.arr.size() > count)
        {
            error = "too many array elements";
            return false;
        }
        for (std::size_t i = 0; i < value.arr.size(); ++i)
            if (!DecodeValue(value.arr[i], out[i], context, error))
                return false;
        return true;
    }
    else if constexpr (IsOptional<U>::value)
    {
        if (value.kind == MiniJson::Value::Null)
        {
            out.reset();
            return true;
        }
        out.emplace();
        return DecodeValue(value, *out, context, error);
    }
    else if constexpr (HasMapped<U>::value)
    {
        if (value.kind != MiniJson::Value::Array)
        {
            error = "expected an array of [key, value]";
            return false;
        }
        out.clear();
        for (const auto& entry : value.arr)
        {
            if (entry.kind != MiniJson::Value::Array || entry.arr.size() != 2)
            {
                error = "expected [key, value]";
                return false;
            }
            typename U::key_type key{};
            typename U::mapped_type mapped{};
            if (!DecodeValue(entry.arr[0], key, context, error))
                return false;
            if (!DecodeValue(entry.arr[1], mapped, context, error))
                return false;
            out.emplace(std::move(key), std::move(mapped));
        }
        return true;
    }
    else if constexpr (HasKey<U>::value)
    {
        if (value.kind != MiniJson::Value::Array)
        {
            error = "expected an array";
            return false;
        }
        out.clear();
        for (const auto& element : value.arr)
        {
            typename U::value_type parsed{};
            if (!DecodeValue(element, parsed, context, error))
                return false;
            out.insert(std::move(parsed));
        }
        return true;
    }
    else if constexpr (HasValue<U>::value)
    {
        if (value.kind != MiniJson::Value::Array)
        {
            error = "expected an array";
            return false;
        }
        out.clear();
        for (const auto& element : value.arr)
        {
            typename U::value_type parsed{};
            if (!DecodeValue(element, parsed, context, error))
                return false;
            out.push_back(std::move(parsed));
        }
        return true;
    }
    else
        return DecodeStruct(value, out, context, error);
}

inline bool DecodeFields(const MiniJson::Value& value, DungeonRun::Facts& out, std::string& error)
{
    DecodeContext const context;
    return DecodeStruct(value, out, context, error);
}

inline bool DecodeFields(const MiniJson::Value& value, DungeonRun::Run& out, const ai::DungeonPlan* plan,
                         const ai::BossTacticTable* tactics, std::string& error)
{
    DecodeContext const context{plan, tactics};
    return DecodeStruct(value, out, context, error);
}

inline bool DecodePlan(const MiniJson::Value& value, ai::DungeonPlan& out, std::string& error)
{
    if (value.kind != MiniJson::Value::Object)
    {
        error = "expected a plan object";
        return false;
    }
    const MiniJson::Value* hash = value.Find("hash");
    const MiniJson::Value* body = value.Find("body");
    if (!hash || hash->kind != MiniJson::Value::String || !body)
    {
        error = "plan without hash or body";
        return false;
    }
    DecodeContext const context;
    if (!DecodeStruct(*body, out, context, error))
        return false;
    if (PlanHash(out) != hash->text)
    {
        error = "plan body does not match its hash";
        return false;
    }
    return true;
}

inline bool DecodeTactics(const MiniJson::Value& value, ai::BossTacticTable& out, bool& set, std::string& error)
{
    if (value.kind != MiniJson::Value::Object)
    {
        error = "expected a tactics object";
        return false;
    }
    const MiniJson::Value* isSet = value.Find("set");
    const MiniJson::Value* rows = value.Find("rows");
    if (!isSet || isSet->kind != MiniJson::Value::Bool || !rows || rows->kind != MiniJson::Value::Array)
    {
        error = "tactics without set or rows";
        return false;
    }
    set = isSet->b;
    DecodeContext const context;
    for (const auto& entry : rows->arr)
    {
        ai::BossTacticRow row;
        if (!DecodeStruct(entry, row, context, error))
            return false;
        if (!out.Add(row))
        {
            error = "tactic row refused by the table";
            return false;
        }
    }
    out.Seal();
    return true;
}

// One field list per captured struct, from the generated header.
#define TB_DCAP_EACH_FIELD(name) fn(#name, obj.name);
#define TB_DCAP_DECODE_FIELD(name) \
    if (!known && entry.first == #name) \
    { \
        known = true; \
        if (!DecodeValue(entry.second, out.name, context, error)) \
        { \
            error = std::string(#name) + ": " + error; \
            return false; \
        } \
    }
#define TB_DCAP_DEFINE_STRUCT(Mangled, Type) \
    template <> \
    struct StructFields<Type> \
    { \
        template <typename Obj, typename Fn> \
        static void Each(Obj& obj, Fn&& fn) \
        { \
            TB_DCAP_FIELDS_##Mangled(TB_DCAP_EACH_FIELD) \
        } \
        template <typename Obj> \
        static bool Decode(const MiniJson::Value& value, Obj& out, const DecodeContext& context, \
                           std::string& error) \
        { \
            for (const auto& entry : value.obj) \
            { \
                bool known = false; \
                TB_DCAP_FIELDS_##Mangled(TB_DCAP_DECODE_FIELD) \
                if (!known) \
                { \
                    error = "unknown field \"" + entry.first + "\""; \
                    return false; \
                } \
            } \
            return true; \
        } \
    };

TB_DCAP_STRUCTS(TB_DCAP_DEFINE_STRUCT)

#undef TB_DCAP_DEFINE_STRUCT
#undef TB_DCAP_DECODE_FIELD
#undef TB_DCAP_EACH_FIELD

} // namespace DecisionCapture
} // namespace TortoiseBots
