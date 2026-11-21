#include "fp_json.h"
#include "fp_cookies.h"

#include <utility>
#include <vector>

namespace {
static size_t SkipWs(const std::string& json, size_t position) {
    while (position < json.size() && (json[position] == ' ' || json[position] == '\t' ||
        json[position] == '\r' || json[position] == '\n')) position++;
    return position;
}

static size_t SkipValue(const std::string& json, size_t position) {
    position = SkipWs(json, position);
    if (position >= json.size()) return position;
    if (json[position] == '"') {
        position++;
        while (position < json.size()) {
            if (json[position] == '\\') { position += 2; continue; }
            if (json[position] == '"') return position + 1;
            position++;
        }
        return position;
    }
    if (json[position] == '{' || json[position] == '[') {
        const char open = json[position];
        const char close = open == '{' ? '}' : ']';
        int depth = 0;
        bool inString = false;
        for (; position < json.size(); position++) {
            if (inString) {
                if (json[position] == '\\') position++;
                else if (json[position] == '"') inString = false;
            } else {
                if (json[position] == '"') inString = true;
                else if (json[position] == open) depth++;
                else if (json[position] == close && --depth == 0) return position + 1;
            }
        }
        return position;
    }
    while (position < json.size() && json[position] != ',' && json[position] != '}' &&
        json[position] != ']') position++;
    while (position > 0 && (json[position - 1] == ' ' || json[position - 1] == '\t' ||
        json[position - 1] == '\r' || json[position - 1] == '\n')) position--;
    return position;
}

static std::string QuoteJsonString(const std::string& value) {
    static const char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char c : value) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 0x20) {
                result += "\\u00";
                result.push_back(hex[c >> 4]);
                result.push_back(hex[c & 0x0f]);
            } else result.push_back((char)c);
        }
    }
    result.push_back('"');
    return result;
}

static bool SetString(std::string& json, const char* key, const std::string& value,
    std::string& error) {
    const std::string updated = FpJsonSet(json, key, QuoteJsonString(value));
    if (updated.empty()) {
        error = std::string("invalid JSON while updating ") + key;
        return false;
    }
    json = updated;
    return true;
}
} // namespace

std::string FpJsonGet(const std::string& json, const std::string& key) {
    size_t position = SkipWs(json, 0);
    if (position >= json.size() || json[position] != '{') return "";
    position++;
    const std::string quotedKey = "\"" + key + "\"";
    while (position < json.size()) {
        position = SkipWs(json, position);
        if (position >= json.size() || json[position] == '}') break;
        if (json.compare(position, quotedKey.size(), quotedKey) == 0) {
            position += quotedKey.size();
            position = SkipWs(json, position);
            if (position < json.size() && json[position] == ':') {
                const size_t valueStart = SkipWs(json, position + 1);
                const size_t valueEnd = SkipValue(json, valueStart);
                return json.substr(valueStart, valueEnd - valueStart);
            }
            return "";
        }
        position = SkipValue(json, position);
        position = SkipWs(json, position);
        if (position < json.size() && json[position] == ':')
            position = SkipValue(json, position + 1);
        position = SkipWs(json, position);
        if (position < json.size() && json[position] == ',') position++;
    }
    return "";
}

std::string FpJsonSet(const std::string& json, const std::string& key,
    const std::string& valueRaw) {
    if (valueRaw.empty()) return "";
    size_t position = SkipWs(json, 0);
    if (position >= json.size() || json[position] != '{') return "";
    const std::string quotedKey = "\"" + key + "\"";
    position++;
    while (position < json.size()) {
        const size_t memberStart = SkipWs(json, position);
        if (memberStart >= json.size() || json[memberStart] == '}') break;
        if (json.compare(memberStart, quotedKey.size(), quotedKey) == 0) {
            size_t colon = SkipWs(json, memberStart + quotedKey.size());
            if (colon >= json.size() || json[colon] != ':') return "";
            const size_t valueStart = SkipWs(json, colon + 1);
            const size_t valueEnd = SkipValue(json, valueStart);
            if (valueEnd <= valueStart) return "";
            std::string updated = json;
            updated.replace(valueStart, valueEnd - valueStart, valueRaw);
            return updated;
        }
        position = SkipValue(json, memberStart);
        position = SkipWs(json, position);
        if (position < json.size() && json[position] == ':')
            position = SkipValue(json, position + 1);
        position = SkipWs(json, position);
        if (position < json.size() && json[position] == ',') position++;
    }
    const size_t objectEnd = json.find_last_of('}');
    if (objectEnd == std::string::npos) return "";
    bool emptyObject = true;
    for (size_t i = 0; i < objectEnd; i++) {
        const char c = json[i];
        if (c != '{' && c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            emptyObject = false;
            break;
        }
    }
    std::string updated = json;
    updated.insert(objectEnd, (emptyObject ? "" : ",") + quotedKey + ":" + valueRaw);
    return updated;
}

