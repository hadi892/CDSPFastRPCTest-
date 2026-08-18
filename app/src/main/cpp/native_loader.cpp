#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#define TAG "CDSPFastRPCTest"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

#define CDSP_DOMAIN_ID 3

// FastRPC handle and buffer types
typedef uint32_t remote_handle;
typedef uint64_t remote_handle64;

typedef union {
    void *pv;
    uint64_t len;
} remote_buf;

typedef struct {
    void *pv;
    size_t len;
} remote_buf64;

typedef union {
    remote_buf buf;
    remote_handle h;
} remote_arg;

typedef union {
    remote_buf64 buf;
    remote_handle64 h;
} remote_arg64;

// Qualcomm FastRPC function pointer signatures
typedef int (*pfn_remote_handle64_open)(const char* name, remote_handle64* handle);
typedef int (*pfn_remote_handle64_invoke)(remote_handle64 handle, uint32_t sc, remote_arg64* pra);
typedef int (*pfn_remote_handle64_close)(remote_handle64 handle);
typedef void* (*pfn_rpcmem_alloc)(int heapid, uint32_t flags, int size);

typedef int (*pfn_remote_handle_open)(const char* name, remote_handle* handle);
typedef int (*pfn_remote_handle_invoke)(remote_handle handle, uint32_t sc, remote_arg* pra);
typedef int (*pfn_remote_handle_close)(remote_handle handle);

static std::string getTimestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_buf;
    localtime_r(&ts.tv_sec, &tm_buf);
    char buf[64];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03ld",
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, ts.tv_nsec / 1000000L);
    return std::string(buf);
}

static std::string checkSingleDeviceNode(const char* path) {
    std::ostringstream ss;
    ss << "Node: " << path << " -> ";
    struct stat st;
    if (stat(path, &st) != 0) {
        int err = errno;
        ss << "MISSING (" << strerror(err) << ", errno=" << err << ")";
        return ss.str();
    }

    ss << "EXISTS (mode=" << std::oct << (st.st_mode & 0777) << std::dec
       << ", uid=" << st.st_uid << ", gid=" << st.st_gid << ")";

    int fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd >= 0) {
        ss << " | OPEN(O_RDWR): GRANTED (fd=" << fd << ")";
        close(fd);
    } else {
        int err = errno;
        ss << " | OPEN(O_RDWR): DENIED (" << strerror(err) << ", errno=" << err << ")";
    }
    return ss.str();
}

static std::string getSelinuxStatus() {
    std::ostringstream ss;
    int fd = open("/sys/fs/selinux/enforce", O_RDONLY);
    if (fd >= 0) {
        char val = '0';
        ssize_t r = read(fd, &val, 1);
        close(fd);
        if (r > 0) {
            ss << (val == '1' ? "Enforcing (1)" : "Permissive (0)");
        } else {
            ss << "Unknown (read failed)";
        }
    } else {
        ss << "Enforcing (assumed, /sys/fs/selinux/enforce inaccessible: " << strerror(errno) << ")";
    }
    return ss.str();
}

struct DlopenTestResult {
    std::string path;
    int flags;
    bool success;
    void* handle;
    std::string dlerrorStr;
    int savedErrno;
};

