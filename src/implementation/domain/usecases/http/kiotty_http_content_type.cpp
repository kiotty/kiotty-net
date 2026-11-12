#include "domain/usecases/http/kiotty_http_content_type.h"

#include "domain/usecases/http/kiotty_http_token.h"

#include <cstddef>
#include <string>

namespace kiotty::net::implementation
{
    namespace
    {
        struct ContentTypeEntry
        {
            KiottyNetHttpContentType type;
            const char*              text;
            TextKind                 kind;
        };

        struct ExtensionEntry
        {
            const char*              extension;
            KiottyNetHttpContentType type;
        };

        constexpr TextKind CHARSET = TextKind::UTF8_WITH_CHARSET;
        constexpr TextKind UTF8 = TextKind::UTF8;
        constexpr TextKind BINARY = TextKind::BINARY;
        constexpr size_t   NOT_FOUND = static_cast<size_t>(-1);

        constexpr ContentTypeEntry CONTENT_TYPES[] = {
            { KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM, "application/octet-stream", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, "text/plain; charset=utf-8", CHARSET },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_HTML, "text/html; charset=utf-8", CHARSET },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_CSS, "text/css; charset=utf-8", CHARSET },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_CSV, "text/csv; charset=utf-8", CHARSET },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_MARKDOWN, "text/markdown; charset=utf-8", CHARSET },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_JAVASCRIPT, "text/javascript; charset=utf-8", CHARSET },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM,
              "text/event-stream; charset=utf-8",
              CHARSET },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, "application/json", UTF8 },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_PROBLEM_JSON, "application/problem+json", UTF8 },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_NDJSON, "application/x-ndjson", UTF8 },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_MANIFEST_JSON, "application/manifest+json", UTF8 },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_XML, "application/xml", UTF8 },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_YAML, "application/yaml", UTF8 },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_FORM_URLENCODED,
              "application/x-www-form-urlencoded",
              UTF8 },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_DICOM, "application/dicom", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_DICOM_JSON, "application/dicom+json", UTF8 },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_DICOM_XML, "application/dicom+xml", UTF8 },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_FHIR_JSON, "application/fhir+json", UTF8 },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_FHIR_XML, "application/fhir+xml", UTF8 },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_PROTOBUF, "application/x-protobuf", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_MSGPACK, "application/msgpack", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_CBOR, "application/cbor", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_PNG, "image/png", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_JPEG, "image/jpeg", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_GIF, "image/gif", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_WEBP, "image/webp", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_AVIF, "image/avif", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_BMP, "image/bmp", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_SVG, "image/svg+xml", UTF8 },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_ICON, "image/x-icon", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_WOFF, "font/woff", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_WOFF2, "font/woff2", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_TTF, "font/ttf", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_OTF, "font/otf", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_MP3, "audio/mpeg", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_WAV, "audio/wav", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_OGG, "audio/ogg", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_MP4, "video/mp4", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_WEBM, "video/webm", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_PDF, "application/pdf", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_ZIP, "application/zip", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_GZIP, "application/gzip", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_TAR, "application/x-tar", BINARY },
            { KIOTTY_NET_HTTP_CONTENT_TYPE_WASM, "application/wasm", BINARY },
        };

        constexpr ExtensionEntry EXTENSIONS[] = {
            { "bin", KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM },
            { "txt", KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN },
            { "html", KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_HTML },
            { "htm", KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_HTML },
            { "css", KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_CSS },
            { "csv", KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_CSV },
            { "md", KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_MARKDOWN },
            { "js", KIOTTY_NET_HTTP_CONTENT_TYPE_JAVASCRIPT },
            { "mjs", KIOTTY_NET_HTTP_CONTENT_TYPE_JAVASCRIPT },
            { "json", KIOTTY_NET_HTTP_CONTENT_TYPE_JSON },
            { "map", KIOTTY_NET_HTTP_CONTENT_TYPE_JSON },
            { "ndjson", KIOTTY_NET_HTTP_CONTENT_TYPE_NDJSON },
            { "webmanifest", KIOTTY_NET_HTTP_CONTENT_TYPE_MANIFEST_JSON },
            { "xml", KIOTTY_NET_HTTP_CONTENT_TYPE_XML },
            { "yaml", KIOTTY_NET_HTTP_CONTENT_TYPE_YAML },
            { "yml", KIOTTY_NET_HTTP_CONTENT_TYPE_YAML },
            { "dcm", KIOTTY_NET_HTTP_CONTENT_TYPE_DICOM },
            { "pb", KIOTTY_NET_HTTP_CONTENT_TYPE_PROTOBUF },
            { "msgpack", KIOTTY_NET_HTTP_CONTENT_TYPE_MSGPACK },
            { "cbor", KIOTTY_NET_HTTP_CONTENT_TYPE_CBOR },
            { "png", KIOTTY_NET_HTTP_CONTENT_TYPE_PNG },
            { "jpg", KIOTTY_NET_HTTP_CONTENT_TYPE_JPEG },
            { "jpeg", KIOTTY_NET_HTTP_CONTENT_TYPE_JPEG },
            { "gif", KIOTTY_NET_HTTP_CONTENT_TYPE_GIF },
            { "webp", KIOTTY_NET_HTTP_CONTENT_TYPE_WEBP },
            { "avif", KIOTTY_NET_HTTP_CONTENT_TYPE_AVIF },
            { "bmp", KIOTTY_NET_HTTP_CONTENT_TYPE_BMP },
            { "svg", KIOTTY_NET_HTTP_CONTENT_TYPE_SVG },
            { "ico", KIOTTY_NET_HTTP_CONTENT_TYPE_ICON },
            { "woff", KIOTTY_NET_HTTP_CONTENT_TYPE_WOFF },
            { "woff2", KIOTTY_NET_HTTP_CONTENT_TYPE_WOFF2 },
            { "ttf", KIOTTY_NET_HTTP_CONTENT_TYPE_TTF },
            { "otf", KIOTTY_NET_HTTP_CONTENT_TYPE_OTF },
            { "mp3", KIOTTY_NET_HTTP_CONTENT_TYPE_MP3 },
            { "wav", KIOTTY_NET_HTTP_CONTENT_TYPE_WAV },
            { "ogg", KIOTTY_NET_HTTP_CONTENT_TYPE_OGG },
            { "mp4", KIOTTY_NET_HTTP_CONTENT_TYPE_MP4 },
            { "webm", KIOTTY_NET_HTTP_CONTENT_TYPE_WEBM },
            { "pdf", KIOTTY_NET_HTTP_CONTENT_TYPE_PDF },
            { "zip", KIOTTY_NET_HTTP_CONTENT_TYPE_ZIP },
            { "gz", KIOTTY_NET_HTTP_CONTENT_TYPE_GZIP },
            { "tar", KIOTTY_NET_HTTP_CONTENT_TYPE_TAR },
            { "wasm", KIOTTY_NET_HTTP_CONTENT_TYPE_WASM },
        };

        constexpr size_t CONTENT_TYPE_COUNT = sizeof(CONTENT_TYPES) / sizeof(CONTENT_TYPES[0]);

        constexpr bool isTableInOrder()
        {
            for (size_t i = 0; i < CONTENT_TYPE_COUNT; ++i)
            {
                if (static_cast<size_t>(CONTENT_TYPES[i].type) != i) { return false; }
            }
            return true;
        }

        static_assert(CONTENT_TYPE_COUNT == KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION,
                      "CONTENT_TYPES: one entry per named content type");
        static_assert(isTableInOrder(), "CONTENT_TYPES: entries follow the enum order");

        ContentTypeEntry        findEntry(KiottyNetHttpContentType type);
        Span<const char>        extensionOf(Span<const char> path);
        bool                    isMediaType(Span<const char> text);
        size_t                  skipParameter(Span<const char> text, size_t position);
        size_t                  skipValue(Span<const char> text, size_t position);
        size_t                  skipQuoted(Span<const char> text, size_t position);
    }

    Span<const char> contentTypeTextOf(KiottyNetHttpContentType type)
    {
        const ContentTypeEntry entry = findEntry(type);
        return Span<const char>{ entry.text, std::char_traits<char>::length(entry.text) };
    }

    TextKind textKindOf(KiottyNetHttpContentType type)
    {
        const ContentTypeEntry entry = findEntry(type);
        return entry.kind;
    }

    KiottyNetHttpContentType contentTypeOfPath(Span<const char> path)
    {
        const Span<const char> extension = extensionOf(path);
        for (const ExtensionEntry& entry : EXTENSIONS)
        {
            if (equalsLiteralIgnoreCase(extension, entry.extension)) { return entry.type; }
        }
        return KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM;
    }

    TextResult parseCustomContentType(const char* text)
    {
        const TextResult parsed = parseText(text, KIOTTY_NET_MAX_CONTENT_TYPE_LENGTH - 1);
        if (!parsed) { return parsed; }
        const bool is_media_type = isMediaType(parsed.value);
        if (!is_media_type) { return TextResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        return parsed;
    }

    namespace
    {
        ContentTypeEntry findEntry(KiottyNetHttpContentType type)
        {
            const size_t index = static_cast<size_t>(type);
            if (index >= CONTENT_TYPE_COUNT) { return ContentTypeEntry{ type, "", BINARY }; }
            return CONTENT_TYPES[index];
        }

        Span<const char> extensionOf(Span<const char> path)
        {
            size_t dot = NOT_FOUND;
            for (size_t i = 0; i < path.length; ++i)
            {
                const bool is_separator = path[i] == '/' || path[i] == '\\';
                dot = is_separator ? NOT_FOUND : dot;
                dot = path[i] == '.' ? i : dot;
            }
            if (dot == NOT_FOUND) { return Span<const char>{ nullptr, 0 }; }
            return path.skip(dot + 1);
        }

        bool isMediaType(Span<const char> text)
        {
            const size_t type_end = skipToken(text, 0);
            const bool has_slash = type_end > 0 && type_end < text.length && text[type_end] == '/';
            if (!has_slash) { return false; }
            const size_t subtype_end = skipToken(text, type_end + 1);
            if (subtype_end == type_end + 1) { return false; }
            size_t position = subtype_end;
            while (position < text.length)
            {
                position = skipParameter(text, position);
            }
            return position == text.length;
        }

        size_t skipParameter(Span<const char> text, size_t position)
        {
            const size_t mark = skipWhitespace(text, position);
            const bool has_semicolon = mark < text.length && text[mark] == ';';
            if (!has_semicolon) { return NOT_FOUND; }
            const size_t name_start = skipWhitespace(text, mark + 1);
            const size_t name_end = skipToken(text, name_start);
            const bool has_name = name_end > name_start && name_end < text.length;
            const bool has_equals = has_name && text[name_end] == '=';
            if (!has_equals) { return NOT_FOUND; }
            return skipValue(text, name_end + 1);
        }

        size_t skipValue(Span<const char> text, size_t position)
        {
            const bool is_quoted = position < text.length && text[position] == '"';
            if (is_quoted) { return skipQuoted(text, position + 1); }
            const size_t end = skipToken(text, position);
            if (end == position) { return NOT_FOUND; }
            return end;
        }

        size_t skipQuoted(Span<const char> text, size_t position)
        {
            size_t cursor = position;
            while (cursor < text.length && text[cursor] != '"')
            {
                const char character = text[cursor];
                const bool is_allowed = isVisibleCharacter(character) || isWhitespace(character);
                if (!is_allowed) { return NOT_FOUND; }
                cursor += text[cursor] == '\\' ? 2 : 1;
            }
            if (cursor >= text.length) { return NOT_FOUND; }
            return cursor + 1;
        }
    }
}
