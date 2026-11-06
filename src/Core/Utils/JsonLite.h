#pragma once

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Core::Utils {

enum class JsonType {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
};

class JsonValue {
public:
    using ArrayType = std::vector<JsonValue>;
    using ObjectType = std::map<std::string, JsonValue>;

    JsonValue() : mType(JsonType::Null) {}
    explicit JsonValue(bool b) : mType(JsonType::Bool), mBool(b) {}
    explicit JsonValue(double n) : mType(JsonType::Number), mNumber(n) {}
    explicit JsonValue(int64_t n) : mType(JsonType::Number), mNumber(static_cast<double>(n)), mInt(n) {}
    explicit JsonValue(std::string s) : mType(JsonType::String), mString(std::move(s)) {}
    explicit JsonValue(ArrayType a) : mType(JsonType::Array), mArray(std::move(a)) {}
    explicit JsonValue(ObjectType o) : mType(JsonType::Object), mObject(std::move(o)) {}

    JsonType Type() const { return mType; }
    bool IsNull() const { return mType == JsonType::Null; }
    bool IsBool() const { return mType == JsonType::Bool; }
    bool IsNumber() const { return mType == JsonType::Number; }
    bool IsString() const { return mType == JsonType::String; }
    bool IsArray() const { return mType == JsonType::Array; }
    bool IsObject() const { return mType == JsonType::Object; }

    bool AsBool(bool Default = false) const { return IsBool() ? mBool : Default; }
    double AsDouble(double Default = 0.0) const { return IsNumber() ? mNumber : Default; }
    float AsFloat(float Default = 0.0f) const { return IsNumber() ? static_cast<float>(mNumber) : Default; }
    int64_t AsInt64(int64_t Default = 0) const {
        if (IsNumber()) return mInt != 0 ? mInt : static_cast<int64_t>(mNumber);
        if (IsString() && mString.size() > 2 && mString[0] == '0' && (mString[1] == 'x' || mString[1] == 'X')) {
            return static_cast<int64_t>(std::strtoull(mString.c_str(), nullptr, 16));
        }
        return Default;
    }
    int AsInt(int Default = 0) const { return static_cast<int>(AsInt64(Default)); }
    uint32_t AsUInt32(uint32_t Default = 0) const { return static_cast<uint32_t>(AsInt64(Default)); }
    const std::string& AsString() const {
        static const std::string Empty;
        return IsString() ? mString : Empty;
    }
    std::string AsString(const std::string& Default) const {
        return IsString() ? mString : Default;
    }

    const ArrayType& AsArray() const {
        static const ArrayType Empty;
        return IsArray() ? mArray : Empty;
    }
    const ObjectType& AsObject() const {
        static const ObjectType Empty;
        return IsObject() ? mObject : Empty;
    }

    bool Has(const std::string& Key) const {
        if (!IsObject()) return false;
        return mObject.find(Key) != mObject.end();
    }

    const JsonValue& operator[](const std::string& Key) const {
        static const JsonValue NullVal;
        if (!IsObject()) return NullVal;
        auto It = mObject.find(Key);
        return It != mObject.end() ? It->second : NullVal;
    }

    const JsonValue& operator[](size_t Index) const {
        static const JsonValue NullVal;
        if (!IsArray() || Index >= mArray.size()) return NullVal;
        return mArray[Index];
    }

    size_t Size() const {
        if (IsArray()) return mArray.size();
        if (IsObject()) return mObject.size();
        return 0;
    }

private:
    JsonType mType = JsonType::Null;
    bool mBool = false;
    double mNumber = 0.0;
    int64_t mInt = 0;
    std::string mString;
    ArrayType mArray;
    ObjectType mObject;
};

class JsonParser {
public:
    static JsonValue ParseString(const std::string& Text) {
        size_t Pos = 0;
        SkipWhitespace(Text, Pos);
        return ParseValue(Text, Pos);
    }

    static bool ParseFile(const std::string& Path, JsonValue& OutValue) {
        std::ifstream File(Path, std::ios::binary);
        if (!File.is_open()) return false;
        std::ostringstream Buffer;
        Buffer << File.rdbuf();
        try {
            OutValue = ParseString(Buffer.str());
            return !OutValue.IsNull();
        } catch (...) {
            return false;
        }
    }

private:
    static void SkipWhitespace(const std::string& S, size_t& Pos) {
        while (Pos < S.size() && (S[Pos] == ' ' || S[Pos] == '\t' || S[Pos] == '\r' || S[Pos] == '\n')) {
            ++Pos;
        }
    }

    static JsonValue ParseValue(const std::string& S, size_t& Pos) {
        SkipWhitespace(S, Pos);
        if (Pos >= S.size()) return JsonValue();

        char C = S[Pos];
        if (C == '{') return ParseObject(S, Pos);
        if (C == '[') return ParseArray(S, Pos);
        if (C == '"') return JsonValue(ParseRawString(S, Pos));
        if (C == 't' || C == 'f') return ParseBool(S, Pos);
        if (C == 'n') return ParseNull(S, Pos);
        if (C == '-' || (C >= '0' && C <= '9')) return ParseNumber(S, Pos);
        return JsonValue();
    }

    static JsonValue ParseObject(const std::string& S, size_t& Pos) {
        JsonValue::ObjectType Obj;
        ++Pos; // skip '{'
        SkipWhitespace(S, Pos);
        if (Pos < S.size() && S[Pos] == '}') {
            ++Pos;
            return JsonValue( std::move(Obj) );
        }
        while (Pos < S.size()) {
            SkipWhitespace(S, Pos);
            if (Pos >= S.size() || S[Pos] != '"') break;
            std::string Key = ParseRawString(S, Pos);
            SkipWhitespace(S, Pos);
            if (Pos < S.size() && S[Pos] == ':') ++Pos;
            JsonValue Val = ParseValue(S, Pos);
            Obj.emplace(std::move(Key), std::move(Val));
            SkipWhitespace(S, Pos);
            if (Pos < S.size() && S[Pos] == ',') {
                ++Pos;
                continue;
            }
            if (Pos < S.size() && S[Pos] == '}') {
                ++Pos;
                break;
            }
            break;
        }
        return JsonValue(std::move(Obj));
    }

    static JsonValue ParseArray(const std::string& S, size_t& Pos) {
        JsonValue::ArrayType Arr;
        ++Pos; // skip '['
        SkipWhitespace(S, Pos);
        if (Pos < S.size() && S[Pos] == ']') {
            ++Pos;
            return JsonValue(std::move(Arr));
        }
        while (Pos < S.size()) {
            Arr.push_back(ParseValue(S, Pos));
            SkipWhitespace(S, Pos);
            if (Pos < S.size() && S[Pos] == ',') {
                ++Pos;
                continue;
            }
            if (Pos < S.size() && S[Pos] == ']') {
                ++Pos;
                break;
            }
            break;
        }
        return JsonValue(std::move(Arr));
    }

    static std::string ParseRawString(const std::string& S, size_t& Pos) {
        std::string Out;
        if (Pos < S.size() && S[Pos] == '"') ++Pos;
        while (Pos < S.size()) {
            char C = S[Pos++];
            if (C == '"') break;
            if (C == '\\' && Pos < S.size()) {
                char Esc = S[Pos++];
                switch (Esc) {
                    case '"': Out.push_back('"'); break;
                    case '\\': Out.push_back('\\'); break;
                    case '/': Out.push_back('/'); break;
                    case 'b': Out.push_back('\b'); break;
                    case 'f': Out.push_back('\f'); break;
                    case 'n': Out.push_back('\n'); break;
                    case 'r': Out.push_back('\r'); break;
                    case 't': Out.push_back('\t'); break;
                    case 'u':
                        if (Pos + 4 <= S.size()) {
                            Pos += 4;
                        }
                        break;
                    default: Out.push_back(Esc); break;
                }
            } else {
                Out.push_back(C);
            }
        }
        return Out;
    }

    static JsonValue ParseBool(const std::string& S, size_t& Pos) {
        if (S.compare(Pos, 4, "true") == 0) {
            Pos += 4;
            return JsonValue(true);
        }
        if (S.compare(Pos, 5, "false") == 0) {
            Pos += 5;
            return JsonValue(false);
        }
        ++Pos;
        return JsonValue(false);
    }

    static JsonValue ParseNull(const std::string& S, size_t& Pos) {
        if (S.compare(Pos, 4, "null") == 0) {
            Pos += 4;
        } else {
            ++Pos;
        }
        return JsonValue();
    }

    static JsonValue ParseNumber(const std::string& S, size_t& Pos) {
        size_t Start = Pos;
        bool IsFloat = false;
        if (S[Pos] == '-') ++Pos;
        while (Pos < S.size() && ((S[Pos] >= '0' && S[Pos] <= '9') || S[Pos] == '.' || S[Pos] == 'e' || S[Pos] == 'E' || S[Pos] == '+' || S[Pos] == '-')) {
            if (S[Pos] == '.' || S[Pos] == 'e' || S[Pos] == 'E') IsFloat = true;
            ++Pos;
        }
        std::string NumStr = S.substr(Start, Pos - Start);
        if (IsFloat) {
            return JsonValue(std::strtod(NumStr.c_str(), nullptr));
        }
        return JsonValue(static_cast<int64_t>(std::strtoll(NumStr.c_str(), nullptr, 10)));
    }
};

} // namespace Core::Utils
