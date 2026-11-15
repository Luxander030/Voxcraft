// Minimal JSON DOM for data-driven registries (blocks, items, recipes, settings).
// Not on any hot path: parsed once at startup.
#pragma once
#include "core/common.h"
#include <map>
#include <memory>
#include <string>

namespace vx {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;   // insertion order preserved

    static Json parse(const std::string& text, std::string* error = nullptr);
    std::string dump(int indent = 0) const;

    bool isNull() const { return type == Type::Null; }
    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }
    const Json* find(const std::string& key) const;
    const Json& operator[](const std::string& key) const;
    const Json& operator[](size_t i) const { static Json nul; return i < arr.size() ? arr[i] : nul; }
    size_t size() const { return type == Type::Array ? arr.size() : obj.size(); }

    std::string asString(const std::string& def = "") const { return type == Type::String ? str : def; }
    double asNumber(double def = 0) const { return type == Type::Number ? num : (type == Type::Bool ? (b ? 1 : 0) : def); }
    int asInt(int def = 0) const { return type == Type::Number ? (int)num : def; }
    bool asBool(bool def = false) const { return type == Type::Bool ? b : (type == Type::Number ? num != 0 : def); }

    // builders
    static Json makeObject() { Json j; j.type = Type::Object; return j; }
    static Json makeArray() { Json j; j.type = Type::Array; return j; }
    static Json of(double v) { Json j; j.type = Type::Number; j.num = v; return j; }
    static Json of(bool v) { Json j; j.type = Type::Bool; j.b = v; return j; }
    static Json of(const std::string& v) { Json j; j.type = Type::String; j.str = v; return j; }
    void set(const std::string& k, Json v);
    void push(Json v) { arr.push_back(std::move(v)); }
};

} // namespace vx