static DlopenTestResult runDlopenTest(const char* path, int flags) {
    DlopenTestResult res;
    res.path = path;
    res.flags = flags;
    dlerror(); // Clear previous error
    errno = 0;

    void* h = dlopen(path, flags);
    res.savedErrno = errno;
    const char* err = dlerror();
    res.dlerrorStr = (err != nullptr) ? err : "None";
    res.handle = h;
    res.success = (h != nullptr);
    return res;
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_cdsfastrpctest_FastRpcBridge_isNativeLoaded(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL
Java_com_cdsfastrpctest_FastRpcBridge_checkSystemEnvironment(JNIEnv* env, jobject thiz) {
    (void)thiz;
    std::ostringstream ss;
    ss << "=== SYSTEM & HARDWARE DIAGNOSTICS ===\n";
    ss << "Time: " << getTimestamp() << "\n";
    ss << "Target Device: Samsung Galaxy Tab A9+ 5G (SM-X216B)\n";
    ss << "Platform SoC: Qualcomm SM6375 / Snapdragon 695 (Blair)\n";
    ss << "Target DSP Domain: CDSP (Domain ID = " << CDSP_DOMAIN_ID << ")\n";
#if defined(__aarch64__)
    ss << "Native ABI: arm64-v8a (64-bit)\n";
#else
    ss << "Native ABI: Non-64-bit (" << __ARM_ARCH << ")\n";
#endif
    ss << "SELinux Status: " << getSelinuxStatus() << "\n";
    ss << "Process UID: " << getuid() << " / GID: " << getgid() << "\n";
    ss << "Process PID: " << getpid() << "\n";
    return env->NewStringUTF(ss.str().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_cdsfastrpctest_FastRpcBridge_checkDeviceNodes(JNIEnv* env, jobject thiz) {
    (void)thiz;
    std::ostringstream ss;
    ss << "=== KERNEL FASTRPC CHARACTER DEVICE NODES ===\n";
    const char* nodes[] = {
        "/dev/fastrpc-cdsp",
        "/dev/fastrpc-cdsp-secure",
        "/dev/adsprpc-smd",
        "/dev/adsprpc-smd-secure",
        "/dev/msm_fastrpc",
        "/dev/smdcntl8",
        "/dev/ion",
        "/dev/dma_heap/qcom,system",
        nullptr
    };

    for (int i = 0; nodes[i] != nullptr; ++i) {
        ss << checkSingleDeviceNode(nodes[i]) << "\n";
    }
    return env->NewStringUTF(ss.str().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_cdsfastrpctest_FastRpcBridge_executeCdspTest(JNIEnv* env, jobject thiz, jint a, jint b) {
    (void)thiz;
    (void)a;
    (void)b;

    std::ostringstream log;

    auto appendLog = [&](const std::string& prefix, const std::string& msg) {
        log << "[" << getTimestamp() << "] " << prefix << " " << msg << "\n";
        LOGI("%s %s", prefix.c_str(), msg.c_str());
    };

    appendLog("[DIAGNOSTIC]", "Starting Linker Namespace & Vendor FastRPC Library Probing");
    appendLog("[INFO]", "Evaluating if unprivileged application linker namespace can load vendor FastRPC");

    // =========================================================================
    // RUNTIME TESTS: Dynamic Linker Namespace Probing (RTLD_NOW | RTLD_LOCAL)
    // 1. dlopen("libcdsprpc.so", RTLD_NOW | RTLD_LOCAL)
    // 2. dlopen("/vendor/lib64/libcdsprpc.so", RTLD_NOW | RTLD_LOCAL)
    // 3. dlopen("libadsprpc.so", RTLD_NOW | RTLD_LOCAL)
    // 4. dlopen("/vendor/lib64/libadsprpc.so", RTLD_NOW | RTLD_LOCAL)
    // =========================================================================

    const struct {
        const char* path;
        const char* label;
    } tests[] = {
        { "libcdsprpc.so", "Test 1: dlopen(\"libcdsprpc.so\", RTLD_NOW | RTLD_LOCAL)" },
        { "/vendor/lib64/libcdsprpc.so", "Test 2: dlopen(\"/vendor/lib64/libcdsprpc.so\", RTLD_NOW | RTLD_LOCAL)" },
        { "libadsprpc.so", "Test 3: dlopen(\"libadsprpc.so\", RTLD_NOW | RTLD_LOCAL)" },
        { "/vendor/lib64/libadsprpc.so", "Test 4: dlopen(\"/vendor/lib64/libadsprpc.so\", RTLD_NOW | RTLD_LOCAL)" }
    };

    std::vector<DlopenTestResult> results;
    void* activeCdsprpcHandle = nullptr;
    std::string activeCdsprpcPath = "";

    for (int i = 0; i < 4; ++i) {
        appendLog("[TEST_START]", tests[i].label);
        DlopenTestResult res = runDlopenTest(tests[i].path, RTLD_NOW | RTLD_LOCAL);
        results.push_back(res);

        std::string statusStr = res.success ? "SUCCESS" : "FAILURE";
        appendLog(res.success ? "[RESULT_OK]" : "[RESULT_FAIL]",
                  "Target: " + res.path +
                  " | Status: " + statusStr +
                  " | dlerror: \"" + res.dlerrorStr + "\"" +
                  " | errno: " + std::to_string(res.savedErrno) + " (" + strerror(res.savedErrno) + ")");

        // Track first successful libcdsprpc.so handle for symbol resolution
        if (res.success && activeCdsprpcHandle == nullptr &&
            (res.path == "libcdsprpc.so" || res.path == "/vendor/lib64/libcdsprpc.so")) {
            activeCdsprpcHandle = res.handle;
            activeCdsprpcPath = res.path;
        } else if (res.success && activeCdsprpcHandle != res.handle) {
            // Close other handles not used for symbol inspection
            dlclose(res.handle);
        }
    }

    // =========================================================================
    // SYMBOL RESOLUTION (If libcdsprpc.so successfully loaded)
    // Symbols to resolve:
    // - remote_handle64_open
    // - remote_handle64_invoke
    // - remote_handle64_close
    // - rpcmem_alloc
    // =========================================================================

    bool symbolsResolved = false;
    struct SymbolCheck {
        const char* name;
        void* address;
        std::string err;
        bool found;
    };

    std::vector<SymbolCheck> symbolChecks;

    if (activeCdsprpcHandle != nullptr) {
        appendLog("[START]", "libcdsprpc.so loaded from " + activeCdsprpcPath + ". Resolving required FastRPC symbols via dlsym()...");

        const char* symbolsToQuery[] = {
            "remote_handle64_open",
            "remote_handle64_invoke",
            "remote_handle64_close",
            "rpcmem_alloc",
            nullptr
        };

        int foundCount = 0;
        for (int i = 0; symbolsToQuery[i] != nullptr; ++i) {
            const char* symName = symbolsToQuery[i];
            dlerror(); // Clear error
            void* symPtr = dlsym(activeCdsprpcHandle, symName);
            const char* symErr = dlerror();

            SymbolCheck sc;
            sc.name = symName;
            sc.address = symPtr;
            sc.err = (symErr != nullptr) ? symErr : "None";
            sc.found = (symPtr != nullptr);
            symbolChecks.push_back(sc);

            if (sc.found) {
                foundCount++;
                appendLog("[SYM_OK]", std::string("dlsym(\"") + symName + "\") -> FOUND at " +
                          (symPtr ? "valid address" : "NULL"));
            } else {
                appendLog("[SYM_FAIL]", std::string("dlsym(\"") + symName + "\") -> NOT FOUND: " + sc.err);
            }
        }

        symbolsResolved = (foundCount > 0);

        // Clean up library handle after diagnostic inspection
        dlclose(activeCdsprpcHandle);
        appendLog("[CLEANUP]", "Closed libcdsprpc.so diagnostic handle");
    } else {
        appendLog("[INFO]", "libcdsprpc.so was not loaded in unprivileged linker namespace; symbol resolution skipped.");
    }

    // =========================================================================
    // COMPOSE DIAGNOSTIC REPORT (DO NOT CALL DSP / NEVER REPORT PASS FOR DSP)
    // =========================================================================
    std::ostringstream fullOutput;
    fullOutput << "=== FASTRPC LINKER NAMESPACE DIAGNOSTIC REPORT ===\n\n";
    fullOutput << "--- RUNTIME LINKER TESTS (RTLD_NOW | RTLD_LOCAL) ---\n";
    for (size_t i = 0; i < results.size(); ++i) {
        fullOutput << "[" << (i + 1) << "] " << results[i].path << "\n";
        fullOutput << "    Result  : " << (results[i].success ? "SUCCESS" : "FAILURE") << "\n";
        fullOutput << "    dlerror : " << results[i].dlerrorStr << "\n";
        fullOutput << "    errno   : " << results[i].savedErrno << " (" << strerror(results[i].savedErrno) << ")\n\n";
    }

    fullOutput << "--- SYMBOL RESOLUTION (libcdsprpc.so) ---\n";
    if (activeCdsprpcPath.empty()) {
        fullOutput << "libcdsprpc.so could not be opened by the app linker namespace.\n";
        fullOutput << "No symbols resolved.\n\n";
    } else {
        fullOutput << "Target Library: " << activeCdsprpcPath << "\n";
        for (size_t i = 0; i < symbolChecks.size(); ++i) {
            fullOutput << "- " << symbolChecks[i].name << " : "
                       << (symbolChecks[i].found ? "FOUND" : "NOT FOUND")
                       << " (dlerror: " << symbolChecks[i].err << ")\n";
        }
        fullOutput << "\n";
    }

    fullOutput << "--- EXECUTION STATUS ---\n";
    fullOutput << "DSP Execution Status : NOT EXECUTED (Diagnostic Stage Only)\n";
    fullOutput << "Linker Namespace Probe: "
               << ((results[0].success || results[1].success || results[2].success || results[3].success) ? "LIBRARY ACCESSIBLE" : "RESTRICTED BY LINKER NAMESPACE")
               << "\n";
    fullOutput << "\nFINAL RESULT:\nREAL CDSP EXECUTION = NOT RUN (Diagnostic Stage - Linker Probe Only)\n";

    return env->NewStringUTF(fullOutput.str().c_str());
}

} // extern "C"
