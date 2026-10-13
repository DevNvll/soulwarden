#include "state/creation_data.h"

#include <algorithm>
#include <span>
#include <string_view>
#include <vector>

#include "core/encoding/base64.h"
#include "middleware/compression/zlib.h"

namespace soulwarden::backend::state {
namespace {

/** Limit for the document. The document of the client has 3 to 4 kilobytes. */
constexpr std::size_t kMaxDocumentBytes = 256 * 1024;
constexpr std::string_view kVisualsMember = "\"visualsBlob\"";

// Binary engine object stream (big-endian): tag 0, stream version, then elements.
//   element := flags:u8 [name:u32 if 0x40] [classVersion:u8 if 0x80] typeId:16 bytes
//              [value if 0x10] element* 0x00
//   value   := size bytes. The size is the low 3 bits of the flags, or with flag 0x20 a
//              number that has so many bytes.
// The name is the CRC-32 of the field name in lower case.
constexpr std::uint8_t kStreamTagBinary = 0;
constexpr std::uint32_t kStreamVersion = 3;
/** Each element of the client has this flag. The value 0 ends an element. */
constexpr std::uint8_t kElementMark = 0x08;
constexpr std::uint8_t kElementEnd = 0;
constexpr std::uint8_t kElementHasValue = 0x10;
constexpr std::uint8_t kElementSizeField = 0x20;
constexpr std::uint8_t kElementHasName = 0x40;
constexpr std::uint8_t kElementHasVersion = 0x80;
constexpr std::uint8_t kElementSizeMask = 0x07;
constexpr std::size_t kTypeIdSize = 16;
using TypeId = std::array<std::uint8_t, kTypeIdSize>;
/** Type id of a text value: {03AAAB3F-5C47-5A66-9EBC-D5FA4DB353C9}. */
constexpr TypeId kTextType{0x03, 0xAA, 0xAB, 0x3F, 0x5C, 0x47, 0x5A, 0x66,
                           0x9E, 0xBC, 0xD5, 0xFA, 0x4D, 0xB3, 0x53, 0xC9};
// Type ids of the other elements of PlayerSaveData in the create requests of the client.
/** The root element, class PlayerSaveData: {144C7576-7FBA-4250-B0C8-F06803657D79}, class version 3. */
constexpr TypeId kSaveDataType{0x14, 0x4C, 0x75, 0x76, 0x7F, 0xBA, 0x42, 0x50,
                               0xB0, 0xC8, 0xF0, 0x68, 0x03, 0x65, 0x7D, 0x79};
constexpr std::uint8_t kSaveDataVersion = 3;
/** A signed 32-bit number: {72B9409A-7D1A-4831-9CFE-FCB3FADD3426}. The client writes the value 0 as one byte. */
constexpr TypeId kIntegerType{0x72, 0xB9, 0x40, 0x9A, 0x7D, 0x1A, 0x48, 0x31,
                              0x9C, 0xFE, 0xFC, 0xB3, 0xFA, 0xDD, 0x34, 0x26};
/** The list of the field appearanceData: {7EF0E808-E5B8-5116-BB2B-00A1610BF1D3}. */
constexpr TypeId kAppearanceDataType{0x7E, 0xF0, 0xE8, 0x08, 0xE5, 0xB8, 0x51, 0x16,
                                     0xBB, 0x2B, 0x00, 0xA1, 0x61, 0x0B, 0xF1, 0xD3};
/** The map of the field equipment: {44C93A82-1897-56D6-8CF7-4BC0289915DE}. */
constexpr TypeId kEquipmentType{0x44, 0xC9, 0x3A, 0x82, 0x18, 0x97, 0x56, 0xD6,
                                0x8C, 0xF7, 0x4B, 0xC0, 0x28, 0x99, 0x15, 0xDE};

/** Names of the appearance fields of PlayerSaveData, in the order of the appearance state. */
constexpr std::array<std::uint32_t, kAppearanceFieldCount> kAppearanceFieldNames{
    0xC7470A42, // gender
    0xDA6FBBAF, // race
    0xA6C5AE93, // skinTone
    0xB0A87DBC, // hairstyle
    0x16F8DF7E, // facialHair
    0xE5438D3F, // hairColor
    0xF9CC2837, // facialHairColor
    0x7C94C553, // eyeColor
    0xC6E599A0, // faceMark
    0xD0B2EFE1, // scar
    0x0DEE4C6F, // tattoo
    0xBA24CC17, // tattooColor
};
constexpr std::uint32_t kBackstoryFieldName = 0x7DC4476D; // backstory
constexpr std::uint32_t kPlayerNameFieldName = 0x6E6926DF; // playerName
constexpr std::uint32_t kCompanyNameFieldName = 0x17BBE394; // companyName
constexpr std::uint32_t kEntityClassFieldName = 0xE9F5E620; // entityClassName
constexpr std::uint32_t kGuildCrestFieldName = 0x04DFB561; // guildCrestName
constexpr std::uint32_t kStartingFactionFieldName = 0x1BCB3A6D; // startingFaction
constexpr std::uint32_t kAppearanceDataFieldName = 0x0BE96AA4; // appearanceData
constexpr std::uint32_t kEquipmentFieldName = 0xD338D583; // equipment
// Values that the client writes for a new character (protocol notes 2.2.5).
constexpr std::string_view kCompanyNameSuffix = "'s Company";
constexpr std::string_view kMaleEntityClass = "PlayerMale";
constexpr std::string_view kFemaleEntityClass = "PlayerFemale";
constexpr std::string_view kFemaleGender = "Female";

int hex_value(std::uint8_t character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

/**
 * Reads the bytes of the JSON string that starts at the quote at `start`. The JSON writer
 * of the client (rapidjson) writes a byte below 0x20 as an escape and each other byte as it is.
 */
bool read_json_bytes(std::span<const std::uint8_t> document, std::size_t start, std::vector<std::uint8_t>& output) {
    std::size_t position = start + 1;
    while (position < document.size() && document[position] != '"') {
        const std::uint8_t byte = document[position++];
        if (byte != '\\') {
            output.push_back(byte);
            continue;
        }
        if (position >= document.size()) {
            return false;
        }
        const std::uint8_t escape = document[position++];
        switch (escape) {
        case 'b':
            output.push_back(0x08);
            break;
        case 'f':
            output.push_back(0x0C);
            break;
        case 'n':
            output.push_back(0x0A);
            break;
        case 'r':
            output.push_back(0x0D);
            break;
        case 't':
            output.push_back(0x09);
            break;
        case '"':
        case '\\':
        case '/':
            output.push_back(escape);
            break;
        case 'u': {
            if (document.size() - position < 4) {
                return false;
            }
            int code = 0;
            for (int index = 0; index < 4; ++index) {
                const int digit = hex_value(document[position++]);
                if (digit < 0) {
                    return false;
                }
                code = code * 16 + digit;
            }
            // The client writes this escape only for one byte.
            if (code > 0xFF) {
                return false;
            }
            output.push_back(static_cast<std::uint8_t>(code));
            break;
        }
        default:
            return false;
        }
    }
    return position < document.size();
}

/** Reads the text fields of the root element of the object stream that the backend uses. */
bool read_save_data(std::span<const std::uint8_t> stream, CreationData& data) {
    constexpr std::size_t kHeaderSize = 5;
    if (stream.size() < kHeaderSize || stream[0] != kStreamTagBinary) {
        return false;
    }
    const std::uint32_t version = (static_cast<std::uint32_t>(stream[1]) << 24)
                                  | (static_cast<std::uint32_t>(stream[2]) << 16)
                                  | (static_cast<std::uint32_t>(stream[3]) << 8) | stream[4];
    if (version != kStreamVersion) {
        return false;
    }
    std::size_t position = kHeaderSize;
    // Depth 0 is the root element (PlayerSaveData). Its fields have depth 1.
    std::size_t depth = 0;
    bool hasRoot = false;
    while (position < stream.size()) {
        const std::uint8_t flags = stream[position++];
        if (flags == 0) {
            if (depth == 0) {
                break;
            }
            --depth;
            continue;
        }
        std::uint32_t name = 0;
        if ((flags & kElementHasName) != 0) {
            if (stream.size() - position < 4) {
                return false;
            }
            name = (static_cast<std::uint32_t>(stream[position]) << 24)
                   | (static_cast<std::uint32_t>(stream[position + 1]) << 16)
                   | (static_cast<std::uint32_t>(stream[position + 2]) << 8) | stream[position + 3];
            position += 4;
        }
        if ((flags & kElementHasVersion) != 0) {
            ++position;
        }
        if (stream.size() < position || stream.size() - position < kTypeIdSize) {
            return false;
        }
        const auto typeId = stream.subspan(position, kTypeIdSize);
        position += kTypeIdSize;
        std::span<const std::uint8_t> value;
        if ((flags & kElementHasValue) != 0) {
            std::size_t size = flags & kElementSizeMask;
            if ((flags & kElementSizeField) != 0) {
                const std::size_t sizeBytes = size;
                if (stream.size() - position < sizeBytes) {
                    return false;
                }
                size = 0;
                for (std::size_t index = 0; index < sizeBytes; ++index) {
                    size = (size << 8) | stream[position++];
                }
            }
            if (stream.size() - position < size) {
                return false;
            }
            value = stream.subspan(position, size);
            position += size;
        }
        // The first element is the root element. It has the class of the creation data.
        if (!hasRoot && !std::equal(typeId.begin(), typeId.end(), kSaveDataType.begin())) {
            return false;
        }
        hasRoot = true;
        const bool isText = std::equal(typeId.begin(), typeId.end(), kTextType.begin());
        if (depth == 1 && isText && (flags & kElementHasName) != 0) {
            const std::string text(value.begin(), value.end());
            if (name == kBackstoryFieldName) {
                data.archetype = text;
            }
            for (std::size_t field = 0; field < kAppearanceFieldCount; ++field) {
                if (name == kAppearanceFieldNames[field]) {
                    data.appearance[field] = text;
                }
            }
        }
        ++depth;
    }
    return hasRoot;
}

/**
 * Adds the start of one element of the object stream. The caller adds the elements of its
 * fields and then kElementEnd.
 * @param name Field number, or 0 for the root element (it has no name).
 * @param classVersion Class version, or 0 for an element that has none.
 * @param value Value bytes. No value for an element that has only fields.
 */
void begin_element(std::vector<std::uint8_t>& stream,
                   std::uint32_t name,
                   std::uint8_t classVersion,
                   const TypeId& type,
                   std::optional<std::string_view> value = std::nullopt) {
    /** Largest value size that is in the flags. A larger size is in one more byte. */
    constexpr std::size_t kMaxSizeInFlags = 7;
    constexpr std::size_t kMaxSizeInOneByte = 0xFF;
    std::uint8_t flags = kElementMark;
    if (name != 0) {
        flags |= kElementHasName;
    }
    if (classVersion != 0) {
        flags |= kElementHasVersion;
    }
    // The texts of this file are short: a value has 255 bytes at most.
    const std::size_t size = value ? std::min(value->size(), kMaxSizeInOneByte) : 0;
    if (value) {
        flags |= kElementHasValue;
        flags |= size <= kMaxSizeInFlags ? static_cast<std::uint8_t>(size) : (kElementSizeField | 1);
    }
    stream.push_back(flags);
    if (name != 0) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            stream.push_back(static_cast<std::uint8_t>(name >> shift));
        }
    }
    if (classVersion != 0) {
        stream.push_back(classVersion);
    }
    stream.insert(stream.end(), type.begin(), type.end());
    if (value) {
        if (size > kMaxSizeInFlags) {
            stream.push_back(static_cast<std::uint8_t>(size));
        }
        stream.insert(stream.end(), value->begin(), value->begin() + static_cast<std::ptrdiff_t>(size));
    }
}

/** Adds one text field of the root element. */
void text_field(std::vector<std::uint8_t>& stream, std::uint32_t name, std::string_view text) {
    begin_element(stream, name, 0, kTextType, text);
    stream.push_back(kElementEnd);
}

/**
 * Adds a byte string as a JSON string with the rules of the JSON writer of the client: a
 * byte below 0x20 is an escape, a quote and a backslash get a backslash, and each other byte
 * stays as it is.
 */
void append_json_bytes(std::string& document, std::span<const std::uint8_t> bytes) {
    constexpr char kHexDigits[] = "0123456789ABCDEF";
    constexpr std::uint8_t kFirstPlainByte = 0x20;
    document.push_back('"');
    for (const std::uint8_t byte : bytes) {
        switch (byte) {
        case 0x08:
            document += "\\b";
            break;
        case 0x0C:
            document += "\\f";
            break;
        case 0x0A:
            document += "\\n";
            break;
        case 0x0D:
            document += "\\r";
            break;
        case 0x09:
            document += "\\t";
            break;
        case '"':
            document += "\\\"";
            break;
        case '\\':
            document += "\\\\";
            break;
        default:
            if (byte < kFirstPlainByte) {
                document += "\\u00";
                document.push_back(kHexDigits[byte >> 4]);
                document.push_back(kHexDigits[byte & 0x0F]);
            } else {
                document.push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    document.push_back('"');
}

} // namespace

std::string make_creation_data(const std::string& characterName,
                               const AppearanceNames& appearance,
                               const std::string& worldId,
                               const std::string& backstory) {
    std::vector<std::uint8_t> stream{kStreamTagBinary};
    for (int shift = 24; shift >= 0; shift -= 8) {
        stream.push_back(static_cast<std::uint8_t>(kStreamVersion >> shift));
    }
    // The fields have the order of the create request of the client.
    begin_element(stream, 0, kSaveDataVersion, kSaveDataType);
    text_field(stream, kPlayerNameFieldName, characterName);
    text_field(stream, kCompanyNameFieldName, characterName + std::string(kCompanyNameSuffix));
    text_field(stream,
               kEntityClassFieldName,
               appearance[kAppearanceGender] == kFemaleGender ? kFemaleEntityClass : kMaleEntityClass);
    text_field(stream, kGuildCrestFieldName, "");
    for (std::size_t field = 0; field < kAppearanceFieldCount; ++field) {
        text_field(stream, kAppearanceFieldNames[field], appearance[field]);
    }
    text_field(stream, kBackstoryFieldName, backstory);
    begin_element(stream, kStartingFactionFieldName, 0, kIntegerType, std::string_view("\0", 1));
    stream.push_back(kElementEnd);
    begin_element(stream, kAppearanceDataFieldName, 0, kAppearanceDataType);
    stream.push_back(kElementEnd);
    begin_element(stream, kEquipmentFieldName, 0, kEquipmentType);
    stream.push_back(kElementEnd);
    stream.push_back(kElementEnd); // End of the root element.
    stream.push_back(kElementEnd); // End of the stream.

    std::string document = R"({"currency":0,"visualsBlob":)";
    append_json_bytes(document, stream);
    document += R"(,"position":{"x":0.0,"y":0.0,"z":0.0},"currentLevel":0,"worldId":")";
    // A world id is a UUID text: it needs no escape.
    document += worldId;
    document += "\"}";
    const std::vector<std::uint8_t> compressed = compression::zlib_store(
        std::span(reinterpret_cast<const std::uint8_t*>(document.data()), document.size()));
    return encoding::base64(std::string(compressed.begin(), compressed.end()));
}

std::string with_current_level(const std::string& text, std::uint32_t level) {
    std::string compressed;
    if (text.empty() || !encoding::base64_decode(text, compressed)) {
        return text;
    }
    std::vector<std::uint8_t> document;
    if (!compression::zlib_decode(
            std::span(reinterpret_cast<const std::uint8_t*>(compressed.data()), compressed.size()),
            kMaxDocumentBytes,
            document)) {
        return text;
    }
    // The member is after the bytes of the object stream: the last one in the document is it.
    // A JSON document can have white space before and after the colon of a member.
    static constexpr std::string_view kMember = "\"currentLevel\"";
    const std::string_view view(reinterpret_cast<const char*>(document.data()), document.size());
    const std::size_t member = view.rfind(kMember);
    if (member == std::string_view::npos) {
        return text;
    }
    const auto skip_space = [&view](std::size_t position) {
        while (position < view.size()
               && (view[position] == ' ' || view[position] == '\t' || view[position] == '\r' || view[position] == '\n')) {
            ++position;
        }
        return position;
    };
    std::size_t first = skip_space(member + kMember.size());
    if (first >= view.size() || view[first] != ':') {
        return text;
    }
    first = skip_space(first + 1);
    std::size_t last = first;
    while (last < view.size() && view[last] >= '0' && view[last] <= '9') {
        ++last;
    }
    if (last == first) {
        return text;
    }
    const std::string number = std::to_string(level == 0 ? 0 : level - 1);
    std::vector<std::uint8_t> changed(document.begin(), document.begin() + static_cast<std::ptrdiff_t>(first));
    changed.insert(changed.end(), number.begin(), number.end());
    changed.insert(changed.end(), document.begin() + static_cast<std::ptrdiff_t>(last), document.end());
    const std::vector<std::uint8_t> stored = compression::zlib_store(changed);
    return encoding::base64(std::string(stored.begin(), stored.end()));
}

std::optional<CreationData> read_creation_data(const std::string& text) {
    std::string compressed;
    if (text.empty() || !encoding::base64_decode(text, compressed)) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> document;
    if (!compression::zlib_decode(
            std::span(reinterpret_cast<const std::uint8_t*>(compressed.data()), compressed.size()),
            kMaxDocumentBytes,
            document)) {
        return std::nullopt;
    }
    const std::string_view view(reinterpret_cast<const char*>(document.data()), document.size());
    const std::size_t member = view.find(kVisualsMember);
    const std::size_t colon = member == std::string_view::npos ? member : view.find(':', member + kVisualsMember.size());
    const std::size_t quote = colon == std::string_view::npos ? colon : view.find('"', colon);
    if (quote == std::string_view::npos) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> stream;
    CreationData data;
    if (!read_json_bytes(document, quote, stream) || !read_save_data(stream, data)) {
        return std::nullopt;
    }
    return data;
}

} // namespace soulwarden::backend::state
