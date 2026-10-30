package io.github.dreamgallery.idoly.localify;

import java.util.function.BooleanSupplier;

/** Availability errors must not be reported as downloaded-file corruption. */
final class NativeResourceValidation {
    private NativeResourceValidation() {}

    static void masterBlobs(boolean ready, String initializationFailure, BooleanSupplier validate) {
        if (!ready) {
            String detail = initializationFailure == null || initializationFailure.isEmpty()
                ? "请进入游戏首页后重试；若仍失败，请检查插件启动日志"
                : initializationFailure;
            throw new IllegalStateException("MasterDB 校验未执行：插件原生模块尚未就绪。" + detail);
        }
        final boolean valid;
        try {
            valid = validate.getAsBoolean();
        } catch (LinkageError error) {
            throw new IllegalStateException("MasterDB 校验未执行：无法调用插件原生接口，请检查插件启动日志", error);
        }
        if (!valid)
            throw new IllegalArgumentException("MasterDB 更新文件未通过原生校验，旧版本保持不变");
    }
}
