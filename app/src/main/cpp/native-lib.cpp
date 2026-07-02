#include <jni.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "whisper.h"

struct WavData {
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    double durationSec = 0.0;
    std::vector<float> pcm16kMono;
};

static std::string jstringToString(JNIEnv *env, jstring value) {
    if (value == nullptr) return "";

    const char *chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) return "";

    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

static int64_t fileSize(const std::string &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return -1;

    std::streamoff size = file.tellg();
    if (size < 0) return -1;

    return static_cast<int64_t>(size);
}

static std::string formatBytes(int64_t bytes) {
    if (bytes < 0) return "unknown";

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

static bool readU16(std::istream &in, uint16_t &v) {
    uint8_t b[2];
    if (!in.read(reinterpret_cast<char *>(b), 2)) return false;
    v = static_cast<uint16_t>(b[0] | (b[1] << 8));
    return true;
}

static bool readU32(std::istream &in, uint32_t &v) {
    uint8_t b[4];
    if (!in.read(reinterpret_cast<char *>(b), 4)) return false;
    v = static_cast<uint32_t>(
            b[0] |
            (b[1] << 8) |
            (b[2] << 16) |
            (b[3] << 24)
    );
    return true;
}

static bool readFourCC(std::istream &in, std::string &id) {
    char b[4];
    if (!in.read(b, 4)) return false;
    id.assign(b, 4);
    return true;
}

static std::vector<float> resampleLinear(
        const std::vector<float> &input,
        int srcRate,
        int dstRate
) {
    if (input.empty()) return {};
    if (srcRate == dstRate) return input;

    const double ratio = static_cast<double>(dstRate) / static_cast<double>(srcRate);
    size_t outCount = static_cast<size_t>(std::llround(input.size() * ratio));
    outCount = std::max<size_t>(1, outCount);

    std::vector<float> output(outCount);

    if (input.size() == 1) {
        std::fill(output.begin(), output.end(), input[0]);
        return output;
    }

    if (outCount == 1) {
        output[0] = input[0];
        return output;
    }

    const double scale = static_cast<double>(input.size() - 1) / static_cast<double>(outCount - 1);

    for (size_t i = 0; i < outCount; ++i) {
        double pos = static_cast<double>(i) * scale;
        size_t idx = static_cast<size_t>(pos);

        if (idx >= input.size() - 1) {
            output[i] = input.back();
            continue;
        }

        double frac = pos - static_cast<double>(idx);
        output[i] = static_cast<float>(
                input[idx] * (1.0 - frac) +
                input[idx + 1] * frac
        );
    }

    return output;
}

static bool readWavAs16kMono(
        const std::string &path,
        double maxSeconds,
        WavData &out,
        std::string &err
) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        err = "WAV 파일을 열 수 없습니다.";
        return false;
    }

    std::string riff;
    std::string wave;
    uint32_t riffSize = 0;

    if (!readFourCC(file, riff) || !readU32(file, riffSize) || !readFourCC(file, wave)) {
        err = "WAV 헤더를 읽을 수 없습니다.";
        return false;
    }

    if (riff != "RIFF" || wave != "WAVE") {
        err = "RIFF/WAVE 형식이 아닙니다.";
        return false;
    }

    bool hasFmt = false;
    bool hasData = false;

    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t blockAlign = 0;
    uint16_t bitsPerSample = 0;

    std::streamoff dataOffset = 0;
    uint32_t dataSize = 0;

    while (file && !file.eof()) {
        std::string chunkId;
        uint32_t chunkSize = 0;

        if (!readFourCC(file, chunkId)) break;
        if (!readU32(file, chunkSize)) break;

        std::streamoff payloadStart = file.tellg();

        if (chunkId == "fmt ") {
            if (chunkSize < 16) {
                err = "fmt chunk 크기가 너무 작습니다.";
                return false;
            }

            uint32_t byteRate = 0;

            if (!readU16(file, audioFormat) ||
                !readU16(file, channels) ||
                !readU32(file, sampleRate) ||
                !readU32(file, byteRate) ||
                !readU16(file, blockAlign) ||
                !readU16(file, bitsPerSample)) {
                err = "fmt chunk를 읽을 수 없습니다.";
                return false;
            }

            hasFmt = true;
        } else if (chunkId == "data") {
            dataOffset = payloadStart;
            dataSize = chunkSize;
            hasData = true;
        }

        std::streamoff next =
                payloadStart +
                static_cast<std::streamoff>(chunkSize) +
                static_cast<std::streamoff>(chunkSize & 1U);

        file.seekg(next, std::ios::beg);
    }

    if (!hasFmt || !hasData) {
        err = "fmt 또는 data chunk를 찾지 못했습니다.";
        return false;
    }

    if (audioFormat != 1) {
        err = "현재 단계에서는 PCM WAV만 지원합니다.";
        return false;
    }

    if (channels < 1 || channels > 8) {
        err = "지원하지 않는 채널 수입니다.";
        return false;
    }

    if (sampleRate == 0) {
        err = "sample rate가 잘못되었습니다.";
        return false;
    }

    if (bitsPerSample != 16) {
        err = "현재 단계에서는 PCM16 WAV만 지원합니다.";
        return false;
    }

    if (blockAlign < channels * 2) {
        err = "WAV blockAlign 값이 잘못되었습니다.";
        return false;
    }

    const uint64_t frames = dataSize / blockAlign;
    const double durationSec = static_cast<double>(frames) / static_cast<double>(sampleRate);

    if (durationSec <= 0.0) {
        err = "오디오 길이가 0입니다.";
        return false;
    }

    if (durationSec > maxSeconds) {
        std::ostringstream msg;
        msg << "현재 단계는 짧은 WAV 테스트만 지원합니다.\n";
        msg << "오디오 길이: " << std::fixed << std::setprecision(1) << durationSec << "초\n";
        msg << "허용 길이: " << maxSeconds << "초\n";
        msg << "다음 단계에서 긴 파일 청크 처리를 추가합니다.";
        err = msg.str();
        return false;
    }

    std::vector<uint8_t> bytes(dataSize);

    file.clear();
    file.seekg(dataOffset, std::ios::beg);

    if (!file.read(reinterpret_cast<char *>(bytes.data()), dataSize)) {
        err = "WAV data chunk를 읽을 수 없습니다.";
        return false;
    }

    std::vector<float> mono(frames);

    for (uint64_t i = 0; i < frames; ++i) {
        int sum = 0;

        const uint64_t frameOffset = i * blockAlign;

        for (uint16_t ch = 0; ch < channels; ++ch) {
            const uint64_t p = frameOffset + ch * 2;

            uint8_t lo = bytes[p];
            uint8_t hi = bytes[p + 1];

            int16_t sample = static_cast<int16_t>(
                    static_cast<uint16_t>(lo | (hi << 8))
            );

            sum += static_cast<int>(sample);
        }

        float value = static_cast<float>(sum) /
                      static_cast<float>(channels) /
                      32768.0f;

        value = std::max(-1.0f, std::min(1.0f, value));
        mono[i] = value;
    }

    out.sampleRate = static_cast<int>(sampleRate);
    out.channels = static_cast<int>(channels);
    out.bitsPerSample = static_cast<int>(bitsPerSample);
    out.durationSec = durationSec;
    out.pcm16kMono = resampleLinear(mono, static_cast<int>(sampleRate), 16000);

    if (out.pcm16kMono.empty()) {
        err = "16kHz mono 변환 결과가 비어 있습니다.";
        return false;
    }

    return true;
}

