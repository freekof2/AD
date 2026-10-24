#include "singbox_config.h"

#include <cstdlib>
#include <cctype>
#include <cstring>
#include <utility>
#include <vector>

namespace {
struct JsonValue {
    enum class Kind { Object, Array, String, Number, Boolean, Null } kind = Kind::Null;
    std::string scalar;
    std::vector<std::pair<std::string, JsonValue>> members;
    std::vector<JsonValue> elements;
};

static void AppendUtf8(std::string& out, unsigned cp) {
    if (cp <= 0x7f) out.push_back((char)cp);
    else if (cp <= 0x7ff) {
        out.push_back((char)(0xc0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back((char)(0xe0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back((char)(0x80 | (cp & 0x3f)));
    } else {
        out.push_back((char)(0xf0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back((char)(0x80 | (cp & 0x3f)));
    }
}

class JsonParser {
public:
    explicit JsonParser(const std::string& source) : source_(source) {}

    bool Parse(JsonValue& value, std::string& error) {
        SkipWhitespace();
        if (!ParseValue(value, 0)) {
            error = error_.empty() ? "invalid JSON" : error_;
            return false;
        }
        SkipWhitespace();
        if (position_ != source_.size()) {
            error = "trailing data after JSON value";
            return false;
        }
        return true;
    }

private:
    const std::string& source_;
    size_t position_ = 0;
    std::string error_;

    void SkipWhitespace() {
        while (position_ < source_.size()) {
            const char c = source_[position_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            position_++;
        }
    }

    bool Fail(const char* message) {
        if (error_.empty()) error_ = message;
        return false;
    }

    bool ReadHex4(unsigned& value) {
        if (position_ + 4 > source_.size()) return Fail("short unicode escape");
        value = 0;
        for (int i = 0; i < 4; i++) {
            const char c = source_[position_++];
            unsigned digit;
            if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
            else return Fail("invalid unicode escape");
            value = (value << 4) | digit;
        }
        return true;
    }

    bool ParseString(std::string& value) {
        if (position_ >= source_.size() || source_[position_] != '"')
            return Fail("expected JSON string");
        position_++;
        value.clear();
        while (position_ < source_.size()) {
            unsigned char c = (unsigned char)source_[position_++];
            if (c == '"') return true;
            if (c < 0x20) return Fail("control character in JSON string");
            if (c != '\\') { value.push_back((char)c); continue; }
            if (position_ >= source_.size()) return Fail("unterminated JSON escape");
            const char escape = source_[position_++];
            switch (escape) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u': {
                unsigned cp = 0;
                if (!ReadHex4(cp)) return false;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (position_ + 2 > source_.size() || source_[position_] != '\\' ||
                        source_[position_ + 1] != 'u')
                        return Fail("missing low surrogate");
                    position_ += 2;
                    unsigned low = 0;
                    if (!ReadHex4(low)) return false;
                    if (low < 0xdc00 || low > 0xdfff)
                        return Fail("invalid low surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                    return Fail("unexpected low surrogate");
                }
                AppendUtf8(value, cp);
                break;
            }
            default: return Fail("invalid JSON escape");
            }
        }
        return Fail("unterminated JSON string");
    }

    bool ParseNumber(std::string& value) {
        const size_t start = position_;
        if (position_ < source_.size() && source_[position_] == '-') position_++;
        if (position_ >= source_.size()) return Fail("invalid JSON number");
        if (source_[position_] == '0') position_++;
        else {
            if (source_[position_] < '1' || source_[position_] > '9')
                return Fail("invalid JSON number");
            while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9')
                position_++;
        }
        if (position_ < source_.size() && source_[position_] == '.') {
            position_++;
            const size_t fraction = position_;
            while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9')
                position_++;
            if (position_ == fraction) return Fail("invalid JSON fraction");
        }
        if (position_ < source_.size() && (source_[position_] == 'e' || source_[position_] == 'E')) {
            position_++;
            if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-'))
                position_++;
            const size_t exponent = position_;
            while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9')
                position_++;
            if (position_ == exponent) return Fail("invalid JSON exponent");
        }
        value = source_.substr(start, position_ - start);
        return true;
    }

    bool ParseValue(JsonValue& value, unsigned depth) {
        if (depth > 128) return Fail("JSON nesting limit exceeded");
        SkipWhitespace();
        if (position_ >= source_.size()) return Fail("unexpected end of JSON");
        const char c = source_[position_];
        if (c == '{') {
            value = JsonValue{};
            value.kind = JsonValue::Kind::Object;
            position_++;
            SkipWhitespace();
            if (position_ < source_.size() && source_[position_] == '}') {
                position_++;
                return true;
            }
            for (;;) {
                SkipWhitespace();
                std::string key;
                if (!ParseString(key)) return false;
                SkipWhitespace();
                if (position_ >= source_.size() || source_[position_++] != ':')
                    return Fail("expected colon in JSON object");
                JsonValue member;
                if (!ParseValue(member, depth + 1)) return false;
                value.members.emplace_back(std::move(key), std::move(member));
                SkipWhitespace();
                if (position_ >= source_.size()) return Fail("unterminated JSON object");
                const char delimiter = source_[position_++];
                if (delimiter == '}') return true;
                if (delimiter != ',') return Fail("expected comma in JSON object");
            }
        }
        if (c == '[') {
            value = JsonValue{};
            value.kind = JsonValue::Kind::Array;
            position_++;
            SkipWhitespace();
            if (position_ < source_.size() && source_[position_] == ']') {
                position_++;
                return true;
            }
            for (;;) {
                JsonValue item;
                if (!ParseValue(item, depth + 1)) return false;
                value.elements.push_back(std::move(item));
                SkipWhitespace();
                if (position_ >= source_.size()) return Fail("unterminated JSON array");
                const char delimiter = source_[position_++];
                if (delimiter == ']') return true;
                if (delimiter != ',') return Fail("expected comma in JSON array");
            }
        }
        if (c == '"') {
            value = JsonValue{};
            value.kind = JsonValue::Kind::String;
            return ParseString(value.scalar);
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            value = JsonValue{};
            value.kind = JsonValue::Kind::Number;
            return ParseNumber(value.scalar);
        }
        auto literal = [&](const char* text, JsonValue::Kind kind) {
            const size_t length = std::strlen(text);
            if (source_.compare(position_, length, text) != 0) return false;
            position_ += length;
            value = JsonValue{};
            value.kind = kind;
            value.scalar = text;
            return true;
        };
        if (literal("true", JsonValue::Kind::Boolean) ||
            literal("false", JsonValue::Kind::Boolean) ||
            literal("null", JsonValue::Kind::Null)) return true;
        return Fail("invalid JSON value");
    }
};

static JsonValue* FindMember(JsonValue& object, const std::string& key) {
    if (object.kind != JsonValue::Kind::Object) return nullptr;
    for (auto it = object.members.rbegin(); it != object.members.rend(); ++it)
        if (it->first == key) return &it->second;
    return nullptr;
}

static const JsonValue* FindMember(const JsonValue& object, const std::string& key) {
    if (object.kind != JsonValue::Kind::Object) return nullptr;
    for (auto it = object.members.rbegin(); it != object.members.rend(); ++it)
        if (it->first == key) return &it->second;
    return nullptr;
}

static void SetMember(JsonValue& object, const std::string& key, JsonValue value) {
    if (JsonValue* existing = FindMember(object, key)) {
        *existing = std::move(value);
        return;
    }
    object.members.emplace_back(key, std::move(value));
}

static std::string NormalizeWindowsPath(const std::string& raw) {
    std::string path = raw;
    for (char& c : path) if (c == '/') c = '\\';
    std::string prefix;
    size_t position = 0;
    bool rooted = false;
    if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') {
        prefix = "\\\\";
        position = 2;
        rooted = true;
    } else if (path.size() >= 2 && std::isalpha((unsigned char)path[0]) && path[1] == ':') {
        prefix = path.substr(0, 2);
        position = 2;
        if (position < path.size() && path[position] == '\\') {
            prefix.push_back('\\');
            position++;
            rooted = true;
        } else {
            // A drive-relative path is treated as rooted so it cannot be mistaken
            // for a path under the config directory.
            prefix.push_back('\\');
            rooted = true;
        }
    } else if (!path.empty() && path[0] == '\\') {
        prefix = "\\";
        position = 1;
        rooted = true;
    }

    std::vector<std::string> parts;
    while (position <= path.size()) {
        const size_t next = path.find('\\', position);
        const size_t end = next == std::string::npos ? path.size() : next;
        const std::string part = path.substr(position, end - position);
        if (!part.empty() && part != ".") {
            if (part == "..") {
                if (!parts.empty() && parts.back() != "..") parts.pop_back();
                else if (!rooted) parts.push_back(part);
            } else {
                parts.push_back(part);
            }
        }
        if (next == std::string::npos) break;
        position = next + 1;
    }
    std::string normalized = prefix;
    for (const std::string& part : parts) {
        if (!normalized.empty() && normalized.back() != '\\') normalized.push_back('\\');
        normalized += part;
    }
    if (normalized.empty()) return ".";
    return normalized;
}

static bool IsAbsoluteWindowsPath(const std::string& path) {
    return (!path.empty() && (path[0] == '\\' || path[0] == '/')) ||
        (path.size() >= 2 && std::isalpha((unsigned char)path[0]) && path[1] == ':');
}

static std::string DirectoryName(const std::string& path) {
    const std::string normalized = NormalizeWindowsPath(path);
    const size_t slash = normalized.find_last_of('\\');
    if (slash == std::string::npos) return {};
    if (slash == 2 && normalized.size() >= 3 && normalized[1] == ':')
        return normalized.substr(0, 3);
    if (slash == 0) return "\\";
    return normalized.substr(0, slash);
}

static std::string BaseName(const std::string& path) {
    std::string normalized = path;
    for (char& c : normalized) if (c == '/') c = '\\';
    while (!normalized.empty() && normalized.back() == '\\') normalized.pop_back();
    const size_t slash = normalized.find_last_of('\\');
    return slash == std::string::npos ? normalized : normalized.substr(slash + 1);
}

static std::string JoinWindowsPath(const std::string& directory, const std::string& leaf) {
    if (directory.empty()) return leaf;
    if (directory.back() == '\\') return directory + leaf;
    return directory + "\\" + leaf;
}

static std::string FoldWindowsPath(const std::string& path) {
    std::string folded = NormalizeWindowsPath(path);
    for (char& c : folded) c = (char)std::tolower((unsigned char)c);
    while (folded.size() > 3 && folded.back() == '\\') folded.pop_back();
    return folded;
}

static JsonValue StringValue(const std::string& text) {
    JsonValue value;
    value.kind = JsonValue::Kind::String;
    value.scalar = text;
    return value;
}

static JsonValue NumberValue(uint16_t number) {
    JsonValue value;
    value.kind = JsonValue::Kind::Number;
    value.scalar = std::to_string(number);
    return value;
}

static bool GetString(const JsonValue& object, const std::string& key, std::string& value) {
    const JsonValue* member = FindMember(object, key);
    if (!member || member->kind != JsonValue::Kind::String) return false;
    value = member->scalar;
    return true;
}

static bool GetPort(const JsonValue& object, uint16_t& port) {
    const JsonValue* member = FindMember(object, "listen_port");
    if (!member || member->kind != JsonValue::Kind::Number || member->scalar.empty()) return false;
    for (char c : member->scalar)
        if (c < '0' || c > '9') return false;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(member->scalar.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed < 1 || parsed > 65535) return false;
    port = (uint16_t)parsed;
    return true;
}

static bool IsSupportedInbound(const JsonValue& inbound, std::string& type) {
    if (!GetString(inbound, "type", type)) return false;
    return type == "socks" || type == "mixed";
}

static bool FindSocksInbound(JsonValue& root, JsonValue*& result, std::string& type,
    uint16_t& port, std::string& error) {
    JsonValue* inbounds = FindMember(root, "inbounds");
    if (!inbounds || inbounds->kind != JsonValue::Kind::Array) {
        error = "config has no inbounds array";
        return false;
    }
    result = nullptr;
    unsigned found = 0;
    for (JsonValue& inbound : inbounds->elements) {
        std::string candidate;
        if (!IsSupportedInbound(inbound, candidate)) continue;
        found++;
        result = &inbound;
        type = candidate;
        if (!GetPort(inbound, port)) {
            error = "SOCKS-compatible inbound has invalid listen_port";
            return false;
        }
    }
    if (found == 0) {
        error = "config has no SOCKS-compatible socks/mixed inbound";
        return false;
    }
    if (found != 1) {
        error = "config has multiple SOCKS-compatible inbounds";
        return false;
    }
    return true;
}

static void AppendJsonString(const std::string& value, std::string& out) {
    static const char hex[] = "0123456789abcdef";
    out.push_back('"');
    for (unsigned char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 0x0f]);
            } else out.push_back((char)c);
        }
    }
    out.push_back('"');
}

static void Serialize(const JsonValue& value, std::string& out) {
    switch (value.kind) {
    case JsonValue::Kind::Object:
        out.push_back('{');
        for (size_t i = 0; i < value.members.size(); i++) {
            if (i) out.push_back(',');
            AppendJsonString(value.members[i].first, out);
            out.push_back(':');
            Serialize(value.members[i].second, out);
        }
        out.push_back('}');
        break;
    case JsonValue::Kind::Array:
        out.push_back('[');
        for (size_t i = 0; i < value.elements.size(); i++) {
            if (i) out.push_back(',');
            Serialize(value.elements[i], out);
        }
        out.push_back(']');
        break;
    case JsonValue::Kind::String:
        AppendJsonString(value.scalar, out);
        break;
    case JsonValue::Kind::Number:
    case JsonValue::Kind::Boolean:
        out += value.scalar;
        break;
    case JsonValue::Kind::Null:
        out += "null";
        break;
    }
}

static bool ParseDocument(const std::string& json, JsonValue& root, std::string& error) {
    error.clear();
    JsonParser parser(json);
    if (!parser.Parse(root, error)) return false;
    if (root.kind != JsonValue::Kind::Object) {
        error = "config root must be an object";
        return false;
    }
    return true;
}
} // namespace

