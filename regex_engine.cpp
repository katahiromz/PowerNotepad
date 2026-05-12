/*
 * PROJECT:    Power Notepad
 * LICENSE:    LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:    PCRE2 16-bit regex engine implementation
 * COPYRIGHT:  Copyright 2024 Katayama Hirofumi MZ
 */

/*
 * We use the PCRE2 16-bit API so that wchar_t strings (which are 16-bit on
 * Windows) can be passed directly without any conversion.
 */
#define PCRE2_CODE_UNIT_WIDTH 16
#include <pcre2.h>

#include "regex_engine.h"

#include <cstring>
#include <cstdlib>

/* Convenience aliases */
typedef pcre2_code_16       PCRE2Code;
typedef pcre2_match_data_16 PCRE2MatchData;

/* -----------------------------------------------------------------------
 * Construction / destruction
 * --------------------------------------------------------------------- */

RegexEngine::RegexEngine()
    : m_code(nullptr), m_matchData(nullptr)
{
}

RegexEngine::~RegexEngine()
{
    if (m_matchData)
        pcre2_match_data_free_16(static_cast<PCRE2MatchData *>(m_matchData));
    if (m_code)
        pcre2_code_free_16(static_cast<PCRE2Code *>(m_code));
}

/* -----------------------------------------------------------------------
 * Compile
 * --------------------------------------------------------------------- */

bool RegexEngine::Compile(const wchar_t *pattern, bool caseless,
                           std::wstring *outError)
{
    /* Release any previous compiled pattern */
    if (m_matchData)
    {
        pcre2_match_data_free_16(static_cast<PCRE2MatchData *>(m_matchData));
        m_matchData = nullptr;
    }
    if (m_code)
    {
        pcre2_code_free_16(static_cast<PCRE2Code *>(m_code));
        m_code = nullptr;
    }

    /*
     * Flags:
     *   PCRE2_MULTILINE      – ^ / $ work per-line
     *   PCRE2_NEWLINE_ANYCRLF– treat \r\n, \r, and \n as line endings
     *   PCRE2_UTF            – UTF-16 mode (full Unicode \w, \d, …)
     *   PCRE2_CASELESS       – case-insensitive (when requested)
     */
    uint32_t options =
        PCRE2_MULTILINE | PCRE2_NEWLINE_ANYCRLF | PCRE2_UTF;
    if (caseless)
        options |= PCRE2_CASELESS;

    int       errcode   = 0;
    PCRE2_SIZE erroffset = 0;

    PCRE2Code *code = pcre2_compile_16(
        reinterpret_cast<PCRE2_SPTR16>(pattern),
        PCRE2_ZERO_TERMINATED,
        options,
        &errcode, &erroffset,
        nullptr /* use default compile context */);

    if (!code)
    {
        if (outError)
        {
            PCRE2_UCHAR16 errbuf[256];
            pcre2_get_error_message_16(
                errcode, errbuf,
                static_cast<PCRE2_SIZE>(sizeof(errbuf) / sizeof(errbuf[0])));
            *outError = reinterpret_cast<const wchar_t *>(errbuf);
        }
        return false;
    }

    m_code      = code;
    m_matchData = pcre2_match_data_create_from_pattern_16(code, nullptr);
    return true;
}

/* -----------------------------------------------------------------------
 * SearchForward
 * --------------------------------------------------------------------- */

bool RegexEngine::SearchForward(const wchar_t *text, size_t textLen,
                                 size_t startOffset,
                                 size_t *pStart, size_t *pEnd) const
{
    if (!m_code || !m_matchData)
        return false;

    auto *code = static_cast<PCRE2Code *>(m_code);
    auto *md   = static_cast<PCRE2MatchData *>(m_matchData);

    PCRE2_SIZE offset = static_cast<PCRE2_SIZE>(startOffset);

    while (offset <= static_cast<PCRE2_SIZE>(textLen))
    {
        int rc = pcre2_match_16(
            code,
            reinterpret_cast<PCRE2_SPTR16>(text),
            static_cast<PCRE2_SIZE>(textLen),
            offset,
            0 /* no extra flags */,
            md,
            nullptr);

        if (rc < 0)
            return false; /* PCRE2_ERROR_NOMATCH or other error */

        PCRE2_SIZE *ov         = pcre2_get_ovector_pointer_16(md);
        PCRE2_SIZE  matchStart = ov[0];
        PCRE2_SIZE  matchEnd   = ov[1];

        if (matchStart == matchEnd)
        {
            /* Zero-length match: skip one code unit and retry */
            if (offset < static_cast<PCRE2_SIZE>(textLen))
            {
                offset++;
                continue;
            }
            return false;
        }

        *pStart = static_cast<size_t>(matchStart);
        *pEnd   = static_cast<size_t>(matchEnd);
        return true;
    }

    return false;
}

/* -----------------------------------------------------------------------
 * SearchBackward
 * --------------------------------------------------------------------- */

