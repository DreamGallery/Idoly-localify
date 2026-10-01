#pragma once
inline bool shouldSubmitTmpRenderer(bool replacementFont, bool pending, bool expectedTexturePresent, bool textureMatches) {
    return expectedTexturePresent && (replacementFont || pending) && (pending || !textureMatches);
}
inline bool pendingTmpRendererAfterSubmit(bool invocationSucceeded, bool textureMatches) {
    return !invocationSucceeded || !textureMatches;
}
