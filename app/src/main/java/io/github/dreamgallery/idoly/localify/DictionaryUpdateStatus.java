package io.github.dreamgallery.idoly.localify;

/** Result of native parsing; a restart-required dictionary has not been applied in memory. */
final class DictionaryUpdateStatus {
    static final int NOT_READY = -1;
    static final int INVALID = 0;
    static final int APPLIED = 1;
    static final int RESTART_REQUIRED = 2;

    private DictionaryUpdateStatus() {}

    static boolean accepted(int status) {
        return status == APPLIED || status == RESTART_REQUIRED;
    }

    static String failureMessage(int status) {
        return status == NOT_READY
            ? "原生模块尚未就绪，请进入游戏首页后重试；若仍失败，请检查插件是否正确加载"
            : "字典格式或内容未通过原生校验，旧版本保持不变";
    }
}
