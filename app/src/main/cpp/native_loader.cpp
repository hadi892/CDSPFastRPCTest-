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

#define REMOTE_SCALARS_MAKEX(attr, method, in, out, rout, oin) \
    ((((uint32_t)(attr))   & 0x7)   << 29 | \
     (((uint32_t)(method)) & 0x1fff)<< 16 | \
     (((uint32_t)(in))     & 0xff)  << 8  | \
     (((uint32_t)(out))    & 0xff))

// Qualcomm FastRPC function pointer types
typedef int (*pfn_remote_handle_open)(const char* name, remote_handle* handle);
typedef int (*pfn_remote_handle_close)(remote_handle handle);
typedef int (*pfn_remote_handle_invoke)(remote_handle handle, uint32_t sc, remote_arg* pra);
typedef int (*pfn_remote_handle64_open)(const char* name, remote_handle64* handle);
typedef int (*pfn_remote_handle64_close)(remote_handle64 handle);
typedef int (*pfn_remote_handle64_invoke)(remote_handle64 handle, uint32_t sc, remote_arg64* pra);
typedef int (*pfn_remote_set_mode)(int mode);
typedef int (*pfn_remote_session_control)(uint32_t req, void* data, uint32_t len);

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
    std::ostringstream log;
    std::string currentStage = "INITIALIZING";
    bool executionPassed = false;
    std::string failureReason = "";
    int failureErrno = 0;
    int failureReturnCode = 0;

    auto appendLog = [&](const std::string& prefix, const std::string& msg) {
        log << "[" << getTimestamp() << "] " << prefix << " " << msg << "\n";
        LOGI("%s %s", prefix.c_str(), msg.c_str());
    };

    appendLog("[START]", "FastRPC initialization on CDSP domain (target: SM6375 / Blair)");
    appendLog("[INFO]", "Test function: test_add(" + std::to_string(a) + ", " + std::to_string(b) + ")");

    // -------------------------------------------------------------
    // Stage 1: FAST RPC LIBRARY PROBING & LOADING
    // -------------------------------------------------------------
    currentStage = "FAST RPC LIBRARY";
    appendLog("[START]", "Loading Qualcomm FastRPC Userspace Library");

    const char* libCandidates[] = {
        "libcdsprpc.so",
        "/vendor/lib64/libcdsprpc.so",
        "/system/lib64/libcdsprpc.so",
        "/vendor/lib64/chipset-subsys/libcdsprpc.so",
        "libadsprpc.so",
        "/vendor/lib64/libadsprpc.so",
        "/system/lib64/libadsprpc.so",
        "libfastrpc.so",
        nullptr
    };

    void* libHandle = nullptr;
    std::string loadedLibName = "";
    std::string lastDlError = "";

    for (int i = 0; libCandidates[i] != nullptr; ++i) {
        const char* candidate = libCandidates[i];
        dlerror(); // clear existing error
        void* h = dlopen(candidate, RTLD_NOW);
        if (h != nullptr) {
            libHandle = h;
            loadedLibName = candidate;
            appendLog("[OK]", "Successfully loaded FastRPC library: " + loadedLibName);
            break;
        } else {
            const char* err = dlerror();
            std::string errStr = (err != nullptr) ? err : "Unknown dlopen failure";
            lastDlError = errStr;
            appendLog("[DEBUG]", "dlopen(\"" + std::string(candidate) + "\") failed: " + errStr);
        }
    }

    if (libHandle == nullptr) {
        currentStage = "FAST RPC LIBRARY";
        failureReason = "Failed to load any Qualcomm FastRPC userspace library (libcdsprpc.so / libadsprpc.so). Root cause: Android Linker Namespace restriction or library absent. dlerror: " + lastDlError;
        failureErrno = errno;
        appendLog("[FAIL]", "Stage: " + currentStage + " | Error: " + failureReason);
        goto format_result;
    }

    // -------------------------------------------------------------
    // Stage 2: RESOLVING FASTRPC SYMBOLS
    // -------------------------------------------------------------
    currentStage = "RESOLVING SYMBOLS";
    appendLog("[START]", "Resolving FastRPC exported symbols in " + loadedLibName);

    pfn_remote_handle_open fn_remote_handle_open;
    pfn_remote_handle_close fn_remote_handle_close;
    pfn_remote_handle_invoke fn_remote_handle_invoke;
    pfn_remote_handle64_open fn_remote_handle64_open;
    pfn_remote_handle64_close fn_remote_handle64_close;
    pfn_remote_handle64_invoke fn_remote_handle64_invoke;
    pfn_remote_set_mode fn_remote_set_mode;
    pfn_remote_session_control fn_remote_session_control;

    fn_remote_handle_open = (pfn_remote_handle_open)dlsym(libHandle, "remote_handle_open");
    fn_remote_handle_close = (pfn_remote_handle_close)dlsym(libHandle, "remote_handle_close");
    fn_remote_handle_invoke = (pfn_remote_handle_invoke)dlsym(libHandle, "remote_handle_invoke");

    fn_remote_handle64_open = (pfn_remote_handle64_open)dlsym(libHandle, "remote_handle64_open");
    fn_remote_handle64_close = (pfn_remote_handle64_close)dlsym(libHandle, "remote_handle64_close");
    fn_remote_handle64_invoke = (pfn_remote_handle64_invoke)dlsym(libHandle, "remote_handle64_invoke");

    fn_remote_set_mode = (pfn_remote_set_mode)dlsym(libHandle, "remote_set_mode");
    fn_remote_session_control = (pfn_remote_session_control)dlsym(libHandle, "remote_session_control");

    appendLog("[INFO]", std::string("remote_handle_open: ") + (fn_remote_handle_open ? "FOUND" : "NULL"));
    appendLog("[INFO]", std::string("remote_handle_close: ") + (fn_remote_handle_close ? "FOUND" : "NULL"));
    appendLog("[INFO]", std::string("remote_handle_invoke: ") + (fn_remote_handle_invoke ? "FOUND" : "NULL"));
    appendLog("[INFO]", std::string("remote_handle64_open: ") + (fn_remote_handle64_open ? "FOUND" : "NULL"));
    appendLog("[INFO]", std::string("remote_handle64_close: ") + (fn_remote_handle64_close ? "FOUND" : "NULL"));
    appendLog("[INFO]", std::string("remote_handle64_invoke: ") + (fn_remote_handle64_invoke ? "FOUND" : "NULL"));
    appendLog("[INFO]", std::string("remote_set_mode: ") + (fn_remote_set_mode ? "FOUND" : "NULL"));

    if (!fn_remote_handle_open && !fn_remote_handle64_open) {
        currentStage = "RESOLVING SYMBOLS";
        failureReason = "Required symbol remote_handle_open / remote_handle64_open not found in " + loadedLibName;
        failureErrno = errno;
        appendLog("[FAIL]", "Stage: " + currentStage + " | Error: " + failureReason);
        dlclose(libHandle);
        goto format_result;
    }
    appendLog("[OK]", "FastRPC symbols resolved");

    // -------------------------------------------------------------
    // Stage 3: OPENING CDSP (Setting Mode / Domain ID = 3)
    // -------------------------------------------------------------
    currentStage = "OPENING CDSP";
    appendLog("[START]", "Opening CDSP (Setting FastRPC mode/domain to CDSP = " + std::to_string(CDSP_DOMAIN_ID) + ")");

    if (fn_remote_set_mode != nullptr) {
        int setModeRes = fn_remote_set_mode(CDSP_DOMAIN_ID);
        if (setModeRes != 0) {
            appendLog("[WARN]", "remote_set_mode(CDSP_DOMAIN_ID=3) returned " + std::to_string(setModeRes) + " (errno=" + std::to_string(errno) + ")");
        } else {
            appendLog("[OK]", "CDSP domain mode configured successfully (return 0)");
        }
    } else {
        appendLog("[INFO]", "remote_set_mode symbol not present; relying on URI domain specification");
    }

    // -------------------------------------------------------------
    // Stage 4: LOADING REMOTE OBJECT / CREATING HANDLE
    // -------------------------------------------------------------
    currentStage = "LOADING REMOTE OBJECT";
    appendLog("[START]", "Loading remote DSP object on CDSP domain");

    {
        // Try standard URI string formats for CDSP remote objects
        // Format: "<uri>?_dom=cdsp" or "remote_test" or "calculator"
        const char* remoteUriCandidates[] = {
            "remote_test?_dom=cdsp",
            "calculator?_dom=cdsp",
            "remote_test",
            "calculator",
            "libcalculator_skel.so",
            nullptr
        };

        remote_handle handle32 = 0;
        remote_handle64 handle64 = 0;
        bool handleOpened = false;
        std::string successfulUri = "";
        int lastOpenRes = -1;

        for (int i = 0; remoteUriCandidates[i] != nullptr; ++i) {
            const char* uri = remoteUriCandidates[i];
            appendLog("[DEBUG]", "Attempting remote open for URI: " + std::string(uri));

            if (fn_remote_handle64_open != nullptr) {
                handle64 = 0;
                int res = fn_remote_handle64_open(uri, &handle64);
                lastOpenRes = res;
                if (res == 0 && handle64 != 0) {
                    handleOpened = true;
                    successfulUri = uri;
                    appendLog("[OK]", "remote_handle64_open succeeded for URI '" + successfulUri + "', handle=0x" + std::to_string(handle64));
                    break;
                } else {
                    appendLog("[DEBUG]", "remote_handle64_open('" + std::string(uri) + "') failed: res=" + std::to_string(res) + ", errno=" + std::to_string(errno) + " (" + strerror(errno) + ")");
                }
            } else if (fn_remote_handle_open != nullptr) {
                handle32 = 0;
                int res = fn_remote_handle_open(uri, &handle32);
                lastOpenRes = res;
                if (res == 0 && handle32 != 0) {
                    handleOpened = true;
                    successfulUri = uri;
                    appendLog("[OK]", "remote_handle_open succeeded for URI '" + successfulUri + "', handle=0x" + std::to_string(handle32));
                    break;
                } else {
                    appendLog("[DEBUG]", "remote_handle_open('" + std::string(uri) + "') failed: res=" + std::to_string(res) + ", errno=" + std::to_string(errno) + " (" + strerror(errno) + ")");
                }
            }
        }

        if (!handleOpened) {
            currentStage = "LOADING REMOTE OBJECT";
            failureReturnCode = lastOpenRes;
            failureErrno = errno;
            failureReason = "remote_handle_open failed for all remote object URIs. Return code: " + std::to_string(lastOpenRes) + ", errno: " + std::to_string(errno) + " (" + strerror(errno) + "). Potential causes: SELinux denial on /dev/fastrpc-cdsp, missing remote skel .so in DSP path, or unprivileged app domain.";
            appendLog("[FAIL]", "Stage: " + currentStage + " | Error: " + failureReason);
            dlclose(libHandle);
            goto format_result;
        }

        currentStage = "HANDLE CREATED";
        appendLog("[OK]", "Remote DSP handle created successfully for " + successfulUri);

        // -------------------------------------------------------------
        // Stage 5: REMOTE FUNCTION CALL (test_add(a, b))
        // -------------------------------------------------------------
        currentStage = "REMOTE FUNCTION CALL";
        appendLog("[START]", "Executing remote function test_add(" + std::to_string(a) + ", " + std::to_string(b) + ") on CDSP");

        int32_t arg_a = a;
        int32_t arg_b = b;
        int32_t dsp_result = 0;
        int invokeRes = -1;

        // Method index 0, in: 2 (arg_a, arg_b), out: 1 (dsp_result)
        uint32_t sc = REMOTE_SCALARS_MAKEX(0, 0, 2, 1, 0, 0);

        if (handle64 != 0 && fn_remote_handle64_invoke != nullptr) {
            remote_arg64 pra[3];
            pra[0].buf.pv = &arg_a;
            pra[0].buf.len = sizeof(arg_a);
            pra[1].buf.pv = &arg_b;
            pra[1].buf.len = sizeof(arg_b);
            pra[2].buf.pv = &dsp_result;
            pra[2].buf.len = sizeof(dsp_result);

            invokeRes = fn_remote_handle64_invoke(handle64, sc, pra);
        } else if (handle32 != 0 && fn_remote_handle_invoke != nullptr) {
            remote_arg pra[3];
            pra[0].buf.pv = &arg_a;
            pra[0].buf.len = sizeof(arg_a);
            pra[1].buf.pv = &arg_b;
            pra[1].buf.len = sizeof(arg_b);
            pra[2].buf.pv = &dsp_result;
            pra[2].buf.len = sizeof(dsp_result);

            invokeRes = fn_remote_handle_invoke(handle32, sc, pra);
        }

        if (invokeRes != 0) {
            currentStage = "REMOTE FUNCTION CALL";
            failureReturnCode = invokeRes;
            failureErrno = errno;
            failureReason = "remote_handle_invoke returned error code " + std::to_string(invokeRes) + " (errno=" + std::to_string(errno) + ": " + strerror(errno) + ")";
            appendLog("[FAIL]", "Stage: " + currentStage + " | Error: " + failureReason);

            // Teardown handle
            if (handle64 != 0 && fn_remote_handle64_close != nullptr) {
                fn_remote_handle64_close(handle64);
            } else if (handle32 != 0 && fn_remote_handle_close != nullptr) {
                fn_remote_handle_close(handle32);
            }
            dlclose(libHandle);
            goto format_result;
        }

        // -------------------------------------------------------------
        // Stage 6: RESULT VALIDATION
        // -------------------------------------------------------------
        currentStage = "RESULT";
        appendLog("[OK]", "DSP invocation succeeded! Physical CDSP returned: " + std::to_string(dsp_result));

        int expectedSum = a + b;
        if (dsp_result == expectedSum) {
            appendLog("[PASS]", "DSP returned expected value: " + std::to_string(dsp_result) + " == " + std::to_string(expectedSum));
            executionPassed = true;
        } else {
            currentStage = "RESULT";
            failureReason = "DSP returned unexpected value " + std::to_string(dsp_result) + " (expected " + std::to_string(expectedSum) + ")";
            appendLog("[FAIL]", "Stage: " + currentStage + " | Error: " + failureReason);
        }

        // -------------------------------------------------------------
        // Stage 7: CLOSE HANDLE
        // -------------------------------------------------------------
        currentStage = "CLOSE";
        appendLog("[START]", "Closing FastRPC remote handle");
        if (handle64 != 0 && fn_remote_handle64_close != nullptr) {
            int closeRes = fn_remote_handle64_close(handle64);
            appendLog("[INFO]", "remote_handle64_close returned " + std::to_string(closeRes));
        } else if (handle32 != 0 && fn_remote_handle_close != nullptr) {
            int closeRes = fn_remote_handle_close(handle32);
            appendLog("[INFO]", "remote_handle_close returned " + std::to_string(closeRes));
        }
        appendLog("[OK]", "Handle closed");
    }

    if (libHandle != nullptr) {
        dlclose(libHandle);
        appendLog("[OK]", "Unloaded FastRPC userspace library");
    }

