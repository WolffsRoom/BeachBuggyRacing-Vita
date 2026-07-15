#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI_Logger.h>

#include <psp2/io/fcntl.h>

#include "utils/logger.h"

extern void port_trace(const char *format, ...);

/*
 * Java services used by Vector Unit's Purple engine. Online services, ads,
 * billing and analytics are deliberately inert. Expansion.apf is different:
 * the native engine reads it through VuExpansionFileHelper, so that helper is
 * backed by the extracted APK asset below.
 */

static SceUID expansion_fd = -1;
static int dummy_java_object;

static void method_void_stub(jmethodID id, va_list args) {
    (void)id; (void)args;
}

static void method_show_toast(jmethodID id, va_list args) {
    (void)id;
    jstring text = va_arg(args, jstring);
    const char *message = text
        ? jni->GetStringUTFChars(&jni, text, NULL) : "(null)";
    l_info("Java toast/error: %s", message);
}

static jobject object_dummy(jmethodID id, va_list args) {
    (void)id; (void)args;
    return (jobject)&dummy_java_object;
}

static jobject object_null(jmethodID id, va_list args) {
    (void)id; (void)args;
    return NULL;
}

static jobject string_empty(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, "");
}

static jobject string_data_path(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, DATA_PATH);
}

static jobject string_version(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, "1.2.22");
}

static jobject string_device_id(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, "PSVITA-BBRV00001");
}

static jobject string_language(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, "en");
}

static jobject string_country(jmethodID id, va_list args) {
    (void)id; (void)args;
    return jni->NewStringUTF(&jni, "US");
}

static jboolean boolean_false(jmethodID id, va_list args) {
    (void)id; (void)args;
    return JNI_FALSE;
}

static jboolean boolean_true(jmethodID id, va_list args) {
    (void)id; (void)args;
    return JNI_TRUE;
}

static jboolean fmod_check_init(jmethodID id, va_list args) {
    (void)id; (void)args;
    static int traced;
    if (!traced++)
        port_trace("FMOD Java helper: checkInit=true");
    return JNI_TRUE;
}

static jboolean fmod_supports_low_latency(jmethodID id, va_list args) {
    (void)id; (void)args;
    static int traced;
    if (!traced++)
        port_trace("FMOD Java helper: supportsLowLatency=false");
    return JNI_FALSE;
}

static jobject fmod_asset_manager(jmethodID id, va_list args) {
    (void)id; (void)args;
    static int traced;
    if (!traced++)
        port_trace("FMOD Java helper: getAssetManager=VitaAssetManager");
    return (jobject)&dummy_java_object;
}

static jboolean expansion_open(jmethodID id, va_list args) {
    (void)id; (void)args;
    if (expansion_fd >= 0)
        sceIoClose(expansion_fd);
    expansion_fd = sceIoOpen(DATA_PATH "assets/Expansion.apf",
                             SCE_O_RDONLY, 0);
    l_info("VuExpansionFileHelper.openFile: fd=%d", expansion_fd);
    port_trace("Java expansion: open fd=%d", expansion_fd);
    return expansion_fd >= 0 ? JNI_TRUE : JNI_FALSE;
}

static void expansion_close(jmethodID id, va_list args) {
    (void)id; (void)args;
    if (expansion_fd >= 0) {
        sceIoClose(expansion_fd);
        expansion_fd = -1;
    }
}

static jint expansion_read(jmethodID id, va_list args) {
    (void)id;
    jbyteArray array = va_arg(args, jbyteArray);
    if (expansion_fd < 0 || !array)
        return -1;
    jsize length = jni->GetArrayLength(&jni, array);
    jbyte *bytes = jni->GetByteArrayElements(&jni, array, NULL);
    if (!bytes || length <= 0)
        return -1;
    int result = sceIoRead(expansion_fd, bytes, length);
    static unsigned read_count;
    if (read_count < 8 || result < 0)
        port_trace("Java expansion: read #%u requested=%d result=%d",
                   ++read_count, length, result);
    return result == 0 ? -1 : result;
}

static jboolean expansion_seek(jmethodID id, va_list args) {
    (void)id;
    jint offset = va_arg(args, jint);
    if (expansion_fd < 0)
        return JNI_FALSE;
    SceOff result = sceIoLseek(expansion_fd, offset, SCE_SEEK_SET);
    port_trace("Java expansion: seek offset=%d result=%lld", offset,
               (long long)result);
    return result >= 0 ? JNI_TRUE : JNI_FALSE;
}

static jint integer_zero(jmethodID id, va_list args) {
    (void)id; (void)args;
    return 0;
}

/* FMOD's Android helper class normally gets these values from AudioManager.
 * The Vita output bridge runs at 48 kHz and accepts 512-frame producer blocks.
 * Returning stable values also makes org.fmod.FMOD.checkInit() succeed without
 * a real Android Context. */