bool FpBuildProxySaveJson(const std::string& existingUiJson,
    const std::string& existingStaticJson, const std::string& proxyType,
    const std::string& proxyHost, const std::string& proxyPort,
    const std::string& proxyUser, const std::string& proxyPass,
    FpProxyJsonPair& result, std::string& error) {
    error.clear();
    std::string ui = existingUiJson.empty() ? "{}" : existingUiJson;
    std::string staticJson = existingStaticJson.empty() ? "{}" : existingStaticJson;
    if (!SetString(ui, "proxyType", proxyType, error) ||
        !SetString(ui, "proxyHost", proxyHost, error) ||
        !SetString(ui, "proxyPort", proxyPort, error) ||
        !SetString(ui, "proxyUser", proxyUser, error) ||
        !SetString(ui, "proxyPass", proxyPass, error)) return false;

    const std::string scheme = proxyType == "sing-box" ? "socks5" : proxyType;
    std::string chain = "[]";
    if (!scheme.empty() && scheme != "noProxy" && scheme != "noproxy" &&
        !proxyHost.empty() && !proxyPort.empty()) {
        chain = "[{\"scheme\":" + QuoteJsonString(scheme) +
            ",\"host\":" + QuoteJsonString(proxyHost) +
            ",\"port\":" + QuoteJsonString(proxyPort) +
            ",\"account\":" + QuoteJsonString(proxyUser) +
            ",\"password\":" + QuoteJsonString(proxyPass) + "}]";
    }
    const std::string updatedStatic = FpJsonSet(staticJson, "ProxyChain", chain);
    if (updatedStatic.empty()) {
        error = "invalid static JSON while updating ProxyChain";
        return false;
    }
    result.uiJson = std::move(ui);
    result.staticJson = updatedStatic;
    return true;
}

std::string FpBuildSpeechVoicesJson(const std::string& cachedVoicesJson,
    const std::string& language, bool mobile) {
    size_t first = cachedVoicesJson.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || cachedVoicesJson[first] != '[')
        return mobile ? "[]" : "";

    const std::vector<std::string> voices = FpCookieArraySplit(cachedVoicesJson);
    if (voices.empty()) {
        const size_t last = cachedVoicesJson.find_last_not_of(" \t\r\n");
        const std::string trimmed = cachedVoicesJson.substr(first, last - first + 1);
        return trimmed == "[]" ? "[]" : "";
    }

    std::string locale = language;
    const size_t separator = locale.find_first_of(",; \t\r\n");
    if (separator != std::string::npos) locale.resize(separator);
    if (locale.size() >= 2 && locale.front() == '"' && locale.back() == '"')
        locale = FpCookieUnescape(locale.substr(1, locale.size() - 2));

    size_t selected = voices.size();
    if (!locale.empty()) {
        for (size_t i = 0; i < voices.size(); ++i) {
            if (FpCookieGetStr(voices[i], "lang").find(locale) != std::string::npos) {
                selected = i;
                break;
            }
        }
        if (selected == voices.size()) {
            const size_t dash = locale.find('-');
            if (dash != std::string::npos) {
                const std::string base = locale.substr(0, dash);
                for (size_t i = 0; i < voices.size(); ++i) {
                    if (FpCookieGetStr(voices[i], "lang").find(base) != std::string::npos) {
                        selected = i;
                        break;
                    }
                }
            }
        }
    }
    if (mobile && selected == voices.size()) return "[]";

    std::string result = "[";
    for (size_t i = 0; i < voices.size(); ++i) {
        if (i != 0) result += ",";
        std::string voice = FpJsonSet(voices[i], "default", i == selected ? "true" : "false");
        if (voice.empty()) return "";
        voice = FpJsonSet(voice, "localService", i == selected ? "true" : "false");
        if (voice.empty()) return "";
        result += voice;
    }
    result += "]";
    return result;
}
