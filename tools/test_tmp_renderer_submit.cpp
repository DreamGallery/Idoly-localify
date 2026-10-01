#include "../app/src/main/cpp/tmp_renderer_submit.h"
#include <cassert>
int main() {
    bool pending = true; // A glyph route changed the CJK font/material pair.
    assert(shouldSubmitTmpRenderer(true,pending,true,false));
    pending=pendingTmpRendererAfterSubmit(true,true);
    assert(!pending);
    assert(!shouldSubmitTmpRenderer(true,pending,true,true)); // No redundant frame submission.
    pending=true; // Restoring Latin removed the style snapshot, but baseline keeps this flag.
    assert(shouldSubmitTmpRenderer(false,pending,true,true));
    pending=pendingTmpRendererAfterSubmit(false,true);
    assert(pending && shouldSubmitTmpRenderer(false,pending,true,true));
    pending=pendingTmpRendererAfterSubmit(true,false);
    assert(pending); // A successful call that did not bind the expected texture still needs repair.
    pending=pendingTmpRendererAfterSubmit(true,true);
    assert(!shouldSubmitTmpRenderer(false,pending,true,false)); // Untouched original font.
    assert(!shouldSubmitTmpRenderer(true,true,false,false)); // Cannot submit an unknown texture.
    assert(shouldSubmitTmpRenderer(true,false,true,false)); // Scene reset on an existing RHR label.
}
