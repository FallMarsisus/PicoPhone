#ifndef APP_H
#define APP_H

#include <lvgl.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace app_text {

inline const char* latin_fallback(uint32_t codepoint) {
    switch (codepoint) {
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
        case 0x0100: case 0x0102: case 0x0104:
        case 0x1EA0: case 0x1EA2: case 0x1EA4: case 0x1EA6: case 0x1EA8: case 0x1EAA:
        case 0x1EAC: case 0x1EAE: case 0x1EB0: case 0x1EB2: case 0x1EB4: case 0x1EB6:
            return "A";
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
        case 0x0101: case 0x0103: case 0x0105:
        case 0x1EA1: case 0x1EA3: case 0x1EA5: case 0x1EA7: case 0x1EA9: case 0x1EAB:
        case 0x1EAD: case 0x1EAF: case 0x1EB1: case 0x1EB3: case 0x1EB5: case 0x1EB7:
            return "a";
        case 0x00C6:
            return "AE";
        case 0x00E6:
            return "ae";
        case 0x00C7: case 0x0106: case 0x0108: case 0x010A: case 0x010C:
            return "C";
        case 0x00E7: case 0x0107: case 0x0109: case 0x010B: case 0x010D:
            return "c";
        case 0x00D0:
            return "D";
        case 0x00F0:
            return "d";
        case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
        case 0x0112: case 0x0114: case 0x0116: case 0x0118: case 0x011A:
            return "E";
        case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
        case 0x0113: case 0x0115: case 0x0117: case 0x0119: case 0x011B:
            return "e";
        case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
        case 0x0128: case 0x012A: case 0x012C: case 0x012E: case 0x0130:
            return "I";
        case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
        case 0x0129: case 0x012B: case 0x012D: case 0x012F: case 0x0131:
            return "i";
        case 0x00D1: case 0x0143: case 0x0145: case 0x0147:
            return "N";
        case 0x00F1: case 0x0144: case 0x0146: case 0x0148:
            return "n";
        case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8:
        case 0x014C: case 0x014E: case 0x0150:
            return "O";
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8:
        case 0x014D: case 0x014F: case 0x0151:
            return "o";
        case 0x0152:
            return "OE";
        case 0x0153:
            return "oe";
        case 0x015A: case 0x015C: case 0x015E: case 0x0160:
            return "S";
        case 0x015B: case 0x015D: case 0x015F: case 0x0161:
            return "s";
        case 0x00DE:
            return "Th";
        case 0x00FE:
            return "th";
        case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
        case 0x016A: case 0x016C: case 0x016E: case 0x0170: case 0x0172:
            return "U";
        case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
        case 0x016B: case 0x016D: case 0x016F: case 0x0171: case 0x0173:
            return "u";
        case 0x00DD: case 0x0178:
            return "Y";
        case 0x00FD: case 0x00FF:
            return "y";
        case 0x0179: case 0x017B: case 0x017D:
            return "Z";
        case 0x017A: case 0x017C: case 0x017E:
            return "z";
        case 0x0132:
            return "IJ";
        case 0x0133:
            return "ij";
        case 0x0141:
            return "L";
        case 0x0142:
            return "l";
        case 0x00DF:
            return "ss";
        default:
            return nullptr;
    }
}

inline bool decode_utf8(const char* text, size_t length, size_t& index, uint32_t& codepoint, size_t& sequence_length) {
    const unsigned char first = static_cast<unsigned char>(text[index]);
    if (first < 0x80) {
        codepoint = first;
        sequence_length = 1;
        return true;
    }

    if ((first & 0xE0) == 0xC0 && index + 1 < length) {
        const unsigned char second = static_cast<unsigned char>(text[index + 1]);
        if ((second & 0xC0) == 0x80) {
            codepoint = ((first & 0x1F) << 6) | (second & 0x3F);
            sequence_length = 2;
            if (codepoint >= 0x80) return true;
        }
    } else if ((first & 0xF0) == 0xE0 && index + 2 < length) {
        const unsigned char second = static_cast<unsigned char>(text[index + 1]);
        const unsigned char third = static_cast<unsigned char>(text[index + 2]);
        if ((second & 0xC0) == 0x80 && (third & 0xC0) == 0x80) {
            codepoint = ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
            sequence_length = 3;
            if (codepoint >= 0x800) return true;
        }
    } else if ((first & 0xF8) == 0xF0 && index + 3 < length) {
        const unsigned char second = static_cast<unsigned char>(text[index + 1]);
        const unsigned char third = static_cast<unsigned char>(text[index + 2]);
        const unsigned char fourth = static_cast<unsigned char>(text[index + 3]);
        if ((second & 0xC0) == 0x80 && (third & 0xC0) == 0x80 && (fourth & 0xC0) == 0x80) {
            codepoint = ((first & 0x07) << 18) | ((second & 0x3F) << 12) | ((third & 0x3F) << 6) | (fourth & 0x3F);
            sequence_length = 4;
            if (codepoint >= 0x10000 && codepoint <= 0x10FFFF) return true;
        }
    }

    codepoint = first;
    sequence_length = 1;
    return false;
}

