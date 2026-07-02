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

struct WavInfo {
    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t blockAlign = 0;
    uint16_t bitsPerSample = 0;
    uint64_t dataOffset = 0;
    uint64_t dataSize = 0;

    uint64_t totalFrames() const {
        return blockAlign == 0 ? 0 : dataSize / blockAlign;
    }

    double durationSec() const {
        return sampleRate == 0 ? 0.0 : static_cast<double>(totalFrames()) / sampleRate;
    }
};

struct CleanMap {
    int64_t cleanStartMs = 0;
    int64_t cleanEndMs = 0;
    int64_t originalStartMs = 0;
    int64_t originalEndMs = 0;
};

struct CleanBlock {
    std::vector<float> pcm;
    std::vector<CleanMap> maps;
    int totalFrames20ms = 0;
    int keptFrames20ms = 0;
};

static std::string jstringToString(JNIEnv *env, jstring value) {
    if (value == nullptr) return "";

    const char *chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) return "";

    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

static void callNativeStatus(JNIEnv *env, jobject thiz, const std::string &message) {
    jclass cls = env->GetObjectClass(thiz);
    if (cls == nullptr) {
        env->ExceptionClear();
        return;
    }

    jmethodID mid = env->GetMethodID(cls, "onNativeStatus", "(Ljava/lang/String;)V");

    if (mid == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(cls);
        return;
    }

    jstring jMessage = env->NewStringUTF(message.c_str());
    env->CallVoidMethod(thiz, mid, jMessage);
    env->DeleteLocalRef(jMessage);
    env->DeleteLocalRef(cls);
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

static bool parseWavInfo(
        const std::string &path,
        WavInfo &info,
        std::string &err
) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        err = "WAV file open failed";
        return false;
    }

    std::string riff;
    std::string wave;
    uint32_t riffSize = 0;

    if (!readFourCC(file, riff) || !readU32(file, riffSize) || !readFourCC(file, wave)) {
        err = "WAV header read failed";
        return false;
    }

    if (riff != "RIFF" || wave != "WAVE") {
        err = "Not RIFF/WAVE format";
        return false;
    }

    bool hasFmt = false;
    bool hasData = false;

    while (file && !file.eof()) {
        std::string chunkId;
        uint32_t chunkSize = 0;

        if (!readFourCC(file, chunkId)) break;
        if (!readU32(file, chunkSize)) break;

        std::streamoff payloadStart = file.tellg();

        if (chunkId == "fmt ") {
            uint32_t byteRate = 0;

            if (!readU16(file, info.audioFormat) ||
                !readU16(file, info.channels) ||
                !readU32(file, info.sampleRate) ||
                !readU32(file, byteRate) ||
                !readU16(file, info.blockAlign) ||
                !readU16(file, info.bitsPerSample)) {
                err = "fmt chunk read failed";
                return false;
            }

            hasFmt = true;
        } else if (chunkId == "data") {
            info.dataOffset = static_cast<uint64_t>(payloadStart);
            info.dataSize = chunkSize;
            hasData = true;
        }

        std::streamoff next =
                payloadStart +
                static_cast<std::streamoff>(chunkSize) +
                static_cast<std::streamoff>(chunkSize & 1U);

        file.seekg(next, std::ios::beg);
    }

    if (!hasFmt || !hasData) {
        err = "fmt or data chunk not found";
        return false;
    }

    if (info.audioFormat != 1) {
        err = "Only PCM WAV is supported in this step";
        return false;
    }

    if (info.bitsPerSample != 16) {
        err = "Only PCM16 WAV is supported in this step";
        return false;
    }

    if (info.channels < 1 || info.channels > 8) {
        err = "Unsupported channel count";
        return false;
    }

    if (info.sampleRate == 0 || info.blockAlign == 0) {
        err = "Invalid WAV format";
        return false;
    }

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

