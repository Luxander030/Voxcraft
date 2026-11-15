#include "core/json.h"
#include <cstdio>
#include <cstdlib>

namespace vx {

namespace {
struct Parser {
    const char* s; const char* e; std::string err;
    void ws() {
        for (;;) {
            while (s < e && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
            if (s + 1 < e && s[0] == '/' && s[1] == '/') { while (s < e && *s != '\n') s++; continue; }   // allow // comments in data files
            break;
        }
    }
    bool fail(const char* m) { if (err.empty()) err = m; return false; }
    bool value(Json& out) {
        ws(); if (s >= e) return fail("unexpected end");
        char c = *s;
        if (c == '{') {
            out.type = Json::Type::Object; s++; ws();
            if (s < e && *s == '}') { s++; return true; }
            for (;;) {
                ws(); std::string k; if (!string(k)) return false; ws();
                if (s >= e || *s != ':') return fail("expected ':'"); s++;
                Json v; if (!value(v)) return false; out.obj.emplace_back(std::move(k), std::move(v));
                ws(); if (s < e && *s == ',') { s++; ws(); if (s < e && *s == '}') { s++; return true; } continue; }
                if (s < e && *s == '}') { s++; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            out.type = Json::Type::Array; s++; ws();
            if (s < e && *s == ']') { s++; return true; }
            for (;;) {
                Json v; if (!value(v)) return false; out.arr.push_back(std::move(v));
                ws(); if (s < e && *s == ',') { s++; ws(); if (s < e && *s == ']') { s++; return true; } continue; }
                if (s < e && *s == ']') { s++; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { out.type = Json::Type::String; return string(out.str); }
        if (e - s >= 4 && !strncmp(s, "true", 4)) { out.type = Json::Type::Bool; out.b = true; s += 4; return true; }
        if (e - s >= 5 && !strncmp(s, "false", 5)) { out.type = Json::Type::Bool; out.b = false; s += 5; return true; }
        if (e - s >= 4 && !strncmp(s, "null", 4)) { out.type = Json::Type::Null; s += 4; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            std::string tmp; while (s < e && (strchr("+-.eE", *s) || (*s >= '0' && *s <= '9'))) tmp += *s++;
            out.type = Json::Type::Number; out.num = strtod(tmp.c_str(), nullptr); return true;
        }
        return fail("unexpected character");
    }
    bool string(std::string& out) {
        if (s >= e || *s != '"') return fail("expected string"); s++;
        while (s < e && *s != '"') {
            if (*s == '\\') {
                s++; if (s >= e) return fail("bad escape");
                char c = *s++;
                switch (c) {
                    case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                    case 'u': {
                        if (e - s < 4) return fail("bad \\u");
                        unsigned cp = (unsigned)strtoul(std::string(s, 4).c_str(), nullptr, 16); s += 4;
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                        else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                        break; }
                    default: out += c;
                }
            } else out += *s++;
        }
        if (s >= e) return fail("unterminated string");
        s++; return true;
    }
};
void dumpStr(std::string& o, const std::string& s) {
    o += '"';
    for (char c : s) {
        if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\"; else if (c == '\n') o += "\\n";
        else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += c;
    }
    o += '"';
}
void dumpRec(const Json& j, std::string& o, int indent, int depth) {
    auto nl = [&](int d) { if (indent) { o += '\n'; o.append((size_t)(d * indent), ' '); } };
    switch (j.type) {
        case Json::Type::Null: o += "null"; break;
        case Json::Type::Bool: o += j.b ? "true" : "false"; break;
        case Json::Type::Number: { char b[64]; if (j.num == (double)(long long)j.num && std::fabs(j.num) < 1e15) snprintf(b, sizeof b, "%lld", (long long)j.num); else snprintf(b, sizeof b, "%.17g", j.num); o += b; break; }
        case Json::Type::String: dumpStr(o, j.str); break;
        case Json::Type::Array:
            o += '['; for (size_t i = 0; i < j.arr.size(); i++) { if (i) o += ','; nl(depth + 1); dumpRec(j.arr[i], o, indent, depth + 1); }
            if (!j.arr.empty()) nl(depth); o += ']'; break;
        case Json::Type::Object:
            o += '{'; for (size_t i = 0; i < j.obj.size(); i++) { if (i) o += ','; nl(depth + 1); dumpStr(o, j.obj[i].first); o += indent ? ": " : ":"; dumpRec(j.obj[i].second, o, indent, depth + 1); }
            if (!j.obj.empty()) nl(depth); o += '}'; break;
    }
}
} // namespace

Json Json::parse(const std::string& text, std::string* error) {
    Parser p{text.data(), text.data() + text.size(), {}};
    Json out;
    if (!p.value(out)) { if (error) *error = p.err + " at offset " + std::to_string(p.s - text.data()); return Json(); }
    if (error) error->clear();
    return out;
}
std::string Json::dump(int indent) const { std::string o; dumpRec(*this, o, indent, 0); return o; }
const Json* Json::find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (auto& kv : obj) if (kv.first == key) return &kv.second;
    return nullptr;
}
const Json& Json::operator[](const std::string& key) const { static Json nul; const Json* j = find(key); return j ? *j : nul; }
void Json::set(const std::string& k, Json v) {
    type = Type::Object;
    for (auto& kv : obj) if (kv.first == k) { kv.second = std::move(v); return; }
    obj.emplace_back(k, std::move(v));
}

} // namespace vx
