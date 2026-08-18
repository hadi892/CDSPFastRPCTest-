package com.cdsfastrpctest

/**
 * JNI Bridge interface to the native ARM64 CDSP FastRPC loader.
 */
object FastRpcBridge {
    private var nativeLibraryLoaded = false
    private var nativeLoadError: String? = null

    init {
        try {
            System.loadLibrary("cdsp_loader")
            nativeLibraryLoaded = true
        } catch (e: UnsatisfiedLinkError) {
            nativeLibraryLoaded = false
            nativeLoadError = "UnsatisfiedLinkError: ${e.message}"
        } catch (e: Throwable) {
            nativeLibraryLoaded = false
            nativeLoadError = "Error loading libcdsp_loader.so: ${e.message}"
        }
    }

    fun isLoaded(): Boolean = nativeLibraryLoaded

    fun getLoadError(): String? = nativeLoadError

    external fun checkSystemEnvironment(): String

    external fun checkDeviceNodes(): String

    external fun executeCdspTest(a: Int, b: Int): String
}
