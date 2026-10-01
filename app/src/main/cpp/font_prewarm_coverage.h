#pragma once
struct FontCoverageCheck { bool covered; bool error; };
// TryAddCharacters may return false when every requested glyph already exists.
// Only the font's own lookup coverage determines whether a static clone is safe.
template<class Check, class Add>
bool ensureFontCoverage(Check check, Add add) {
    const auto before = check();
    if (before.error) return false;
    if (before.covered) return true;
    if (!add()) return false; // Invocation failed, independent of TMP's bool result.
    const auto after = check();
    return !after.error && after.covered;
}