bool SbParseConfig(const std::string& json, SingBoxConfigInfo& info, std::string& error) {
    JsonValue root;
    if (!ParseDocument(json, root, error)) return false;
    JsonValue* inbound = nullptr;
    std::string inboundType;
    uint16_t listenPort = 0;
    if (!FindSocksInbound(root, inbound, inboundType, listenPort, error)) return false;

    SingBoxConfigInfo parsed{};
    parsed.inboundType = inboundType;
    parsed.listenPort = listenPort;
    GetString(*inbound, "listen", parsed.inboundListen);

    if (const JsonValue* experimental = FindMember(root, "experimental")) {
        if (experimental->kind != JsonValue::Kind::Object) {
            error = "experimental must be an object";
            return false;
        }
        if (const JsonValue* clash = FindMember(*experimental, "clash_api")) {
            if (clash->kind != JsonValue::Kind::Object) {
                error = "experimental.clash_api must be an object";
                return false;
            }
            const JsonValue* controller = FindMember(*clash, "external_controller");
            if (controller && controller->kind != JsonValue::Kind::String) {
                error = "clash_api.external_controller must be a string";
                return false;
            }
            if (controller) parsed.controller = controller->scalar;
            const JsonValue* secret = FindMember(*clash, "secret");
            if (secret && secret->kind != JsonValue::Kind::String) {
                error = "clash_api.secret must be a string";
                return false;
            }
            if (secret) parsed.apiSecret = secret->scalar;
        }
        if (const JsonValue* cache = FindMember(*experimental, "cache_file")) {
            if (cache->kind != JsonValue::Kind::Object) {
                error = "experimental.cache_file must be an object";
                return false;
            }
            const JsonValue* enabled = FindMember(*cache, "enabled");
            if (enabled && enabled->kind != JsonValue::Kind::Boolean) {
                error = "cache_file.enabled must be a boolean";
                return false;
            }
            parsed.cacheEnabled = enabled && enabled->kind == JsonValue::Kind::Boolean &&
                enabled->scalar == "true";
            const JsonValue* path = FindMember(*cache, "path");
            if (path && path->kind != JsonValue::Kind::String) {
                error = "cache_file.path must be a string";
                return false;
            }
            if (path) parsed.cachePath = path->scalar;
        }
    }
    info = std::move(parsed);
    error.clear();
    return true;
}

