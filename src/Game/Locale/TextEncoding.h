#pragma once

#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef DrawText
#undef DrawText
#endif
#ifdef PlaySound
#undef PlaySound
#endif
#elif __has_include(<iconv.h>)
#include <iconv.h>
#include <vector>
#endif

namespace Game::Locale
{

inline bool IsUtf8Text(const std::string& Text)
{
    if (Text.empty())
        return true;

#if defined(_WIN32)
    const int WideLen = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        Text.data(),
        (int)Text.size(),
        nullptr,
        0);
    return WideLen > 0;
#else
    size_t I = 0;
    while (I < Text.size())
    {
        const unsigned char Ch = (unsigned char)Text[I];
        size_t Need = 0;
        if (Ch < 0x80)
            Need = 1;
        else if ((Ch & 0xE0) == 0xC0)
        {
            if (Ch < 0xC2)
                return false; // Overlong 2-byte sequence
            Need = 2;
        }
        else if ((Ch & 0xF0) == 0xE0)
            Need = 3;
        else if ((Ch & 0xF8) == 0xF0)
        {
            if (Ch > 0xF4)
                return false;
            Need = 4;
        }
        else
            return false;

        if (I + Need > Text.size())
            return false;
        for (size_t J = 1; J < Need; ++J)
            if (((unsigned char)Text[I + J] & 0xC0) != 0x80)
                return false;
        I += Need;
    }
    return true;
#endif
}

inline std::string WideToUtf8(const wchar_t* Text, int Length)
{
#if defined(_WIN32)
    if (!Text || Length <= 0)
        return {};

    const int Utf8Len = WideCharToMultiByte(
        CP_UTF8,
        0,
        Text,
        Length,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (Utf8Len <= 0)
        return {};

    std::string Out((size_t)Utf8Len, '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        Text,
        Length,
        Out.data(),
        Utf8Len,
        nullptr,
        nullptr);
    return Out;
#else
    (void)Text;
    (void)Length;
    return {};
#endif
}

inline std::string LegacyGameTextToUtf8(const std::string& Text)
{
    if (Text.empty() || IsUtf8Text(Text))
        return Text;

#if defined(_WIN32)
    const int WideLen = MultiByteToWideChar(
        CP_ACP,
        0,
        Text.data(),
        (int)Text.size(),
        nullptr,
        0);
    if (WideLen <= 0)
        return Text;

    std::wstring Wide((size_t)WideLen, L'\0');
    MultiByteToWideChar(
        CP_ACP,
        0,
        Text.data(),
        (int)Text.size(),
        Wide.data(),
        WideLen);

    std::string Utf8 = WideToUtf8(Wide.data(), WideLen);
    return Utf8.empty() ? Text : Utf8;
#elif __has_include(<iconv.h>)
    const char* Encodings[] = {"GB18030", "CP949", "WINDOWS-1252"};
    for (const char* Encoding : Encodings)
    {
        iconv_t Cd = iconv_open("UTF-8", Encoding);
        if (Cd == (iconv_t)-1)
            continue;

        std::vector<char> OutBuf(Text.size() * 4 + 4, '\0');
        char* InPtr = const_cast<char*>(Text.data());
        size_t InBytesLeft = Text.size();
        char* OutPtr = OutBuf.data();
        size_t OutBytesLeft = OutBuf.size();

        const size_t Res = iconv(Cd, &InPtr, &InBytesLeft, &OutPtr, &OutBytesLeft);
        iconv_close(Cd);
        if (Res != (size_t)-1 && InBytesLeft == 0)
        {
            return std::string(OutBuf.data(), OutBuf.size() - OutBytesLeft);
        }
    }
    return Text;
#else
    return Text;
#endif
}

} // namespace Game::Locale
