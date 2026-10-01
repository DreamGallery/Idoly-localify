#include "../app/src/main/cpp/font_prewarm_coverage.h"
#include <cassert>
int main() {
    int adds=0;
    assert(ensureFontCoverage([] { return FontCoverageCheck{true,false}; }, [&] { ++adds; return true; }));
    assert(adds==0); // Already present: no ambiguous TryAddCharacters call needed.
    bool covered=false;
    assert(ensureFontCoverage([&] { return FontCoverageCheck{covered,false}; }, [&] { ++adds; covered=true; return true; }));
    assert(adds==1);
    assert(!ensureFontCoverage([] { return FontCoverageCheck{false,false}; }, [] { return true; })); // Real missing glyph.
    assert(!ensureFontCoverage([] { return FontCoverageCheck{false,true}; }, [] { assert(false); return true; }));
    assert(!ensureFontCoverage([] { return FontCoverageCheck{false,false}; }, [] { return false; })); // Add invocation exception.
    int checks=0;
    assert(!ensureFontCoverage([&] { return FontCoverageCheck{false,++checks==2}; }, [] { return true; }));
}