static std::string timestampMs(int64_t ms) {
    int64_t h = ms / 3600000;
    int64_t m = (ms % 3600000) / 60000;
    int64_t s = (ms % 60000) / 1000;
    int64_t r = ms % 1000;

    std::ostringstream out;
    out << std::setfill('0')
        << std::setw(2) << h << ":"
        << std::setw(2) << m << ":"
        << std::setw(2) << s << "."
        << std::setw(3) << r;

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

    auto start = std::chrono::steady_clock::now();

    whisper_context_params cparams = whisper_context_default_params();

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
        out << "Reason: whisper_init_from_file_with_params returned null\n";
        std::string result = out.str();
        return env->NewStringUTF(result.c_str());
    }

    whisper_free(ctx);

    out << "SUCCESS\n";
    out << "Model was loaded and freed successfully.\n\n";
    out << "Load time:\n";
    out << elapsedMs << " ms\n";

    std::string result = out.str();
    return env->NewStringUTF(result.c_str());
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_mobilestt_SafeActivity_nativeTranscribeShortWav(
        JNIEnv *env,
        jobject /* thiz */,
        jstring jModelPath,
        jstring jWavPath,
        jstring jLanguage,
        jint jThreads
) {
    std::string modelPath = jstringToString(env, jModelPath);
    std::string wavPath = jstringToString(env, jWavPath);
    std::string language = jstringToString(env, jLanguage);

    if (language.empty()) {
        language = "ko";
    }

    int threads = static_cast<int>(jThreads);
    if (threads <= 0) threads = 2;
    threads = std::max(1, std::min(threads, 8));

    std::ostringstream out;

    out << "Whisper STT short WAV test\n";
    out << "==========================\n\n";

    if (modelPath.empty()) {
        out << "FAILED\nReason: model path is empty\n";
        std::string result = out.str();
        return env->NewStringUTF(result.c_str());
    }

    if (wavPath.empty()) {
        out << "FAILED\nReason: wav path is empty\n";
        std::string result = out.str();
        return env->NewStringUTF(result.c_str());
    }

    int64_t modelSize = fileSize(modelPath);
    int64_t wavSize = fileSize(wavPath);

    out << "Model:\n" << modelPath << "\n";
    out << "Model size: " << formatBytes(modelSize) << "\n\n";

    out << "WAV:\n" << wavPath << "\n";
    out << "WAV size: " << formatBytes(wavSize) << "\n\n";

    if (modelSize <= 0) {
        out << "FAILED\nReason: model file does not exist or is empty\n";
        std::string result = out.str();
        return env->NewStringUTF(result.c_str());
    }

    if (wavSize <= 0) {
        out << "FAILED\nReason: WAV file does not exist or is empty\n";
        std::string result = out.str();
        return env->NewStringUTF(result.c_str());
    }

    WavData wav;
    std::string err;

    const double maxSeconds = 180.0;

    auto audioStart = std::chrono::steady_clock::now();

    if (!readWavAs16kMono(wavPath, maxSeconds, wav, err)) {
        out << "FAILED\n";
        out << "Reason while reading WAV:\n";
        out << err << "\n";
        std::string result = out.str();
        return env->NewStringUTF(result.c_str());
    }

    auto audioEnd = std::chrono::steady_clock::now();

    long long audioMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            audioEnd - audioStart
    ).count();

    out << "Audio info:\n";
    out << "Original sample rate: " << wav.sampleRate << " Hz\n";
    out << "Original channels: " << wav.channels << "\n";
    out << "Bits per sample: " << wav.bitsPerSample << "\n";
    out << "Duration: " << std::fixed << std::setprecision(2) << wav.durationSec << " sec\n";
    out << "Converted samples: " << wav.pcm16kMono.size() << " at 16kHz mono\n";
    out << "Audio decode/resample time: " << audioMs << " ms\n\n";

    out << "Loading model...\n";

    auto modelStart = std::chrono::steady_clock::now();

    whisper_context_params cparams = whisper_context_default_params();

    whisper_context *ctx = whisper_init_from_file_with_params(
            modelPath.c_str(),
            cparams
    );

    auto modelEnd = std::chrono::steady_clock::now();

    long long modelMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            modelEnd - modelStart
    ).count();

    if (ctx == nullptr) {
        out << "FAILED\n";
        out << "Reason: whisper model load failed\n";
        std::string result = out.str();
        return env->NewStringUTF(result.c_str());
    }

    out << "Model loaded in " << modelMs << " ms\n\n";

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    params.n_threads = threads;
    params.language = language.c_str();
    params.translate = false;

    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.print_special = false;

    params.no_timestamps = false;

    out << "Running STT...\n";
    out << "Language: " << language << "\n";
    out << "Threads: " << threads << "\n\n";

    auto sttStart = std::chrono::steady_clock::now();

    int ret = whisper_full(
            ctx,
            params,
            wav.pcm16kMono.data(),
            static_cast<int>(wav.pcm16kMono.size())
    );

    auto sttEnd = std::chrono::steady_clock::now();

    long long sttMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            sttEnd - sttStart
    ).count();

    if (ret != 0) {
        whisper_free(ctx);

        out << "FAILED\n";
        out << "Reason: whisper_full returned " << ret << "\n";

        std::string result = out.str();
        return env->NewStringUTF(result.c_str());
    }

    int nSegments = whisper_full_n_segments(ctx);

    out << "SUCCESS\n";
    out << "STT completed.\n";
    out << "STT time: " << sttMs << " ms\n";
    out << "Segments: " << nSegments << "\n\n";

    out << "Transcript:\n";
    out << "-----------\n";

    if (nSegments <= 0) {
        out << "(No speech segment detected)\n";
    } else {
        for (int i = 0; i < nSegments; ++i) {
            int64_t t0 = whisper_full_get_segment_t0(ctx, i) * 10;
            int64_t t1 = whisper_full_get_segment_t1(ctx, i) * 10;

            const char *text = whisper_full_get_segment_text(ctx, i);
            std::string line = text ? text : "";

            out << "["
                << timestampMs(t0)
                << " - "
                << timestampMs(t1)
                << "] "
                << line
                << "\n";
        }
    }

    whisper_free(ctx);

    out << "\nSystem info:\n";
    const char *info = whisper_print_system_info();
    if (info != nullptr) {
        out << info;
    } else {
        out << "(no system info)";
    }

    std::string result = out.str();
    return env->NewStringUTF(result.c_str());
}

