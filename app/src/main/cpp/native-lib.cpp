#include <jni.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include "whisper.h"

static std::string jstringToString(JNIEnv *env, jstring value) {
    if (value == nullptr) {
        return "";
    }

    const char *chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        return "";
    }

    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

static int64_t fileSize(const std::string &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file) {
        return -1;
    }

    std::streamoff size = file.tellg();

    if (size < 0) {
        return -1;
    }

    return static_cast<int64_t>(size);
}

static std::string formatBytes(int64_t bytes) {
    if (bytes < 0) {
        return "unknown";
    }

    std::ostringstream out;

    if (bytes < 1024) {
        out << bytes << " B";
        return out.str();
    }

    double kb = bytes / 1024.0;
    if (kb < 1024.0) {
        out << std::fixed << std::setprecision(1) << kb << " KB";
        return out.str();
    }

    double mb = kb / 1024.0;
    if (mb < 1024.0) {
        out << std::fixed << std::setprecision(1) << mb << " MB";
        return out.str();
    }

    double gb = mb / 1024.0;
    out << std::fixed << std::setprecision(1) << gb << " GB";
    return out.str();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_mobilestt_SafeActivity_nativeCheck(
        JNIEnv *env,
        jobject /* thiz */
) {
    std::ostringstream out;

    out << "Native C++ bridge OK\n";
    out << "whisper.cpp linked OK\n\n";
    out << "whisper.cpp system info:\n";

    const char *info = whisper_print_system_info();

    if (info != nullptr) {
        out << info;
    } else {
        out << "(no system info)";
    }

    std::string result = out.str();
    return env->NewStringUTF(result.c_str());
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_mobilestt_SafeActivity_nativeLoadModel(
        JNIEnv *env,
        jobject /* thiz */,
        jstring jModelPath
) {
    std::string modelPath = jstringToString(env, jModelPath);

    std::ostringstream out;

    out << "Whisper model load test\n";
    out << "=======================\n\n";

    if (modelPath.empty()) {
        out << "FAILED\n";
        out << "Reason: model path is empty\n";
        std::string result = out.str();
        return env->NewStringUTF(result.c_str());
    }

    int64_t size = fileSize(modelPath);

    out << "Model path:\n";
    out << modelPath << "\n\n";

    out << "File size:\n";
    out << formatBytes(size) << "\n\n";

    if (size <= 0) {
        out << "FAILED\n";
        out << "Reason: model file does not exist or is empty\n";
        std::string result = out.str();
        return env->NewStringUTF(result.c_str());
    }

    if (size < 1024 * 1024) {
        out << "FAILED\n";
        out << "Reason: file is too small to be a Whisper model\n";
        std::string result = out.str();
        return env->NewStringUTF(result.c_str());
    }

    auto start = std::chrono::steady_clock::now();

    whisper_context_params cparams = whisper_context_default_params();

    // 이번 단계는 CPU 로드 확인만 합니다.
    // GPU/Vulkan은 나중에 따로 켭니다.
    whisper_context *ctx = whisper_init_from_file_with_params(
            modelPath.c_str(),
            cparams
    );

    auto end = std::chrono::steady_clock::now();

    long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start
    ).count();

    if (ctx == nullptr) {
        out << "FAILED\n";
        out << "Reason: whisper_init_from_file_with_params returned null\n\n";
        out << "Possible causes:\n";
        out << "- The selected file is not a whisper.cpp ggml/gguf model\n";
        out << "- The model file is corrupted\n";
        out << "- The model is too large for this phone memory\n";
        out << "- The whisper.cpp fork expects a different model format\n";

        std::string result = out.str();
        return env->NewStringUTF(result.c_str());
    }

    whisper_free(ctx);

    out << "SUCCESS\n";
    out << "Model was loaded and freed successfully.\n\n";
    out << "Load time:\n";
    out << elapsedMs << " ms\n\n";

    out << "System info:\n";
    const char *info = whisper_print_system_info();

    if (info != nullptr) {
        out << info;
    } else {
        out << "(no system info)";
    }

    std::string result = out.str();
    return env->NewStringUTF(result.c_str());
}
