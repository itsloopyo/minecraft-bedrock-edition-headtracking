#include "cameraunlock/config/ini_reader.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <clocale>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace cameraunlock {

constexpr size_t kMaxIniValueLength = 1024;

#ifdef _MSC_VER
// One "C" locale shared by the read and the write side. Both strtod and printf's %g
// follow LC_NUMERIC, and a consumer built /MD shares the game's CRT - so a title that
// calls setlocale(LC_ALL, "") on a German install would otherwise turn Sensitivity=2.5
// into 2.0 on read, or write it back as "2,5". Pinning only one side leaves the round
// trip broken in the other direction.
static _locale_t CNumericLocale() {
    static const _locale_t loc = _create_locale(LC_NUMERIC, "C");
    return loc;
}
#endif

bool IniReader::Open(const std::string& path) {
    if (path.empty()) {
        LogError("Empty path provided to IniReader::Open");
        return false;
    }

#ifdef _WIN32
    // Check if file exists
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
#endif

    m_path = path;
    RefreshModTime();
    return true;
}

void IniReader::Close() {
    m_path.clear();
#ifdef _WIN32
    m_lastModTime = {};
#else
    m_lastModTime = 0;
#endif
}

bool IniReader::HasChanged() const {
    if (m_path.empty()) {
        return false;
    }

#ifdef _WIN32
    // Attribute query rather than open/query/close: this is polled every frame
    // by hot-reload consumers.
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (!GetFileAttributesExA(m_path.c_str(), GetFileExInfoStandard, &attrs)) {
        return false;
    }

    return CompareFileTime(&m_lastModTime, &attrs.ftLastWriteTime) != 0;
#else
    struct stat st;
    if (stat(m_path.c_str(), &st) != 0) {
        return false;
    }
    return st.st_mtime != m_lastModTime;
#endif
}

void IniReader::RefreshModTime() {
    if (m_path.empty()) {
        return;
    }

#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (GetFileAttributesExA(m_path.c_str(), GetFileExInfoStandard, &attrs)) {
        m_lastModTime = attrs.ftLastWriteTime;
    }
#else
    struct stat st;
    if (stat(m_path.c_str(), &st) == 0) {
        m_lastModTime = st.st_mtime;
    }
#endif
}

std::string IniReader::ReadString(const char* section, const char* key, const char* defaultValue) const {
    if (m_path.empty()) {
        return defaultValue;
    }

#ifdef _WIN32
    char buffer[kMaxIniValueLength];
    DWORD result = GetPrivateProfileStringA(
        section,
        key,
        defaultValue,
        buffer,
        sizeof(buffer),
        m_path.c_str());

    if (result == 0 && GetLastError() != ERROR_SUCCESS) {
        return defaultValue;
    }

    return std::string(buffer);
#else
    // Simple INI parsing for non-Windows platforms
    FILE* file = fopen(m_path.c_str(), "r");
    if (!file) {
        return defaultValue;
    }

    char line[kMaxIniValueLength];
    bool inSection = false;
    std::string sectionStr = std::string("[") + section + "]";
    std::string keyStr = key;

    while (fgets(line, sizeof(line), file)) {
        // Trim whitespace
        char* start = line;
        while (*start && std::isspace(static_cast<unsigned char>(*start))) start++;

        // Check for section
        if (*start == '[') {
            inSection = (strncmp(start, sectionStr.c_str(), sectionStr.length()) == 0);
            continue;
        }

        if (!inSection) continue;

        // Parse key=value
        char* eq = strchr(start, '=');
        if (!eq) continue;

        // Extract key
        *eq = '\0';
        char* keyEnd = eq - 1;
        while (keyEnd > start && std::isspace(static_cast<unsigned char>(*keyEnd))) *keyEnd-- = '\0';

        if (strcmp(start, key) != 0) continue;

        // Extract value
        char* value = eq + 1;
        while (*value && std::isspace(static_cast<unsigned char>(*value))) value++;

        // Remove trailing whitespace and newline. Guard against empty value
        // (e.g. "key="): stepping back from value would compute `value - 1`,
        // which is undefined pointer arithmetic before the start of the buffer.
        size_t valueLen = strlen(value);
        if (valueLen > 0) {
            char* valueEnd = value + valueLen - 1;
            while (valueEnd >= value && (std::isspace(static_cast<unsigned char>(*valueEnd))
                                         || *valueEnd == '\n' || *valueEnd == '\r')) {
                *valueEnd = '\0';
                if (valueEnd == value) break;
                --valueEnd;
            }
        }

        fclose(file);
        return std::string(value);
    }

    fclose(file);
    return defaultValue;
#endif
}

int IniReader::ReadInt(const char* section, const char* key, int defaultValue) const {
    if (m_path.empty()) {
        return defaultValue;
    }

#ifdef _WIN32
    return GetPrivateProfileIntA(section, key, defaultValue, m_path.c_str());
#else
    std::string str = ReadString(section, key, "");
    if (str.empty()) return defaultValue;

    char* end;
    long value = strtol(str.c_str(), &end, 10);
    if (end == str.c_str()) return defaultValue;
    return static_cast<int>(value);
#endif
}

unsigned int IniReader::ReadUInt(const char* section, const char* key, unsigned int defaultValue) const {
    int value = ReadInt(section, key, static_cast<int>(defaultValue));
    return value >= 0 ? static_cast<unsigned int>(value) : defaultValue;
}