inline std::string sanitize_text(const char* text) {
    if (text == nullptr) {
        return {};
    }

    std::string output;
    const size_t length = std::char_traits<char>::length(text);
    output.reserve(length);

    for (size_t index = 0; index < length;) {
        uint32_t codepoint = 0;
        size_t sequence_length = 1;
        const size_t start = index;
        const bool valid_utf8 = decode_utf8(text, length, index, codepoint, sequence_length);

        if (!valid_utf8) {
            output.append(text + start, sequence_length);
            index += sequence_length;
            continue;
        }

        const char* fallback = latin_fallback(codepoint);
        if (fallback != nullptr) {
            output.append(fallback);
        } else if (codepoint >= 0x0300 && codepoint <= 0x036F) {
            // Ignore combining marks so accented text degrades cleanly.
        } else {
            output.append(text + start, sequence_length);
        }

        index += sequence_length;
    }

    return output;
}

inline std::string sanitize_format(const char* fmt, va_list args) {
    if (fmt == nullptr) {
        return {};
    }

    va_list args_copy;
    va_copy(args_copy, args);
    const int length = std::vsnprintf(nullptr, 0, fmt, args_copy);
    va_end(args_copy);

    if (length < 0) {
        return sanitize_text(fmt);
    }

    std::vector<char> buffer(static_cast<size_t>(length) + 1U);
    std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
    return sanitize_text(buffer.data());
}

inline void label_set_text(lv_obj_t* obj, const char* text) {
    std::string sanitized = sanitize_text(text);
    ::lv_label_set_text(obj, sanitized.c_str());
}

inline void label_set_text_fmt(lv_obj_t* obj, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string sanitized = sanitize_format(fmt, args);
    va_end(args);
    ::lv_label_set_text(obj, sanitized.c_str());
}

inline void textarea_set_text(lv_obj_t* obj, const char* text) {
    std::string sanitized = sanitize_text(text);
    ::lv_textarea_set_text(obj, sanitized.c_str());
}

inline void textarea_set_placeholder_text(lv_obj_t* obj, const char* text) {
    std::string sanitized = sanitize_text(text);
    ::lv_textarea_set_placeholder_text(obj, sanitized.c_str());
}

inline void span_set_text(lv_span_t* span, const char* text) {
    std::string sanitized = sanitize_text(text);
    ::lv_span_set_text(span, sanitized.c_str());
}

inline void dropdown_set_options(lv_obj_t* obj, const char* options) {
    std::string sanitized = sanitize_text(options);
    ::lv_dropdown_set_options(obj, sanitized.c_str());
}

} // namespace app_text

#define lv_label_set_text(obj, text) app_text::label_set_text((obj), (text))
#define lv_label_set_text_fmt(obj, fmt, ...) app_text::label_set_text_fmt((obj), (fmt), ##__VA_ARGS__)
#define lv_textarea_set_text(obj, text) app_text::textarea_set_text((obj), (text))
#define lv_textarea_set_placeholder_text(obj, text) app_text::textarea_set_placeholder_text((obj), (text))
#define lv_span_set_text(span, text) app_text::span_set_text((span), (text))
#define lv_dropdown_set_options(obj, text) app_text::dropdown_set_options((obj), (text))

/**
 * Classe de base pour toutes les applications
 */
class App {
public:
    virtual ~App() {}

    // Appelé au lancement de l'app : Créez vos boutons/labels ici
    virtual void start(lv_obj_t* parent) = 0;

    // Appelé à chaque tour de boucle (pour les animations ou la logique)
    virtual void update() {}

    // Appelé AVANT lv_obj_clean() pour nettoyer timers/callbacks
    virtual void preClean() {}

    // Appelé quand on quitte l'app (nettoyage)
    virtual void stop() {}

    // Appelé à chaque tour de boucle du coeur 1 (pour les requêtes réseau)
    virtual void update1() {}
};

#endif