format_result:
    std::ostringstream fullOutput;
    fullOutput << "--- DIAGNOSTIC RUN LOG ---\n";
    fullOutput << log.str();
    fullOutput << "\n--- REAL HARDWARE RESULT ---\n";
    if (executionPassed) {
        fullOutput << "[PASS] FastRPC library loaded\n";
        fullOutput << "[PASS] CDSP opened\n";
        fullOutput << "[PASS] Remote DSP object loaded\n";
        fullOutput << "[PASS] Remote DSP handle created\n";
        fullOutput << "[PASS] Remote function executed\n";
        fullOutput << "[PASS] DSP returned " << (a + b) << "\n";
        fullOutput << "[PASS] Remote handle closed\n";
        fullOutput << "\nFINAL RESULT:\nREAL CDSP EXECUTION = PASS\n";
    } else {
        fullOutput << "[FAIL] Stage: " << currentStage << "\n";
        fullOutput << "Error:\n" << failureReason << "\n";
        if (failureReturnCode != 0) {
            fullOutput << "Return Code: " << failureReturnCode << "\n";
        }
        if (failureErrno != 0) {
            fullOutput << "Errno: " << failureErrno << " (" << strerror(failureErrno) << ")\n";
        }
        fullOutput << "\nFINAL RESULT:\nREAL CDSP EXECUTION = FAIL\n";
    }

    return env->NewStringUTF(fullOutput.str().c_str());
}

} // extern "C"