static jint fmod_output_sample_rate(jmethodID id, va_list args) {
    (void)id; (void)args;
    static int traced;
    if (!traced++)
        port_trace("FMOD Java helper: outputSampleRate=48000");
    return 48000;
}

static jint fmod_output_block_size(jmethodID id, va_list args) {
    (void)id; (void)args;
    static int traced;
    if (!traced++)
        port_trace("FMOD Java helper: outputBlockSize=512");
    return 512;
}

enum {
    MID_GET_CLASS_LOADER = 1,
    MID_LOAD_CLASS,
    MID_GET_SYSTEM_SERVICE,
    MID_GET_DEFAULT_DISPLAY,
    MID_GET_DEFAULT,
    MID_GET_INSTANCE,
    MID_GET_LANGUAGE,
    MID_GET_COUNTRY,
    MID_GET_ROTATION,
    MID_GET_DEVICE_ID,
    MID_GET_VERSION,
    MID_GET_GAME_CONFIGURATION_VALUE,
    MID_OPEN_CONNECTION,
    MID_GET_FILES_DIR,
    MID_GET_ABSOLUTE_PATH,
    MID_IS_READY_INTERSTITIAL,
    MID_IS_READY_INCENTIVIZED,
    MID_WAS_CONFIGURATION_RECEIVED,
    MID_IS_DOLBY_SUPPORTED,
    MID_IS_DOLBY_ENABLED,
    MID_IS_DEVICE_CONNECTED,
    MID_HAS_TOUCH,
    MID_IS_CONSUMABLE,
    MID_OPEN_FILE,
    MID_SEEK_FILE,
    MID_READ_FILE,
    MID_INITIALIZE,
    MID_CLOSE_FILE,
    MID_START_DOWNLOAD,
    MID_HANDLE_ERROR,
    MID_SHOW_TOAST,
    MID_GENERIC_VOID,
    MID_FMOD_CHECK_INIT,
    MID_FMOD_GET_ASSET_MANAGER,
    MID_FMOD_GET_OUTPUT_BLOCK_SIZE,
    MID_FMOD_GET_OUTPUT_SAMPLE_RATE,
    MID_FMOD_SUPPORTS_LOW_LATENCY,
};

NameToMethodID nameToMethodId[] = {
    { MID_GET_CLASS_LOADER, "getClassLoader", METHOD_TYPE_OBJECT },
    { MID_LOAD_CLASS, "loadClass", METHOD_TYPE_OBJECT },
    { MID_GET_SYSTEM_SERVICE, "getSystemService", METHOD_TYPE_OBJECT },
    { MID_GET_DEFAULT_DISPLAY, "getDefaultDisplay", METHOD_TYPE_OBJECT },
    { MID_GET_DEFAULT, "getDefault", METHOD_TYPE_OBJECT },
    { MID_GET_INSTANCE, "getInstance", METHOD_TYPE_OBJECT },
    { MID_GET_LANGUAGE, "getLanguage", METHOD_TYPE_OBJECT },
    { MID_GET_COUNTRY, "getCountry", METHOD_TYPE_OBJECT },
    { MID_GET_ROTATION, "getRotation", METHOD_TYPE_INT },
    { MID_GET_DEVICE_ID, "getDeviceId", METHOD_TYPE_OBJECT },
    { MID_GET_VERSION, "getVersion", METHOD_TYPE_OBJECT },
    { MID_GET_GAME_CONFIGURATION_VALUE, "getGameConfigurationValue", METHOD_TYPE_OBJECT },
    { MID_OPEN_CONNECTION, "openConnection", METHOD_TYPE_OBJECT },
    { MID_GET_FILES_DIR, "getFilesDir", METHOD_TYPE_OBJECT },
    { MID_GET_ABSOLUTE_PATH, "getAbsolutePath", METHOD_TYPE_OBJECT },
    { MID_IS_READY_INTERSTITIAL, "isReadyInterstitial", METHOD_TYPE_BOOLEAN },
    { MID_IS_READY_INCENTIVIZED, "isReadyIncentivized", METHOD_TYPE_BOOLEAN },
    { MID_WAS_CONFIGURATION_RECEIVED, "wasGameConfigurationReceived", METHOD_TYPE_BOOLEAN },
    { MID_IS_DOLBY_SUPPORTED, "isDolbyAudioProcessingSupported", METHOD_TYPE_BOOLEAN },
    { MID_IS_DOLBY_ENABLED, "isDolbyAudioProcessingEnabled", METHOD_TYPE_BOOLEAN },
    { MID_IS_DEVICE_CONNECTED, "isDeviceConnected", METHOD_TYPE_BOOLEAN },
    { MID_HAS_TOUCH, "hasTouch", METHOD_TYPE_BOOLEAN },
    { MID_IS_CONSUMABLE, "isConsumable", METHOD_TYPE_BOOLEAN },
    { MID_OPEN_FILE, "openFile", METHOD_TYPE_BOOLEAN },
    { MID_SEEK_FILE, "seekFile", METHOD_TYPE_BOOLEAN },
    { MID_READ_FILE, "readFile", METHOD_TYPE_INT },
    { MID_INITIALIZE, "initialize", METHOD_TYPE_VOID },
    { MID_CLOSE_FILE, "closeFile", METHOD_TYPE_VOID },
    { MID_START_DOWNLOAD, "startDownload", METHOD_TYPE_VOID },
    { MID_HANDLE_ERROR, "handleError", METHOD_TYPE_VOID },
    { MID_SHOW_TOAST, "showToast", METHOD_TYPE_VOID },
    { MID_FMOD_CHECK_INIT, "checkInit", METHOD_TYPE_BOOLEAN },
    { MID_FMOD_GET_ASSET_MANAGER, "getAssetManager", METHOD_TYPE_OBJECT },
    { MID_FMOD_GET_OUTPUT_BLOCK_SIZE, "getOutputBlockSize", METHOD_TYPE_INT },
    { MID_FMOD_GET_OUTPUT_SAMPLE_RATE, "getOutputSampleRate", METHOD_TYPE_INT },
    { MID_FMOD_SUPPORTS_LOW_LATENCY, "supportsLowLatency", METHOD_TYPE_BOOLEAN },
    { MID_GENERIC_VOID, "checkForAds", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "showInterstitial", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "showIncentivized", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "logEvent", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "logResourceEvent", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "logProgressionEvent", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "openPrivacyDashboard", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "addItemId", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "startPurchase", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "showWebPage", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "showMoreGames", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "showTwitterPage", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "showFacebookPage", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "showGooglePlusPage", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "rateGame", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "setDolbyAudioProcessingEnabled", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "playVibration", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "startSignIn", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "startSignOut", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "showAchievements", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "submitScore", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "unlockAchievement", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "setRequestProperty", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "setTimeoutMS", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "sendRequest", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "onPause", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "onResume", METHOD_TYPE_VOID },
    { MID_GENERIC_VOID, "onDestroy", METHOD_TYPE_VOID },
};