int64_t IniReader::ReadInt64(const char* section, const char* key, int64_t defaultValue) const {
    std::string str = ReadString(section, key, "");
    if (str.empty()) return defaultValue;

    char* end;
    int64_t value = strtoll(str.c_str(), &end, 10);
    if (end == str.c_str()) return defaultValue;
    return value;
}

double IniReader::ReadDouble(const char* section, const char* key, double defaultValue) const {
    std::string str = ReadString(section, key, "");
    if (str.empty()) return defaultValue;

    char* end;
#ifdef _MSC_VER
    double value = _strtod_l(str.c_str(), &end, CNumericLocale());
#else
    double value = strtod(str.c_str(), &end);
#endif
    if (end == str.c_str()) return defaultValue;
    return value;
}

float IniReader::ReadFloat(const char* section, const char* key, float defaultValue) const {
    return static_cast<float>(ReadDouble(section, key, static_cast<double>(defaultValue)));
}

bool IniReader::ReadBool(const char* section, const char* key, bool defaultValue) const {
    std::string str = ReadString(section, key, "");
    if (str.empty()) return defaultValue;

    // Handle various boolean representations
    if (str == "1" || str == "true" || str == "True" || str == "TRUE" ||
        str == "yes" || str == "Yes" || str == "YES" || str == "on" || str == "On" || str == "ON") {
        return true;
    }
    if (str == "0" || str == "false" || str == "False" || str == "FALSE" ||
        str == "no" || str == "No" || str == "NO" || str == "off" || str == "Off" || str == "OFF") {
        return false;
    }

    return defaultValue;
}

int IniReader::ReadHex(const char* section, const char* key, int defaultValue) const {
    std::string str = ReadString(section, key, "");
    if (str.empty()) return defaultValue;

    // Skip "0x" or "0X" prefix if present
    const char* start = str.c_str();
    if (str.length() >= 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        start += 2;
    }

    char* end;
    long value = strtol(start, &end, 16);
    if (end == start) return defaultValue;
    return static_cast<int>(value);
}

bool IniReader::ReadIntInRange(const char* section, const char* key, int& outValue,
                                int minValue, int maxValue, int defaultValue) const {
    outValue = ReadInt(section, key, defaultValue);
    if (outValue < minValue || outValue > maxValue) {
        return false;
    }
    return true;
}

bool IniReader::ReadDoubleInRange(const char* section, const char* key, double& outValue,
                                   double minValue, double maxValue, double defaultValue) const {
    outValue = ReadDouble(section, key, defaultValue);
    if (outValue < minValue || outValue > maxValue) {
        return false;
    }
    return true;
}

bool IniReader::ReadFloatInRange(const char* section, const char* key, float& outValue,
                                  float minValue, float maxValue, float defaultValue) const {
    outValue = ReadFloat(section, key, defaultValue);
    if (outValue < minValue || outValue > maxValue) {
        return false;
    }
    return true;
}

void IniReader::LogError(const char* message) const {
    if (m_errorCallback) {
        m_errorCallback(message);
    }
}

// ========== IniWriter ==========

IniWriter::~IniWriter() {
    Close();
}

bool IniWriter::Open(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    FILE* file = fopen(path.c_str(), "w");
    if (!file) {
        return false;
    }

    m_file = file;
    return true;
}

void IniWriter::Close() {
    if (m_file) {
        fclose(static_cast<FILE*>(m_file));
        m_file = nullptr;
    }
}

void IniWriter::WriteComment(const char* comment) {
    if (!m_file) return;
    fprintf(static_cast<FILE*>(m_file), "; %s\n", comment);
}

void IniWriter::WriteBlankLine() {
    if (!m_file) return;
    fprintf(static_cast<FILE*>(m_file), "\n");
}

void IniWriter::WriteSection(const char* section) {
    if (!m_file) return;
    fprintf(static_cast<FILE*>(m_file), "[%s]\n", section);
}

void IniWriter::WriteString(const char* key, const char* value) {
    if (!m_file) return;
    fprintf(static_cast<FILE*>(m_file), "%s=%s\n", key, value);
}

void IniWriter::WriteInt(const char* key, int value) {
    if (!m_file) return;
    fprintf(static_cast<FILE*>(m_file), "%s=%d\n", key, value);
}

void IniWriter::WriteDouble(const char* key, double value) {
    if (!m_file) return;
    // Formatted through the SAME "C" locale the reader parses with. printf's %g follows
    // LC_NUMERIC, so pinning only ReadDouble left the round trip broken in the opposite
    // direction: a consumer building /MD inside a game that calls setlocale(LC_ALL, "")
    // on a German install wrote Sensitivity=2,5 and then read it back as 2.0 - a value
    // that survived the round trip before the read side was pinned at all.
#ifdef _MSC_VER
    _fprintf_l(static_cast<FILE*>(m_file), "%s=%.6g\n", CNumericLocale(), key, value);
#else
    fprintf(static_cast<FILE*>(m_file), "%s=%.6g\n", key, value);
#endif
}

void IniWriter::WriteBool(const char* key, bool value) {
    if (!m_file) return;
    fprintf(static_cast<FILE*>(m_file), "%s=%d\n", key, value ? 1 : 0);
}

void IniWriter::WriteHex(const char* key, int value) {
    if (!m_file) return;
    fprintf(static_cast<FILE*>(m_file), "%s=0x%02X\n", key, value);
}

}  // namespace cameraunlock