static bool readWavSegmentAs16kMono(
        const std::string &path,
        const WavInfo &info,
        uint64_t startFrame,
        uint64_t frameCount,
        std::vector<float> &out,
        std::string &err
) {
    out.clear();

    const uint64_t totalFrames = info.totalFrames();

    if (startFrame >= totalFrames) {
        return true;
    }

    const uint64_t framesToRead = std::min(frameCount, totalFrames - startFrame);
    const uint64_t byteCount64 = framesToRead * info.blockAlign;

    if (byteCount64 == 0) {
        return true;
    }

    if (byteCount64 > static_cast<uint64_t>(256 * 1024 * 1024)) {
        err = "Internal segment read size is too large";
        return false;
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(byteCount64));

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        err = "WAV file open failed";
        return false;
    }

    uint64_t byteOffset = info.dataOffset + startFrame * info.blockAlign;
    file.seekg(static_cast<std::streamoff>(byteOffset), std::ios::beg);

    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    size_t got = static_cast<size_t>(file.gcount());
    size_t actualFrames = got / info.blockAlign;

    if (actualFrames == 0) {
        return true;
    }

    std::vector<float> mono(actualFrames);

    for (size_t i = 0; i < actualFrames; ++i) {
        int sum = 0;
        size_t frameOffset = i * info.blockAlign;

        for (uint16_t ch = 0; ch < info.channels; ++ch) {
            size_t p = frameOffset + ch * 2;

            uint8_t lo = bytes[p];
            uint8_t hi = bytes[p + 1];

            int16_t sample = static_cast<int16_t>(
                    static_cast<uint16_t>(lo | (hi << 8))
            );

            sum += static_cast<int>(sample);
        }

        float value = static_cast<float>(sum) /
                      static_cast<float>(info.channels) /
                      32768.0f;

        if (value > 1.0f) value = 1.0f;
        if (value < -1.0f) value = -1.0f;

        mono[i] = value;
    }

    out = resampleLinear(mono, static_cast<int>(info.sampleRate), 16000);

    return true;
}

static double rmsDbRange(
        const std::vector<float> &pcm,
        size_t start,
        size_t end
) {
    if (start >= end || start >= pcm.size()) {
        return -120.0;
    }

    end = std::min(end, pcm.size());

    double sum = 0.0;
    size_t n = 0;

    for (size_t i = start; i < end; ++i) {
        double v = pcm[i];
        sum += v * v;
        n++;
    }

    if (n == 0) return -120.0;

    return 20.0 * std::log10(std::sqrt(sum / static_cast<double>(n)) + 1e-9);
}

static double estimateSpeechThreshold(
        const std::string &path,
        const WavInfo &info
) {
    const int targetRate = 16000;
    const int frameSamples = targetRate * 20 / 1000;

    const uint64_t maxScanFrames =
            std::min<uint64_t>(
                    info.totalFrames(),
                    static_cast<uint64_t>(info.sampleRate) * 60ULL
            );

    const uint64_t readBlockFrames = static_cast<uint64_t>(info.sampleRate) * 10ULL;

    std::vector<double> dbValues;

    for (uint64_t start = 0; start < maxScanFrames; start += readBlockFrames) {
        std::vector<float> pcm;
        std::string err;

        uint64_t count = std::min<uint64_t>(readBlockFrames, maxScanFrames - start);

        if (!readWavSegmentAs16kMono(path, info, start, count, pcm, err)) {
            continue;
        }

        if (pcm.empty()) {
            continue;
        }

        size_t frames = (pcm.size() + frameSamples - 1) / frameSamples;

        for (size_t i = 0; i < frames; ++i) {
            size_t s = i * frameSamples;
            size_t e = std::min(s + static_cast<size_t>(frameSamples), pcm.size());
            dbValues.push_back(rmsDbRange(pcm, s, e));
        }
    }

    if (dbValues.empty()) {
        return -45.0;
    }

    std::sort(dbValues.begin(), dbValues.end());

    size_t noiseIndex = static_cast<size_t>(dbValues.size() * 0.20);
    if (noiseIndex >= dbValues.size()) noiseIndex = dbValues.size() - 1;

    double noiseFloor = dbValues[noiseIndex];

    double threshold = noiseFloor + 10.0;

    if (threshold < -48.0) threshold = -48.0;
    if (threshold > -28.0) threshold = -28.0;

    return threshold;
}

