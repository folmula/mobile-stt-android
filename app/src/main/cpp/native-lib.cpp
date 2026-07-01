#include <jni.h>
#include <sstream>
#include <string>

#include "whisper.h"

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

