package io.github.dreamgallery.idoly.localify;

public final class NativeResourceValidationTest {
    public static void main(String[] args) {
        // Reproduce a visible Java overlay with a failed native library load.
        // The parser must not be invoked or the valid payload called corrupt.
        try {
            NativeResourceValidation.masterBlobs(false, "加载原生库失败", () -> {
                throw new AssertionError("Uninitialized JNI must not be called");
            });
            throw new AssertionError("Unavailable validator was accepted");
        } catch (IllegalStateException expected) {
            if (!expected.getMessage().contains("校验未执行") ||
                !expected.getMessage().contains("加载原生库失败")) throw new AssertionError(expected);
        }
        try {
            NativeResourceValidation.masterBlobs(true, "", () -> false);
            throw new AssertionError("Invalid file was accepted");
        } catch (IllegalArgumentException expected) {
            if (!expected.getMessage().contains("未通过原生校验")) throw new AssertionError(expected);
        }
        try {
            NativeResourceValidation.masterBlobs(true, "", () -> { throw new UnsatisfiedLinkError("JNI unavailable"); });
            throw new AssertionError("Missing JNI was accepted");
        } catch (IllegalStateException expected) {
            if (!(expected.getCause() instanceof UnsatisfiedLinkError)) throw new AssertionError(expected);
        }
        NativeResourceValidation.masterBlobs(true, "", () -> true);
        System.out.println("Native availability, missing JNI and invalid payload remain distinct");
    }
}