bool SbNormalizeCacheFilePath(const std::string& json, const std::string& configPath,
    std::string& normalizedJson, std::string& cachePath, bool& changed,
    std::string& error) {
    normalizedJson = json;
    cachePath.clear();
    changed = false;
    error.clear();
    const std::string configDirectory = DirectoryName(configPath);
    if (configDirectory.empty()) {
        error = "config path must include its containing directory";
        return false;
    }

    JsonValue root;
    if (!ParseDocument(json, root, error)) return false;
    JsonValue* experimental = FindMember(root, "experimental");
    if (!experimental) return true;
    if (experimental->kind != JsonValue::Kind::Object) {
        error = "experimental must be an object";
        return false;
    }
    JsonValue* cache = FindMember(*experimental, "cache_file");
    if (!cache) return true;
    if (cache->kind != JsonValue::Kind::Object) {
        error = "experimental.cache_file must be an object";
        return false;
    }
    const JsonValue* enabled = FindMember(*cache, "enabled");
    if (enabled && enabled->kind != JsonValue::Kind::Boolean) {
        error = "cache_file.enabled must be a boolean";
        return false;
    }
    const bool cacheEnabled = enabled && enabled->scalar == "true";
    const JsonValue* path = FindMember(*cache, "path");
    if (path && path->kind != JsonValue::Kind::String) {
        error = "cache_file.path must be a string";
        return false;
    }
    if (!path && !cacheEnabled) return true;

    const std::string currentPath = path ? path->scalar : std::string();
    if (!currentPath.empty()) {
        const std::string resolvedPath = IsAbsoluteWindowsPath(currentPath)
            ? currentPath : JoinWindowsPath(configDirectory, currentPath);
        if (FoldWindowsPath(DirectoryName(resolvedPath)) == FoldWindowsPath(configDirectory)) {
            cachePath = currentPath;
            return true;
        }
    }

    std::string fileName = BaseName(currentPath);
    if (fileName.empty() || fileName == "." || fileName == "..") fileName = "cache.db";
    cachePath = JoinWindowsPath(NormalizeWindowsPath(configDirectory), fileName);
    SetMember(*cache, "path", StringValue(cachePath));
    changed = true;
    normalizedJson.clear();
    Serialize(root, normalizedJson);
    return true;
}

