// cp1252.h - Mastercam's ANSI strings (Windows-1252: the part file's group name / comment, the machine group label) <->
// QString. The 0x80..0x9F row is Windows-1252's (Unicode's published mapping), the rest is Latin-1; a character with no
// Windows-1252 byte is written as '?' (as WideCharToMultiByte's default character). Ours: the conversion of the Qt front end.
#pragma once
#include <QString>
#include <QByteArray>
namespace Cp1252 {
inline const char16_t *row80() {
    static const char16_t t[32] = { 0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                                    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178 };
    return t;
}
inline QString toQ(const char *s) {
    QString o; if (!s) return o;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) o += QChar(*p >= 0x80 && *p < 0xA0 ? row80()[*p - 0x80] : (char16_t)*p);
    return o;
}
inline QByteArray fromQ(const QString &q) {
    QByteArray o; o.reserve(q.size());
    for (QChar c : q) {
        char16_t u = c.unicode(); char b = '?';
        if (u < 0x80 || (u >= 0xA0 && u < 0x100)) b = (char)u;
        else for (int k = 0; k < 32; k++) if (row80()[k] == u) { b = (char)(0x80 + k); break; }
        o += b;
    }
    return o;
}
}