bool RegexEngine::SearchBackward(const wchar_t *text, size_t textLen,
                                  size_t startOffset,
                                  size_t *pStart, size_t *pEnd) const
{
    if (!m_code || !m_matchData)
        return false;

    auto *code = static_cast<PCRE2Code *>(m_code);
    auto *md   = static_cast<PCRE2MatchData *>(m_matchData);

    PCRE2_SIZE offset = 0;
    bool       bFound = false;
    size_t     lastStart = 0, lastEnd = 0;

    while (offset <= static_cast<PCRE2_SIZE>(textLen))
    {
        int rc = pcre2_match_16(
            code,
            reinterpret_cast<PCRE2_SPTR16>(text),
            static_cast<PCRE2_SIZE>(textLen),
            offset,
            0,
            md,
            nullptr);

        if (rc < 0)
            break;

        PCRE2_SIZE *ov         = pcre2_get_ovector_pointer_16(md);
        PCRE2_SIZE  matchStart = ov[0];
        PCRE2_SIZE  matchEnd   = ov[1];

        /* Stop once we reach or pass the current selection start */
        if (matchStart >= static_cast<PCRE2_SIZE>(startOffset))
            break;

        if (matchStart == matchEnd)
        {
            /* Zero-length match: advance by one */
            if (offset < static_cast<PCRE2_SIZE>(textLen))
            {
                offset++;
                continue;
            }
            break;
        }

        bFound    = true;
        lastStart = static_cast<size_t>(matchStart);
        lastEnd   = static_cast<size_t>(matchEnd);

        /* Advance past the current match start to find later candidates */
        offset = matchStart + 1;
    }

    if (!bFound)
        return false;

    *pStart = lastStart;
    *pEnd   = lastEnd;
    return true;
}

/* -----------------------------------------------------------------------
 * IsFullMatch
 * --------------------------------------------------------------------- */

bool RegexEngine::IsFullMatch(const wchar_t *text, size_t start,
                               size_t end) const
{
    if (!m_code || !m_matchData)
        return false;

    auto *code = static_cast<PCRE2Code *>(m_code);
    auto *md   = static_cast<PCRE2MatchData *>(m_matchData);

    /*
     * Pass text[0..end) as the subject so that ^ and $ anchors are evaluated
     * relative to the full lines visible in the document.
     * PCRE2_ANCHORED forces the match to start exactly at `start'.
     * We then verify the match end equals `end'.
     */
    int rc = pcre2_match_16(
        code,
        reinterpret_cast<PCRE2_SPTR16>(text),
        static_cast<PCRE2_SIZE>(end),
        static_cast<PCRE2_SIZE>(start),
        PCRE2_ANCHORED,
        md,
        nullptr);

    if (rc < 0)
        return false;

    PCRE2_SIZE *ov = pcre2_get_ovector_pointer_16(md);
    return ov[1] == static_cast<PCRE2_SIZE>(end);
}

/* -----------------------------------------------------------------------
 * ReplaceMatch
 * --------------------------------------------------------------------- */

bool RegexEngine::ReplaceMatch(const wchar_t *text, size_t textLen,
                                size_t start, size_t end,
                                const wchar_t *replacement,
                                std::wstring &result) const
{
    if (!m_code)
        return false;

    /*
     * Operate on the selected slice so that the output contains only the
     * substituted text.  PCRE2_ANCHORED forces the match to start at offset 0
     * of the slice.  PCRE2_SUBSTITUTE_EXTENDED enables \U, \L, \u, \l, \E
     * case-modifier sequences in the replacement string.
     */
    const wchar_t *subject    = text + start;
    PCRE2_SIZE     subjectLen = static_cast<PCRE2_SIZE>(end - start);
    size_t         replLen    = wcslen(replacement);

    /* Generous initial estimate for the output buffer */
    PCRE2_SIZE outputLen =
        static_cast<PCRE2_SIZE>(subjectLen + replLen * 2 + 64);

    result.resize(outputLen);

    uint32_t subOpts =
        PCRE2_ANCHORED |
        PCRE2_SUBSTITUTE_EXTENDED |
        PCRE2_SUBSTITUTE_OVERFLOW_LENGTH |
        PCRE2_SUBSTITUTE_UNSET_EMPTY;

    int rc = pcre2_substitute_16(
        static_cast<PCRE2Code *>(m_code),
        reinterpret_cast<PCRE2_SPTR16>(subject),
        subjectLen,
        /*startoffset=*/ 0,
        subOpts,
        /*match_data=*/  nullptr, /* PCRE2 creates a temporary one */
        /*match_context=*/ nullptr,
        reinterpret_cast<PCRE2_SPTR16>(replacement),
        PCRE2_ZERO_TERMINATED,
        reinterpret_cast<PCRE2_UCHAR16 *>(&result[0]),
        &outputLen);

    if (rc == PCRE2_ERROR_NOMEMORY)
    {
        /*
         * PCRE2_SUBSTITUTE_OVERFLOW_LENGTH caused outputLen to be updated
         * to the required size.  Resize and retry without the overflow flag.
         */
        result.resize(outputLen);
        rc = pcre2_substitute_16(
            static_cast<PCRE2Code *>(m_code),
            reinterpret_cast<PCRE2_SPTR16>(subject),
            subjectLen,
            0,
            PCRE2_ANCHORED | PCRE2_SUBSTITUTE_EXTENDED | PCRE2_SUBSTITUTE_UNSET_EMPTY,
            nullptr, nullptr,
            reinterpret_cast<PCRE2_SPTR16>(replacement),
            PCRE2_ZERO_TERMINATED,
            reinterpret_cast<PCRE2_UCHAR16 *>(&result[0]),
            &outputLen);
    }

    if (rc < 0)
        return false;

    result.resize(outputLen);
    return true;
}

std::wstring RegexEngine::EscapeForRegex(const std::wstring& input)
{
    // meta characters
    static const wchar_t kMeta[] = L"\\.^$*+?()[]{}|";

    std::wstring result;
    result.reserve(input.size() * 2);

    for (wchar_t ch : input)
    {
        if (std::wcschr(kMeta, ch))
            result += L'\\';
        result += ch;
    }

    return result;
}