static CleanBlock buildCleanBlock(
        const std::vector<float> &pcm16k,
        int64_t originalBlockStartMs,
        double thresholdDb
) {
    CleanBlock result;

    const int sampleRate = 16000;
    const int frameSamples = sampleRate * 20 / 1000;
    const int padFrames = 12;

    if (pcm16k.empty()) {
        return result;
    }

    const size_t frameCount = (pcm16k.size() + frameSamples - 1) / frameSamples;

    result.totalFrames20ms = static_cast<int>(frameCount);

    std::vector<bool> speech(frameCount, false);
    std::vector<bool> keep(frameCount, false);

    for (size_t i = 0; i < frameCount; ++i) {
        size_t s = i * frameSamples;
        size_t e = std::min(s + static_cast<size_t>(frameSamples), pcm16k.size());

        double db = rmsDbRange(pcm16k, s, e);

        if (db >= thresholdDb) {
            speech[i] = true;

            size_t a = i > static_cast<size_t>(padFrames) ? i - padFrames : 0;
            size_t b = std::min(frameCount - 1, i + static_cast<size_t>(padFrames));

            for (size_t k = a; k <= b; ++k) {
                keep[k] = true;
            }
        }
    }

    size_t i = 0;

    while (i < frameCount) {
        if (!keep[i]) {
            i++;
            continue;
        }

        size_t groupStart = i;

        while (i < frameCount && keep[i]) {
            i++;
        }

        size_t groupEnd = i;

        size_t sampleStart = groupStart * frameSamples;
        size_t sampleEnd = std::min(groupEnd * static_cast<size_t>(frameSamples), pcm16k.size());

        if (sampleEnd <= sampleStart) {
            continue;
        }

        int64_t cleanStartMs =
                static_cast<int64_t>(result.pcm.size()) * 1000LL / sampleRate;

        result.pcm.insert(
                result.pcm.end(),
                pcm16k.begin() + static_cast<std::ptrdiff_t>(sampleStart),
                pcm16k.begin() + static_cast<std::ptrdiff_t>(sampleEnd)
        );

        int64_t cleanEndMs =
                static_cast<int64_t>(result.pcm.size()) * 1000LL / sampleRate;

        int64_t originalStartMs =
                originalBlockStartMs + static_cast<int64_t>(sampleStart) * 1000LL / sampleRate;

        int64_t originalEndMs =
                originalBlockStartMs + static_cast<int64_t>(sampleEnd) * 1000LL / sampleRate;

        CleanMap m;
        m.cleanStartMs = cleanStartMs;
        m.cleanEndMs = cleanEndMs;
        m.originalStartMs = originalStartMs;
        m.originalEndMs = originalEndMs;

        result.maps.push_back(m);
        result.keptFrames20ms += static_cast<int>(groupEnd - groupStart);
    }

    return result;
}