bool SbUpdateListenPortConfig(const std::string& sourceJson,
    const std::string& requestedPort, std::string& updatedJson,
    uint16_t& portOut, bool& changed, std::string& error) {
    updatedJson = sourceJson;
    portOut = 0;
    changed = false;
    error.clear();
    if (requestedPort.empty() || requestedPort.find_first_not_of("0123456789") != std::string::npos) {
        error = "listen_port must contain decimal digits only";
        return false;
    }
    char* end = nullptr;
    const unsigned long requested = std::strtoul(requestedPort.c_str(), &end, 10);
    if (!end || *end != '\0' || requested < 1 || requested > 65535) {
        error = "listen_port must be in the range 1..65535";
        return false;
    }

    JsonValue root;
    if (!ParseDocument(sourceJson, root, error)) return false;
    JsonValue* inbound = nullptr;
    std::string inboundType;
    uint16_t originalPort = 0;
    if (!FindSocksInbound(root, inbound, inboundType, originalPort, error)) return false;

    portOut = (uint16_t)requested;
    changed = (portOut != originalPort);
    if (!changed) return true;
    SetMember(*inbound, "listen_port", NumberValue(portOut));
    updatedJson.clear();
    Serialize(root, updatedJson);
    return true;
}

bool SbBuildRuntimeConfig(const std::string& sourceJson, uint16_t listenPort,
    uint16_t apiPort, const std::string& cachePath, const std::string& apiSecret,
    std::string& runtimeJson, std::string& error) {
    if (listenPort == 0 || apiPort == 0 || listenPort == apiPort) {
        error = "runtime listen and API ports must be valid and distinct";
        return false;
    }
    JsonValue root;
    if (!ParseDocument(sourceJson, root, error)) return false;
    SingBoxConfigInfo sourceInfo{};
    if (!SbParseConfig(sourceJson, sourceInfo, error)) return false;

    JsonValue* inbound = nullptr;
    std::string inboundType;
    uint16_t sourcePort = 0;
    if (!FindSocksInbound(root, inbound, inboundType, sourcePort, error)) return false;
    SetMember(*inbound, "listen", StringValue("127.0.0.1"));
    SetMember(*inbound, "listen_port", NumberValue(listenPort));

    JsonValue* experimental = FindMember(root, "experimental");
    if (!experimental) {
        JsonValue value;
        value.kind = JsonValue::Kind::Object;
        SetMember(root, "experimental", std::move(value));
        experimental = FindMember(root, "experimental");
    }
    if (!experimental || experimental->kind != JsonValue::Kind::Object) {
        error = "experimental must be an object";
        return false;
    }
    JsonValue* clash = FindMember(*experimental, "clash_api");
    if (!clash) {
        JsonValue value;
        value.kind = JsonValue::Kind::Object;
        SetMember(*experimental, "clash_api", std::move(value));
        clash = FindMember(*experimental, "clash_api");
    }
    if (!clash || clash->kind != JsonValue::Kind::Object) {
        error = "experimental.clash_api must be an object";
        return false;
    }
    SetMember(*clash, "external_controller",
        StringValue("127.0.0.1:" + std::to_string(apiPort)));
    if (!apiSecret.empty()) SetMember(*clash, "secret", StringValue(apiSecret));

    if (sourceInfo.cacheEnabled) {
        if (cachePath.empty()) {
            error = "enabled cache_file requires a per-profile path";
            return false;
        }
        JsonValue* cache = FindMember(*experimental, "cache_file");
        if (!cache || cache->kind != JsonValue::Kind::Object) {
            error = "experimental.cache_file must be an object";
            return false;
        }
        SetMember(*cache, "path", StringValue(cachePath));
    }

    runtimeJson.clear();
    Serialize(root, runtimeJson);
    error.clear();
    return true;
}