MethodsBoolean methodsBoolean[] = {
    { MID_IS_READY_INTERSTITIAL, boolean_false },
    { MID_IS_READY_INCENTIVIZED, boolean_false },
    { MID_WAS_CONFIGURATION_RECEIVED, boolean_false },
    { MID_IS_DOLBY_SUPPORTED, boolean_false },
    { MID_IS_DOLBY_ENABLED, boolean_false },
    { MID_IS_DEVICE_CONNECTED, boolean_true },
    { MID_HAS_TOUCH, boolean_true },
    { MID_IS_CONSUMABLE, boolean_false },
    { MID_FMOD_CHECK_INIT, fmod_check_init },
    { MID_FMOD_SUPPORTS_LOW_LATENCY, fmod_supports_low_latency },
    { MID_OPEN_FILE, expansion_open },
    { MID_SEEK_FILE, expansion_seek },
};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {};
MethodsInt methodsInt[] = {
    { MID_GET_ROTATION, integer_zero },
    { MID_READ_FILE, expansion_read },
    { MID_FMOD_GET_OUTPUT_BLOCK_SIZE, fmod_output_block_size },
    { MID_FMOD_GET_OUTPUT_SAMPLE_RATE, fmod_output_sample_rate },
};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {
    { MID_GET_CLASS_LOADER, object_dummy },
    { MID_LOAD_CLASS, object_dummy },
    { MID_GET_SYSTEM_SERVICE, object_dummy },
    { MID_GET_DEFAULT_DISPLAY, object_dummy },
    { MID_GET_DEFAULT, object_dummy },
    { MID_GET_INSTANCE, object_dummy },
    { MID_GET_LANGUAGE, string_language },
    { MID_GET_COUNTRY, string_country },
    { MID_GET_DEVICE_ID, string_device_id },
    { MID_GET_VERSION, string_version },
    { MID_GET_GAME_CONFIGURATION_VALUE, string_empty },
    { MID_OPEN_CONNECTION, object_null },
    { MID_GET_FILES_DIR, object_dummy },
    { MID_GET_ABSOLUTE_PATH, string_data_path },
    { MID_FMOD_GET_ASSET_MANAGER, fmod_asset_manager },
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { MID_INITIALIZE, method_void_stub },
    { MID_CLOSE_FILE, expansion_close },
    { MID_START_DOWNLOAD, method_void_stub },
    { MID_HANDLE_ERROR, method_show_toast },
    { MID_SHOW_TOAST, method_show_toast },
    { MID_GENERIC_VOID, method_void_stub },
};

char WINDOW_SERVICE[] = "window";
const int SDK_INT = 19;

NameToFieldID nameToFieldId[] = {
    { 0, "WINDOW_SERVICE", FIELD_TYPE_OBJECT },
    { 1, "SDK_INT", FIELD_TYPE_INT },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = { { 1, SDK_INT } };
FieldsObject fieldsObject[] = { { 0, WINDOW_SERVICE } };
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