static int64_t mapCleanMsToOriginalMs(
        int64_t cleanMs,
        const std::vector<CleanMap> &maps
) {
    if (maps.empty()) {
        return cleanMs;
    }

    for (const CleanMap &m : maps) {
        if (cleanMs >= m.cleanStartMs && cleanMs <= m.cleanEndMs) {
            return m.originalStartMs + (cleanMs - m.cleanStartMs);
        }
    }

    if (cleanMs < maps.front().cleanStartMs) {
        return maps.front().originalStartMs;
    }

    return maps.back().originalEndMs;
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
        jobject thiz,
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

    out << "Whisper STT chunked WAV test\n";
    out << "============================\n\n";

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

    WavInfo info;
    std::string err;

    if (!parseWavInfo(wavPath, info, err)) {
        out << "FAILED\n";
        out << "Reason while parsing WAV:\n";
        out << err << "\n";
        std::string result = out.str();
        return env->NewStringUTF(result.c_str());
    }

    int64_t modelSize = fileSize(modelPath);
    int64_t wavSize = fileSize(wavPath);

    out << "Model:\n" << modelPath << "\n";
    out << "Model size: " << formatBytes(modelSize) << "\n\n";

    out << "WAV:\n" << wavPath << "\n";
    out << "WAV size: " << formatBytes(wavSize) << "\n\n";

    out << "Audio info:\n";
    out << "Sample rate: " << info.sampleRate << " Hz\n";
    out << "Channels: " << info.channels << "\n";
    out << "Bits per sample: " << info.bitsPerSample << "\n";
    out << "Duration: " << std::fixed << std::setprecision(2) << info.durationSec() << " sec\n\n";

    callNativeStatus(env, thiz, "Estimating noise floor and speech threshold...");

    double thresholdDb = estimateSpeechThreshold(wavPath, info);

    out << "VAD threshold:\n";
    out << std::fixed << std::setprecision(2) << thresholdDb << " dB\n\n";

    callNativeStatus(env, thiz, "Loading Whisper model...");

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

    const uint64_t totalFrames = info.totalFrames();

    const int blockSec = 30;
    const uint64_t blockFrames = static_cast<uint64_t>(info.sampleRate) * blockSec;

    int processedBlocks = 0;
    int speechBlocks = 0;
    int skippedBlocks = 0;
    int totalSegments = 0;

    int64_t totalOriginalMs = static_cast<int64_t>(info.durationSec() * 1000.0);
    int64_t totalCleanMs = 0;

    auto sttTotalStart = std::chrono::steady_clock::now();

    out << "Transcript:\n";
    out << "-----------\n";

    for (uint64_t startFrame = 0; startFrame < totalFrames; startFrame += blockFrames) {
        processedBlocks++;

        uint64_t remain = totalFrames - startFrame;
        uint64_t readFrames = std::min(blockFrames, remain);

        int percent = static_cast<int>((startFrame * 100ULL) / std::max<uint64_t>(1, totalFrames));

        {
            std::ostringstream status;
            status << "STT processing...\n\n";
            status << "Progress: " << percent << "%\n";
            status << "Block: " << processedBlocks << "\n";
            status << "Time: " << timestampMs(static_cast<int64_t>(startFrame) * 1000LL / info.sampleRate)
                   << " / "
                   << timestampMs(totalOriginalMs) << "\n\n";
            status << "Speech blocks: " << speechBlocks << "\n";
            status << "Skipped silent blocks: " << skippedBlocks;
            callNativeStatus(env, thiz, status.str());
        }

        std::vector<float> blockPcm;
        std::string readErr;

        if (!readWavSegmentAs16kMono(wavPath, info, startFrame, readFrames, blockPcm, readErr)) {
            out << "\n[WARN] block read failed at frame " << startFrame << ": " << readErr << "\n";
            continue;
        }

        if (blockPcm.empty()) {
            skippedBlocks++;
            continue;
        }

        int64_t blockStartMs = static_cast<int64_t>(startFrame) * 1000LL / info.sampleRate;

        CleanBlock clean = buildCleanBlock(blockPcm, blockStartMs, thresholdDb);

        if (clean.pcm.size() < 1600 || clean.maps.empty()) {
            skippedBlocks++;
            continue;
        }

        speechBlocks++;
        totalCleanMs += static_cast<int64_t>(clean.pcm.size()) * 1000LL / 16000LL;

        auto blockStart = std::chrono::steady_clock::now();

        int ret = whisper_full(
                ctx,
                params,
                clean.pcm.data(),
                static_cast<int>(clean.pcm.size())
        );

        auto blockEnd = std::chrono::steady_clock::now();

        long long blockMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                blockEnd - blockStart
        ).count();

        if (ret != 0) {
            out << "\n[WARN] whisper_full failed at block " << processedBlocks
                << ", ret=" << ret << "\n";
            continue;
        }

        int nSegments = whisper_full_n_segments(ctx);

        if (nSegments <= 0) {
            out << "\n[INFO] no segment in block "
                << processedBlocks
                << " / STT time "
                << blockMs
                << " ms\n";
            continue;
        }

        for (int i = 0; i < nSegments; ++i) {
            int64_t cleanT0 = whisper_full_get_segment_t0(ctx, i) * 10;
            int64_t cleanT1 = whisper_full_get_segment_t1(ctx, i) * 10;

            int64_t origT0 = mapCleanMsToOriginalMs(cleanT0, clean.maps);
            int64_t origT1 = mapCleanMsToOriginalMs(cleanT1, clean.maps);

            const char *text = whisper_full_get_segment_text(ctx, i);
            std::string line = text ? text : "";

            out << "["
                << timestampMs(origT0)
                << " - "
                << timestampMs(origT1)
                << "] "
                << line
                << "\n";

            totalSegments++;
        }
    }

    auto sttTotalEnd = std::chrono::steady_clock::now();

    long long totalSttMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            sttTotalEnd - sttTotalStart
    ).count();

    whisper_free(ctx);

    callNativeStatus(env, thiz, "STT finished. Preparing result text...");

    out << "\nSummary:\n";
    out << "--------\n";
    out << "Original duration: " << timestampMs(totalOriginalMs) << "\n";
    out << "Estimated speech duration after silence removal: " << timestampMs(totalCleanMs) << "\n";
    out << "Estimated removed duration: " << timestampMs(std::max<int64_t>(0, totalOriginalMs - totalCleanMs)) << "\n";
    out << "Processed blocks: " << processedBlocks << "\n";
    out << "Speech blocks: " << speechBlocks << "\n";
    out << "Skipped silent blocks: " << skippedBlocks << "\n";
    out << "Segments: " << totalSegments << "\n";
    out << "Total STT time: " << totalSttMs << " ms\n\n";

    out << "System info:\n";
    const char *sys = whisper_print_system_info();
    if (sys != nullptr) {
        out << sys;
    } else {
        out << "(no system info)";
    }

    std::string result = out.str();
    return env->NewStringUTF(result.c_str());
}
