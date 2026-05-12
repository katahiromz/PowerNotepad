/*
 * PROJECT:    Power Notepad
 * LICENSE:    LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:    Thin PCRE2 16-bit wrapper for regex search and replace
 * COPYRIGHT:  Copyright 2024 Katayama Hirofumi MZ
 */

#pragma once

#include <string>

/**
 * Thin wrapper around the PCRE2 16-bit API for regex search and replace.
 *
 * Compile flags used:
 *   PCRE2_MULTILINE      – ^ and $ match per-line boundaries (not just
 *                          start/end of the whole document).
 *   PCRE2_NEWLINE_ANYCRLF – treats \r\n, \r, and \n all as line endings,
 *                          which is appropriate for Windows text editing.
 *   PCRE2_UTF             – enables UTF-16 decoding so \w, \d, etc. match
 *                          full Unicode characters, not just ASCII.
 *   PCRE2_CASELESS        – added when case-insensitive matching is requested.
 *
 * Replacement syntax (PCRE2_SUBSTITUTE_EXTENDED):
 *   $0 or ${0}   – whole match (note: std::regex used $& for this)
 *   $1, ${1}     – first capture group
 *   ${name}      – named capture group
 *   \$           – literal dollar sign (std::regex used $$)
 */
class RegexEngine
{
public:
    RegexEngine();
    ~RegexEngine();

    /**
     * Compile the pattern.  Returns true on success.
     * On failure, if outError is non-null it receives a human-readable message.
     */
    bool Compile(const wchar_t *pattern, bool caseless,
                 std::wstring *outError = nullptr);

    bool IsValid() const { return m_code != nullptr; }

    /**
     * Forward search: find the first match at or after startOffset.
     * Zero-length matches are skipped (consistent with the previous
     * std::regex_search behaviour).
     */
    bool SearchForward(const wchar_t *text, size_t textLen,
                       size_t startOffset,
                       size_t *pStart, size_t *pEnd) const;

    /**
     * Backward search: find the last match whose start position is strictly
     * before startOffset.
     */
    bool SearchBackward(const wchar_t *text, size_t textLen,
                        size_t startOffset,
                        size_t *pStart, size_t *pEnd) const;

    /**
     * Returns true if text[start..end) is a complete, anchored match
     * (equivalent to std::regex_match on that slice).
     */
    bool IsFullMatch(const wchar_t *text, size_t start, size_t end) const;

    /**
     * Replaces the match covering text[start..end) using the expansion of
     * `replacement'.  On success sets `result' to the substituted string and
     * returns true.
     */
    bool ReplaceMatch(const wchar_t *text, size_t textLen,
                      size_t start, size_t end,
                      const wchar_t *replacement,
                      std::wstring &result) const;

    static std::wstring EscapeForRegex(const std::wstring& input);

private:
    void *m_code;       /* pcre2_code_16*       */
    void *m_matchData;  /* pcre2_match_data_16* */

    /* Non-copyable */
    RegexEngine(const RegexEngine&) = delete;
    RegexEngine& operator=(const RegexEngine&) = delete;
};
