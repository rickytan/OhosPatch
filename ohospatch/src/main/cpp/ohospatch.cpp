#include "ark_runtime/jsvm.h"
#include "fixit_runtime.h"
#include "hilog/log.h"
#include "napi/native_api.h"
#include "uv.h"

#include <array>
#include <cstdint>
#include <memory>
#include <new>
#include <string>

namespace {

constexpr unsigned int kLogDomain = 0xD003900;
constexpr const char *kLogTag = "OhosPatch";

void LogError(const char *operation, int status)
{
    OH_LOG_Print(LOG_APP, LOG_ERROR, kLogDomain, kLogTag, "%{public}s failed with status %{public}d", operation,
                 status);
}

void LogError(const std::string &message)
{
    OH_LOG_Print(LOG_APP, LOG_ERROR, kLogDomain, kLogTag, "%{public}s", message.c_str());
}

void LogUvError(const char *operation, int status)
{
    OH_LOG_Print(LOG_APP, LOG_ERROR, kLogDomain, kLogTag, "%{public}s failed: %{public}s (%{public}d)", operation,
                 uv_strerror(status), status);
}

void ClearPendingNapiException(napi_env env)
{
    bool pending = false;
    napi_status pendingStatus = napi_is_exception_pending(env, &pending);
    if (pendingStatus != napi_ok) {
        LogError("napi_is_exception_pending", static_cast<int>(pendingStatus));
        return;
    }
    if (!pending) {
        return;
    }
    napi_value exception = nullptr;
    napi_status clearStatus = napi_get_and_clear_last_exception(env, &exception);
    if (clearStatus != napi_ok) {
        LogError("napi_get_and_clear_last_exception", static_cast<int>(clearStatus));
    }
}

bool NapiOk(napi_env env, napi_status status, const char *operation)
{
    if (status == napi_ok) {
        return true;
    }
    LogError(operation, static_cast<int>(status));
    ClearPendingNapiException(env);
    return false;
}

napi_value NapiUndefined(napi_env env)
{
    napi_value value = nullptr;
    if (!NapiOk(env, napi_get_undefined(env, &value), "napi_get_undefined")) {
        return nullptr;
    }
    return value;
}

napi_value NapiUint32(napi_env env, uint32_t number)
{
    napi_value value = nullptr;
    if (!NapiOk(env, napi_create_uint32(env, number, &value), "napi_create_uint32")) {
        return NapiUndefined(env);
    }
    return value;
}

void DeleteNapiReference(napi_env env, napi_ref reference, const char *operation)
{
    if (!env || !reference) {
        return;
    }
    napi_status status = napi_delete_reference(env, reference);
    if (status != napi_ok) {
        LogError(operation, static_cast<int>(status));
    }
}

bool NapiString(napi_env env, napi_value value, std::string *output)
{
    if (!value || !output) {
        LogError("NapiString received an invalid argument");
        return false;
    }
    size_t length = 0;
    if (!NapiOk(env, napi_get_value_string_utf8(env, value, nullptr, 0, &length),
                "napi_get_value_string_utf8(length)")) {
        return false;
    }
    std::string text(length + 1, '\0');
    if (!NapiOk(env, napi_get_value_string_utf8(env, value, text.data(), text.size(), &length),
                "napi_get_value_string_utf8(value)")) {
        return false;
    }
    text.resize(length);
    *output = std::move(text);
    return true;
}

bool NapiNamedString(napi_env env, napi_value object, const char *name, std::string *output)
{
    napi_value value = nullptr;
    return NapiOk(env, napi_get_named_property(env, object, name, &value), "napi_get_named_property(string)") &&
           NapiString(env, value, output);
}

bool NapiNamedUint32(napi_env env, napi_value object, const char *name, uint32_t *output)
{
    napi_value value = nullptr;
    return NapiOk(env, napi_get_named_property(env, object, name, &value), "napi_get_named_property(uint32)") &&
           NapiOk(env, napi_get_value_uint32(env, value, output), "napi_get_value_uint32");
}

bool NapiJsonFunction(napi_env env, const char *name, napi_value *jsonObject, napi_value *function)
{
    napi_value global = nullptr;
    return NapiOk(env, napi_get_global(env, &global), "napi_get_global") &&
           NapiOk(env, napi_get_named_property(env, global, "JSON", jsonObject), "napi_get_named_property(JSON)") &&
           NapiOk(env, napi_get_named_property(env, *jsonObject, name, function),
                  "napi_get_named_property(JSON function)");
}

bool NapiJsonStringify(napi_env env, napi_value value, const char *undefinedFallback, std::string *output)
{
    napi_value jsonObject = nullptr;
    napi_value stringify = nullptr;
    if (!NapiJsonFunction(env, "stringify", &jsonObject, &stringify)) {
        return false;
    }

    napi_value result = nullptr;
    if (!NapiOk(env, napi_call_function(env, jsonObject, stringify, 1, &value, &result),
                "napi_call_function(JSON.stringify)")) {
        return false;
    }
    napi_valuetype type = napi_undefined;
    if (!NapiOk(env, napi_typeof(env, result, &type), "napi_typeof(JSON.stringify result)")) {
        return false;
    }
    if (type != napi_string) {
        *output = undefinedFallback;
        return true;
    }
    return NapiString(env, result, output);
}

bool NapiJsonParse(napi_env env, const std::string &json, napi_value *output)
{
    napi_value jsonObject = nullptr;
    napi_value parse = nullptr;
    if (!NapiJsonFunction(env, "parse", &jsonObject, &parse)) {
        return false;
    }
    napi_value source = nullptr;
    if (!NapiOk(env, napi_create_string_utf8(env, json.c_str(), json.size(), &source),
                "napi_create_string_utf8(JSON)")) {
        return false;
    }
    return NapiOk(env, napi_call_function(env, jsonObject, parse, 1, &source, output),
                  "napi_call_function(JSON.parse)");
}

struct HookRecord {
    napi_env env = nullptr;
    napi_ref holder = nullptr;
    napi_ref original = nullptr;
    std::string className;
    std::string targetKey;
    std::string methodName;
    bool classMethod = false;
};

constexpr size_t kMaxProxyHandles = 256;

struct ActiveInvocation {
    napi_env env = nullptr;
    HookRecord *hook = nullptr;
    napi_value receiver = nullptr;
    bool originalExceptionPending = false;
    std::array<napi_value, kMaxProxyHandles> proxyValues{};
    size_t proxyValueCount = 0;
};

struct ImportedValue {
    napi_ref reference = nullptr;
    napi_valuetype type = napi_undefined;
    uint32_t handle = 0;
};

enum class UiRuleKind {
    PARAM,
    STATE,
    ATTRIBUTE,
    EVENT,
};

enum class UiMethodKind {
    PARAM_INITIAL,
    PARAM_UPDATE,
    INITIAL_RENDER,
    OBSERVE_CREATION,
};

constexpr size_t kMaxUiCaptureProperties = 16;
constexpr size_t kMaxUiNodeTypesPerRender = 64;
constexpr size_t kMaxUiEventsPerNode = 16;

struct UiRule {
    UiRuleKind kind = UiRuleKind::ATTRIBUTE;
    uint32_t ruleId = 0;
    std::string className;
    std::string modulePath;
    std::string moduleInfo;
    std::string exportName;
    std::string targetKey;
    std::string propertyName;
    std::string nodeType;
    uint32_t occurrence = 0;
    std::string attributeName;
    std::string argumentsJson;
    std::string eventName;
    std::array<std::string, kMaxUiCaptureProperties> captureProperties;
    size_t captureCount = 0;
};

struct UiComponentHook;

struct UiMethodHook {
    UiComponentHook *component = nullptr;
    UiMethodKind kind = UiMethodKind::PARAM_INITIAL;
    napi_ref original = nullptr;
    std::string methodName;
};

struct UiComponentHook {
    napi_env env = nullptr;
    napi_ref holder = nullptr;
    std::string className;
    std::string targetKey;
    std::array<UiMethodHook, 4> methods;
    size_t methodCount = 0;
};

struct UiNodeCallbackRecord {
    napi_env env = nullptr;
    napi_ref originalBuilder = nullptr;
    napi_ref componentApi = nullptr;
    napi_ref owner = nullptr;
    std::string targetKey;
    std::string nodeType;
    uint32_t occurrence = 0;
};

struct UiEventCallbackRecord {
    napi_env env = nullptr;
    napi_ref owner = nullptr;
    napi_ref originalEvent = nullptr;
    uint32_t ruleId = 0;
    std::array<std::string, kMaxUiCaptureProperties> captureProperties;
    size_t captureCount = 0;
};

struct UiEventCaptureContext {
    napi_env env = nullptr;
    napi_ref originalRegistrar = nullptr;
    napi_ref originalEvent = nullptr;
    UiRule *rule = nullptr;
    bool installed = false;
};

struct UiNodeTypeCounter {
    std::string nodeType;
    uint32_t count = 0;
};

struct UiRenderFrame {
    std::string targetKey;
    napi_value owner = nullptr;
    std::array<UiNodeTypeCounter, kMaxUiNodeTypesPerRender> counters;
    size_t counterCount = 0;
};

class JsvmRuntime;

struct TimerRecord {
    uv_timer_t handle{};
    JsvmRuntime *runtime = nullptr;
    uint32_t id = 0;
    bool repeating = false;
};

JsvmRuntime &Runtime();

class JsvmRuntime
{
  public:
    bool Ensure()
    {
        if (ready_) {
            return true;
        }

        if (!initialized_) {
            JSVM_InitOptions initOptions{};
            if (!JsvmOk(OH_JSVM_Init(&initOptions), "OH_JSVM_Init", nullptr)) {
                return false;
            }
            initialized_ = true;
        }

        JSVM_CreateVMOptions vmOptions{};
        if (!JsvmOk(OH_JSVM_CreateVM(&vmOptions, &vm_), "OH_JSVM_CreateVM", nullptr) ||
            !JsvmOk(OH_JSVM_OpenVMScope(vm_, &vmScope_), "OH_JSVM_OpenVMScope", nullptr) ||
            !JsvmOk(OH_JSVM_CreateEnv(vm_, 0, nullptr, &env_), "OH_JSVM_CreateEnv", nullptr) ||
            !JsvmOk(OH_JSVM_OpenEnvScope(env_, &envScope_), "OH_JSVM_OpenEnvScope", env_) ||
            !InstallNativeFunctionsWithScope() || !Run(kFixitRuntimeScript)) {
            ResetVm();
            return false;
        }

        ready_ = true;
        return true;
    }

    size_t ExecuteAndInstall(napi_env napiEnv, const std::string &script)
    {
        if (!Ensure() || !BindEventLoop(napiEnv) || !Clear(napiEnv)) {
            return 0;
        }
        if (!Run(script)) {
            ClearRegistry();
            ClearImportedValues();
            return 0;
        }
        JSVM_HandleScope scope = nullptr;
        if (!JsvmOk(OH_JSVM_OpenHandleScope(env_, &scope), "OH_JSVM_OpenHandleScope(patch install)", env_)) {
            ClearRegistry();
            ClearImportedValues();
            return 0;
        }
        bool hooksInstalled = InstallHooks(napiEnv);
        bool uiInstalled = hooksInstalled && InstallUiRules(napiEnv);
        bool scopeClosed = JsvmOk(OH_JSVM_CloseHandleScope(env_, scope),
                                  "OH_JSVM_CloseHandleScope(patch install)", env_);
        if (!hooksInstalled || !uiInstalled || !scopeClosed) {
            RestoreUiHooks();
            ClearUiRules();
            RestoreHooks();
            ClearRegistry();
            ClearImportedValues();
            return 0;
        }
        return hookCount_ + uiRuleCount_;
    }

    bool Clear(napi_env napiEnv)
    {
        if (!ready_) {
            return true;
        }
        if ((hookCount_ > 0 && hooks_[0]->env != napiEnv) ||
            (uiComponentHookCount_ > 0 && uiComponentHooks_[0]->env != napiEnv) ||
            ((HasTimers() || importedValueCount_ > 0) && hostEnv_ != napiEnv)) {
            LogError("OhosPatch must be cleared on the ArkTS VM where it was installed");
            return false;
        }
        bool uiRestored = RestoreUiHooks();
        ClearUiRules();
        bool restored = RestoreHooks();
        bool cleared = ClearRegistry();
        bool importsCleared = ClearImportedValues();
        return uiRestored && restored && cleared && importsCleared;
    }

    napi_value InvokeHook(napi_env napiEnv, HookRecord *hook, napi_value receiver, size_t argc, const napi_value *argv)
    {
        auto callOriginal = [&]() -> napi_value {
            napi_value result = nullptr;
            bool exceptionPending = false;
            if (CallOriginal(napiEnv, hook, receiver, argc, argv, &result, &exceptionPending)) {
                return result;
            }
            return exceptionPending ? nullptr : NapiUndefined(napiEnv);
        };

        if (!Ensure() || !hook) {
            LogError("Cannot invoke an unavailable OhosPatch hook");
            return hook ? callOriginal() : NapiUndefined(napiEnv);
        }

        napi_value argsArray = nullptr;
        if (!NapiOk(napiEnv, napi_create_array_with_length(napiEnv, argc, &argsArray),
                    "napi_create_array_with_length")) {
            return callOriginal();
        }
        for (size_t index = 0; index < argc; ++index) {
            if (!NapiOk(napiEnv, napi_set_element(napiEnv, argsArray, static_cast<uint32_t>(index), argv[index]),
                        "napi_set_element(arguments)")) {
                return callOriginal();
            }
        }

        std::string argsJson;
        if (!NapiJsonStringify(napiEnv, argsArray, "[]", &argsJson)) {
            return callOriginal();
        }

        JSVM_HandleScope scope = nullptr;
        if (!JsvmOk(OH_JSVM_OpenHandleScope(env_, &scope), "OH_JSVM_OpenHandleScope(method patch)", env_)) {
            return callOriginal();
        }
        auto closeScope = [&]() {
            return JsvmOk(OH_JSVM_CloseHandleScope(env_, scope), "OH_JSVM_CloseHandleScope(method patch)", env_);
        };

        JSVM_Value targetKeyValue = nullptr;
        JSVM_Value methodNameValue = nullptr;
        JSVM_Value classMethodValue = nullptr;
        JSVM_Value targetHandleValue = nullptr;
        JSVM_Value argsValue = nullptr;
        if (!String(hook->targetKey, &targetKeyValue) || !String(hook->methodName, &methodNameValue) ||
            !Bool(hook->classMethod, &classMethodValue) ||
            !JsvmOk(OH_JSVM_CreateUint32(env_, 0, &targetHandleValue), "OH_JSVM_CreateUint32(proxy root)", env_) ||
            !ParseJson(argsJson, &argsValue)) {
            closeScope();
            return callOriginal();
        }

        JSVM_Value patchArgs[] = {targetKeyValue, methodNameValue, classMethodValue, targetHandleValue, argsValue};
        ActiveInvocation previous = activeInvocation_;
        activeInvocation_ = {};
        activeInvocation_.env = napiEnv;
        activeInvocation_.hook = hook;
        activeInvocation_.receiver = receiver;
        activeInvocation_.proxyValues[0] = receiver;
        activeInvocation_.proxyValueCount = 1;
        auto restoreInvocation = [&]() {
            activeInvocation_ = previous;
        };
        JSVM_Value patchResult = nullptr;
        bool called = CallGlobal("__ohospatch_callPatch", patchArgs, std::size(patchArgs), &patchResult);
        bool originalExceptionPending = activeInvocation_.originalExceptionPending;
        if (originalExceptionPending) {
            restoreInvocation();
            closeScope();
            return nullptr;
        }
        if (!called) {
            restoreInvocation();
            closeScope();
            return callOriginal();
        }

        std::string resultJson;
        bool stringified = StringifyJson(patchResult, &resultJson);
        bool scopeClosed = closeScope();
        if (!stringified || !scopeClosed) {
            restoreInvocation();
            return callOriginal();
        }
        napi_value envelope = nullptr;
        if (!NapiJsonParse(napiEnv, resultJson, &envelope)) {
            restoreInvocation();
            return callOriginal();
        }

        napi_value handledValue = nullptr;
        bool handled = false;
        if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, envelope, "handled", &handledValue),
                    "napi_get_named_property(handled)") ||
            !NapiOk(napiEnv, napi_get_value_bool(napiEnv, handledValue, &handled), "napi_get_value_bool(handled)")) {
            restoreInvocation();
            return callOriginal();
        }
        if (!handled) {
            restoreInvocation();
            return callOriginal();
        }

        bool hasResult = false;
        if (!NapiOk(napiEnv, napi_has_named_property(napiEnv, envelope, "result", &hasResult),
                    "napi_has_named_property(result)")) {
            restoreInvocation();
            return callOriginal();
        }
        if (!hasResult) {
            restoreInvocation();
            return NapiUndefined(napiEnv);
        }

        napi_value resultWire = nullptr;
        std::string resultKind;
        if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, envelope, "result", &resultWire),
                    "napi_get_named_property(result wire)") ||
            !NapiNamedString(napiEnv, resultWire, "kind", &resultKind)) {
            restoreInvocation();
            return callOriginal();
        }
        if (resultKind == "undefined") {
            restoreInvocation();
            return NapiUndefined(napiEnv);
        }
        if (resultKind == "remote") {
            uint32_t handle = 0;
            if (!NapiNamedUint32(napiEnv, resultWire, "handle", &handle) ||
                handle >= activeInvocation_.proxyValueCount) {
                LogError("OhosPatch patch returned an invalid proxy handle");
                restoreInvocation();
                return callOriginal();
            }
            napi_value result = activeInvocation_.proxyValues[handle];
            restoreInvocation();
            return result;
        }
        if (resultKind == "imported") {
            uint32_t handle = 0;
            napi_value result = nullptr;
            if (!NapiNamedUint32(napiEnv, resultWire, "handle", &handle) ||
                !GetImportedValue(handle, &result)) {
                LogError("OhosPatch patch returned an invalid imported handle");
                restoreInvocation();
                return callOriginal();
            }
            restoreInvocation();
            return result;
        }
        if (resultKind != "wire") {
            LogError("OhosPatch patch returned an unknown result kind");
            restoreInvocation();
            return callOriginal();
        }

        napi_value encodedResult = nullptr;
        napi_value result = nullptr;
        if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, resultWire, "value", &encodedResult),
                    "napi_get_named_property(result value)") ||
            !ResolveBridgeWireValue(napiEnv, encodedResult, 0, &result)) {
            restoreInvocation();
            return callOriginal();
        }
        restoreInvocation();
        return result;
    }

  private:
    static constexpr size_t kMaxArguments = 64;
    static constexpr size_t kMaxHooks = 256;
    static constexpr size_t kMaxTimers = 256;
    static constexpr size_t kMaxImportedValues = 512;
    static constexpr size_t kMaxUiRules = 256;
    static constexpr size_t kMaxUiComponentHooks = 64;
    static constexpr size_t kMaxUiRenderDepth = 16;

    JSVM_VM vm_ = nullptr;
    JSVM_VMScope vmScope_ = nullptr;
    JSVM_Env env_ = nullptr;
    JSVM_EnvScope envScope_ = nullptr;
    bool initialized_ = false;
    bool ready_ = false;
    napi_env hostEnv_ = nullptr;
    uv_loop_t *eventLoop_ = nullptr;
    ActiveInvocation activeInvocation_;
    std::array<std::unique_ptr<HookRecord>, kMaxHooks> hooks_;
    size_t hookCount_ = 0;
    std::array<TimerRecord *, kMaxTimers> timers_{};
    std::array<ImportedValue, kMaxImportedValues> importedValues_{};
    size_t importedValueCount_ = 0;
    uint32_t nextImportedHandle_ = 1;
    std::array<std::unique_ptr<UiRule>, kMaxUiRules> uiRules_;
    size_t uiRuleCount_ = 0;
    std::array<std::unique_ptr<UiComponentHook>, kMaxUiComponentHooks> uiComponentHooks_;
    size_t uiComponentHookCount_ = 0;
    std::array<UiRenderFrame, kMaxUiRenderDepth> uiRenderFrames_;
    size_t uiRenderDepth_ = 0;

    bool BindEventLoop(napi_env napiEnv)
    {
        if (hostEnv_ == napiEnv && eventLoop_) {
            return true;
        }
        if (hostEnv_ && hostEnv_ != napiEnv && (hookCount_ > 0 || HasTimers() || importedValueCount_ > 0)) {
            LogError("OhosPatch cannot switch event loops while hooks or timers are active");
            return false;
        }

        uv_loop_t *loop = nullptr;
        if (!NapiOk(napiEnv, napi_get_uv_event_loop(napiEnv, &loop), "napi_get_uv_event_loop") || !loop) {
            LogError("OhosPatch could not obtain the host event loop");
            return false;
        }
        hostEnv_ = napiEnv;
        eventLoop_ = loop;
        return true;
    }

    bool HasTimers() const
    {
        for (TimerRecord *timer : timers_) {
            if (timer) {
                return true;
            }
        }
        return false;
    }

    static bool JsvmOk(JSVM_Status status, const char *operation, JSVM_Env env)
    {
        if (status == JSVM_OK) {
            return true;
        }
        LogError(operation, static_cast<int>(status));
        if (env) {
            bool pending = false;
            JSVM_Status pendingStatus = OH_JSVM_IsExceptionPending(env, &pending);
            if (pendingStatus != JSVM_OK) {
                LogError("OH_JSVM_IsExceptionPending", static_cast<int>(pendingStatus));
            } else if (pending) {
                JSVM_Value exception = nullptr;
                JSVM_Status clearStatus = OH_JSVM_GetAndClearLastException(env, &exception);
                if (clearStatus != JSVM_OK) {
                    LogError("OH_JSVM_GetAndClearLastException", static_cast<int>(clearStatus));
                }
            }
        }
        return false;
    }

    void ResetVm()
    {
        CancelAllTimers();
        if (env_ && envScope_) {
            JsvmOk(OH_JSVM_CloseEnvScope(env_, envScope_), "OH_JSVM_CloseEnvScope", env_);
        }
        envScope_ = nullptr;
        if (env_) {
            JsvmOk(OH_JSVM_DestroyEnv(env_), "OH_JSVM_DestroyEnv", nullptr);
        }
        env_ = nullptr;
        if (vm_ && vmScope_) {
            JsvmOk(OH_JSVM_CloseVMScope(vm_, vmScope_), "OH_JSVM_CloseVMScope", nullptr);
        }
        vmScope_ = nullptr;
        if (vm_) {
            JsvmOk(OH_JSVM_DestroyVM(vm_), "OH_JSVM_DestroyVM", nullptr);
        }
        vm_ = nullptr;
        hostEnv_ = nullptr;
        eventLoop_ = nullptr;
        ready_ = false;
    }

    static JsvmRuntime *Current(JSVM_Env env)
    {
        void *data = nullptr;
        if (!JsvmOk(OH_JSVM_GetInstanceData(env, &data), "OH_JSVM_GetInstanceData", env)) {
            return nullptr;
        }
        return static_cast<JsvmRuntime *>(data);
    }

    static void CloseTimerCallback(uv_handle_t *handle)
    {
        if (!handle) {
            return;
        }
        delete static_cast<TimerRecord *>(handle->data);
    }

    static void TimerCallback(uv_timer_t *handle)
    {
        if (!handle || !handle->data) {
            LogError("OhosPatch received an invalid timer callback");
            return;
        }
        TimerRecord *timer = static_cast<TimerRecord *>(handle->data);
        JsvmRuntime *runtime = timer->runtime;
        uint32_t id = timer->id;
        bool oneShot = !timer->repeating;
        if (runtime) {
            runtime->FireTimer(id);
            if (oneShot) {
                runtime->CancelTimer(id);
            }
        }
    }

    bool ScheduleTimer(uint32_t id, uint32_t delay, bool repeating)
    {
        if (!eventLoop_) {
            LogError("OhosPatch timer requested without a host event loop");
            return false;
        }

        size_t slot = kMaxTimers;
        for (size_t index = 0; index < kMaxTimers; ++index) {
            if (timers_[index] && timers_[index]->id == id) {
                LogError("OhosPatch timer ID is already active");
                return false;
            }
            if (!timers_[index] && slot == kMaxTimers) {
                slot = index;
            }
        }
        if (slot == kMaxTimers) {
            LogError("OhosPatch native timer limit reached");
            return false;
        }

        TimerRecord *timer = new (std::nothrow) TimerRecord();
        if (!timer) {
            LogError("Cannot allocate OhosPatch TimerRecord");
            return false;
        }
        timer->runtime = this;
        timer->id = id;
        timer->repeating = repeating;
        timer->handle.data = timer;

        int status = uv_timer_init(eventLoop_, &timer->handle);
        if (status != 0) {
            LogUvError("uv_timer_init", status);
            delete timer;
            return false;
        }
        timers_[slot] = timer;
        uint64_t repeat = repeating ? static_cast<uint64_t>(delay) : 0;
        status = uv_timer_start(&timer->handle, TimerCallback, delay, repeat);
        if (status != 0) {
            LogUvError("uv_timer_start", status);
            timers_[slot] = nullptr;
            uv_close(reinterpret_cast<uv_handle_t *>(&timer->handle), CloseTimerCallback);
            return false;
        }
        uv_unref(reinterpret_cast<uv_handle_t *>(&timer->handle));
        return true;
    }

    bool CancelTimer(uint32_t id)
    {
        for (size_t index = 0; index < kMaxTimers; ++index) {
            TimerRecord *timer = timers_[index];
            if (!timer || timer->id != id) {
                continue;
            }
            timers_[index] = nullptr;
            int status = uv_timer_stop(&timer->handle);
            if (status != 0) {
                LogUvError("uv_timer_stop", status);
            }
            uv_handle_t *handle = reinterpret_cast<uv_handle_t *>(&timer->handle);
            if (!uv_is_closing(handle)) {
                uv_close(handle, CloseTimerCallback);
            }
            return status == 0;
        }
        return true;
    }

    bool CancelAllTimers()
    {
        bool success = true;
        for (size_t index = 0; index < kMaxTimers; ++index) {
            if (timers_[index] && !CancelTimer(timers_[index]->id)) {
                success = false;
            }
        }
        return success;
    }

    bool FireTimer(uint32_t id)
    {
        if (!ready_ || !hostEnv_) {
            LogError("OhosPatch timer fired after JSVM shutdown");
            return false;
        }

        napi_handle_scope napiScope = nullptr;
        if (!NapiOk(hostEnv_, napi_open_handle_scope(hostEnv_, &napiScope),
                    "napi_open_handle_scope(timer)")) {
            return false;
        }
        JSVM_HandleScope scope = nullptr;
        if (!JsvmOk(OH_JSVM_OpenHandleScope(env_, &scope), "OH_JSVM_OpenHandleScope(timer)", env_)) {
            NapiOk(hostEnv_, napi_close_handle_scope(hostEnv_, napiScope), "napi_close_handle_scope(timer)");
            return false;
        }

        JSVM_Value timerId = nullptr;
        JSVM_Value ignored = nullptr;
        bool success = JsvmOk(OH_JSVM_CreateUint32(env_, id, &timerId), "OH_JSVM_CreateUint32(timer ID)", env_) &&
                       CallGlobal("__ohospatch_fireTimer", &timerId, 1, &ignored);
        if (!JsvmOk(OH_JSVM_PerformMicrotaskCheckpoint(vm_), "OH_JSVM_PerformMicrotaskCheckpoint", env_)) {
            success = false;
        }
        if (!JsvmOk(OH_JSVM_CloseHandleScope(env_, scope), "OH_JSVM_CloseHandleScope(timer)", env_)) {
            success = false;
        }
        if (!NapiOk(hostEnv_, napi_close_handle_scope(hostEnv_, napiScope), "napi_close_handle_scope(timer)")) {
            success = false;
        }
        return success;
    }

    static napi_value UiMethodCallback(napi_env env, napi_callback_info info)
    {
        size_t argc = kMaxArguments;
        napi_value receiver = nullptr;
        void *data = nullptr;
        std::array<napi_value, kMaxArguments> argv{};
        if (!NapiOk(env, napi_get_cb_info(env, info, &argc, argv.data(), &receiver, &data),
                    "napi_get_cb_info(component method)")) {
            return NapiUndefined(env);
        }
        if (!data) {
            LogError("OhosPatch component method callback has no UiMethodHook");
            return NapiUndefined(env);
        }
        if (argc > kMaxArguments) {
            LogError("Component method arguments were truncated to the OhosPatch limit");
            argc = kMaxArguments;
        }
        return Runtime().InvokeUiMethod(env, static_cast<UiMethodHook *>(data), receiver, argc, argv.data());
    }

    static napi_value UiNodeBuilderCallback(napi_env env, napi_callback_info info)
    {
        size_t argc = kMaxArguments;
        napi_value receiver = nullptr;
        void *data = nullptr;
        std::array<napi_value, kMaxArguments> argv{};
        if (!NapiOk(env, napi_get_cb_info(env, info, &argc, argv.data(), &receiver, &data),
                    "napi_get_cb_info(component node builder)")) {
            return NapiUndefined(env);
        }
        if (!data) {
            LogError("OhosPatch component node callback has no UiNodeCallbackRecord");
            return NapiUndefined(env);
        }
        if (argc > kMaxArguments) {
            LogError("Component node builder arguments were truncated to the OhosPatch limit");
            argc = kMaxArguments;
        }
        return Runtime().InvokeUiNodeBuilder(env, static_cast<UiNodeCallbackRecord *>(data), receiver, argc,
                                             argv.data());
    }

    static napi_value UiEventCaptureCallback(napi_env env, napi_callback_info info)
    {
        size_t argc = kMaxArguments;
        napi_value receiver = nullptr;
        void *data = nullptr;
        std::array<napi_value, kMaxArguments> argv{};
        if (!NapiOk(env, napi_get_cb_info(env, info, &argc, argv.data(), &receiver, &data),
                    "napi_get_cb_info(component event capture)")) {
            return NapiUndefined(env);
        }
        UiEventCaptureContext *capture = static_cast<UiEventCaptureContext *>(data);
        if (!capture) {
            LogError("OhosPatch component event capture has no context");
            return NapiUndefined(env);
        }
        if (argc > kMaxArguments) {
            LogError("Component event registration arguments were truncated to the OhosPatch limit");
            argc = kMaxArguments;
        }

        if (argc > 0) {
            DeleteNapiReference(env, capture->originalEvent, "napi_delete_reference(previous component event)");
            capture->originalEvent = nullptr;
            if (!NapiOk(env, napi_create_reference(env, argv[0], 1, &capture->originalEvent),
                        "napi_create_reference(original component event)")) {
                capture->originalEvent = nullptr;
            }
        }

        napi_value registrar = nullptr;
        if (!NapiOk(env, napi_get_reference_value(env, capture->originalRegistrar, &registrar),
                    "napi_get_reference_value(component event registrar)")) {
            return NapiUndefined(env);
        }
        napi_value result = nullptr;
        napi_status status = napi_call_function(env, receiver, registrar, argc, argv.data(), &result);
        if (status == napi_pending_exception) {
            LogError("Original component event registrar threw an exception");
            return nullptr;
        }
        if (!NapiOk(env, status, "napi_call_function(component event registrar)")) {
            return NapiUndefined(env);
        }
        return result;
    }

    static napi_value UiEventCallback(napi_env env, napi_callback_info info)
    {
        size_t argc = kMaxArguments;
        napi_value receiver = nullptr;
        void *data = nullptr;
        std::array<napi_value, kMaxArguments> argv{};
        if (!NapiOk(env, napi_get_cb_info(env, info, &argc, argv.data(), &receiver, &data),
                    "napi_get_cb_info(component event)")) {
            return NapiUndefined(env);
        }
        if (!data) {
            LogError("OhosPatch component event callback has no UiEventCallbackRecord");
            return NapiUndefined(env);
        }
        if (argc > kMaxArguments) {
            LogError("Component event arguments were truncated to the OhosPatch limit");
            argc = kMaxArguments;
        }
        return Runtime().InvokeUiEvent(env, static_cast<UiEventCallbackRecord *>(data), receiver, argc, argv.data());
    }

    static void FinalizeUiNodeCallback(napi_env env, void *data, void *)
    {
        UiNodeCallbackRecord *record = static_cast<UiNodeCallbackRecord *>(data);
        if (!record) {
            return;
        }
        DeleteNapiReference(env, record->originalBuilder, "napi_delete_reference(component node builder)");
        DeleteNapiReference(env, record->componentApi, "napi_delete_reference(component API)");
        DeleteNapiReference(env, record->owner, "napi_delete_reference(component owner)");
        delete record;
    }

    static void FinalizeUiEventCallback(napi_env env, void *data, void *)
    {
        UiEventCallbackRecord *record = static_cast<UiEventCallbackRecord *>(data);
        if (!record) {
            return;
        }
        DeleteNapiReference(env, record->owner, "napi_delete_reference(component event owner)");
        DeleteNapiReference(env, record->originalEvent, "napi_delete_reference(original component event)");
        delete record;
    }

    static napi_value CallUiOriginal(napi_env env, UiMethodHook *method, napi_value receiver, size_t argc,
                                     const napi_value *argv)
    {
        if (!method || !method->original) {
            LogError("CallUiOriginal received an invalid method hook");
            return NapiUndefined(env);
        }
        napi_value original = nullptr;
        if (!NapiOk(env, napi_get_reference_value(env, method->original, &original),
                    "napi_get_reference_value(component original)")) {
            return NapiUndefined(env);
        }
        napi_value result = nullptr;
        napi_status status = napi_call_function(env, receiver, original, argc, argv, &result);
        if (status == napi_pending_exception) {
            LogError("Original ArkUI component method threw an exception");
            return nullptr;
        }
        if (!NapiOk(env, status, "napi_call_function(component original)")) {
            return NapiUndefined(env);
        }
        return result;
    }

    napi_value InvokeUiMethod(napi_env napiEnv, UiMethodHook *method, napi_value receiver, size_t argc,
                              napi_value *argv)
    {
        if (!method || !method->component) {
            LogError("Cannot invoke an unavailable component method hook");
            return NapiUndefined(napiEnv);
        }

        if ((method->kind == UiMethodKind::PARAM_INITIAL || method->kind == UiMethodKind::PARAM_UPDATE) && argc > 0) {
            ApplyUiValueRules(napiEnv, method->component->targetKey, UiRuleKind::PARAM, argv[0]);
        }

        bool pushedFrame = false;
        if (method->kind == UiMethodKind::INITIAL_RENDER) {
            ApplyUiValueRules(napiEnv, method->component->targetKey, UiRuleKind::STATE, receiver);
            pushedFrame = PushUiRenderFrame(method->component->targetKey, receiver);
        } else if (method->kind == UiMethodKind::OBSERVE_CREATION) {
            WrapUiNodeBuilder(napiEnv, method->component->targetKey, argc, argv);
        }

        napi_value result = CallUiOriginal(napiEnv, method, receiver, argc, argv);
        if (result &&
            (method->kind == UiMethodKind::PARAM_INITIAL || method->kind == UiMethodKind::PARAM_UPDATE) && argc > 0) {
            ApplyUiParamValuesToOwner(napiEnv, method->component->targetKey, argv[0], receiver);
        }
        if (pushedFrame) {
            PopUiRenderFrame();
        }
        return result;
    }

    napi_value InvokeUiNodeBuilder(napi_env napiEnv, UiNodeCallbackRecord *record, napi_value receiver, size_t argc,
                                   napi_value *argv)
    {
        if (!record) {
            return NapiUndefined(napiEnv);
        }
        napi_value builder = nullptr;
        napi_value componentApi = nullptr;
        if (!NapiOk(napiEnv, napi_get_reference_value(napiEnv, record->originalBuilder, &builder),
                    "napi_get_reference_value(component node builder)") ||
            !NapiOk(napiEnv, napi_get_reference_value(napiEnv, record->componentApi, &componentApi),
                    "napi_get_reference_value(component API)")) {
            return NapiUndefined(napiEnv);
        }

        std::array<UiEventCaptureContext, kMaxUiEventsPerNode> captures{};
        size_t captureCount = PrepareUiEventCaptures(napiEnv, record, componentApi, captures.data(), captures.size());

        napi_value result = nullptr;
        napi_status status = napi_call_function(napiEnv, receiver, builder, argc, argv, &result);
        RestoreUiEventCaptures(napiEnv, componentApi, captures.data(), captureCount);
        if (status == napi_pending_exception) {
            LogError("Original ArkUI node builder threw an exception");
            ReleaseUiEventCaptures(napiEnv, captures.data(), captureCount);
            return nullptr;
        }
        if (!NapiOk(napiEnv, status, "napi_call_function(component node builder)")) {
            ReleaseUiEventCaptures(napiEnv, captures.data(), captureCount);
            return NapiUndefined(napiEnv);
        }

        ApplyUiAttributes(napiEnv, record, componentApi);
        InstallUiEventCallbacks(napiEnv, record, componentApi, captures.data(), captureCount);
        ReleaseUiEventCaptures(napiEnv, captures.data(), captureCount);
        return result;
    }

    napi_value InvokeUiEvent(napi_env napiEnv, UiEventCallbackRecord *record, napi_value receiver, size_t argc,
                             napi_value *argv)
    {
        if (!record) {
            return NapiUndefined(napiEnv);
        }

        napi_value event = argc > 0 ? argv[0] : NapiUndefined(napiEnv);
        napi_value owner = nullptr;
        NapiOk(napiEnv, napi_get_reference_value(napiEnv, record->owner, &owner),
               "napi_get_reference_value(component event owner)");

        napi_value state = nullptr;
        if (!NapiOk(napiEnv, napi_create_object(napiEnv, &state), "napi_create_object(component state)")) {
            return CallOriginalUiEvent(napiEnv, record, receiver, argc, argv);
        }
        if (owner) {
            for (size_t index = 0; index < record->captureCount; ++index) {
                napi_value value = nullptr;
                const std::string &name = record->captureProperties[index];
                if (NapiOk(napiEnv, napi_get_named_property(napiEnv, owner, name.c_str(), &value),
                           "napi_get_named_property(captured component state)")) {
                    NapiOk(napiEnv, napi_set_named_property(napiEnv, state, name.c_str(), value),
                           "napi_set_named_property(captured component state)");
                }
            }
        }

        napi_value envelope = nullptr;
        bool handled = false;
        if (!CallUiEventHandler(napiEnv, record->ruleId, event, state, owner, &envelope, &handled) || !handled) {
            return CallOriginalUiEvent(napiEnv, record, receiver, argc, argv);
        }

        if (owner) {
            bool hasStatePatch = false;
            if (NapiOk(napiEnv, napi_has_named_property(napiEnv, envelope, "statePatch", &hasStatePatch),
                       "napi_has_named_property(component state patch)") &&
                hasStatePatch) {
                napi_value statePatch = nullptr;
                if (NapiOk(napiEnv, napi_get_named_property(napiEnv, envelope, "statePatch", &statePatch),
                           "napi_get_named_property(component state patch)")) {
                    ApplyTargetPatch(napiEnv, owner, statePatch);
                }
            }
        }

        bool hasResult = false;
        if (!NapiOk(napiEnv, napi_has_named_property(napiEnv, envelope, "result", &hasResult),
                    "napi_has_named_property(component event result)") ||
            !hasResult) {
            return NapiUndefined(napiEnv);
        }
        napi_value result = nullptr;
        if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, envelope, "result", &result),
                    "napi_get_named_property(component event result)")) {
            return NapiUndefined(napiEnv);
        }
        return result;
    }

    JSVM_Value ProxyResponse(const char *kind, JSVM_Value payload)
    {
        JSVM_Value response = nullptr;
        JSVM_Value kindValue = nullptr;
        if (!JsvmOk(OH_JSVM_CreateArrayWithLength(env_, 2, &response), "OH_JSVM_CreateArrayWithLength(proxy response)",
                    env_) ||
            !String(kind, &kindValue) ||
            !JsvmOk(OH_JSVM_SetElement(env_, response, 0, kindValue), "OH_JSVM_SetElement(proxy response kind)",
                    env_)) {
            return Undefined(env_);
        }
        JSVM_Value responsePayload = payload ? payload : Undefined(env_);
        if (!responsePayload ||
            !JsvmOk(OH_JSVM_SetElement(env_, response, 1, responsePayload),
                    "OH_JSVM_SetElement(proxy response payload)", env_)) {
            return Undefined(env_);
        }
        return response;
    }

    JSVM_Value ProxyError(const char *message)
    {
        LogError(message);
        JSVM_Value payload = nullptr;
        if (!String(message, &payload)) {
            return Undefined(env_);
        }
        return ProxyResponse("error", payload);
    }

    bool FindOrAddProxyValue(ActiveInvocation &active, napi_value value, uint32_t *handle)
    {
        if (!value || !handle) {
            LogError("FindOrAddProxyValue received an invalid argument");
            return false;
        }
        for (size_t index = 0; index < active.proxyValueCount; ++index) {
            bool equal = false;
            if (!NapiOk(active.env, napi_strict_equals(active.env, active.proxyValues[index], value, &equal),
                        "napi_strict_equals(proxy value)")) {
                return false;
            }
            if (equal) {
                *handle = static_cast<uint32_t>(index);
                return true;
            }
        }
        if (active.proxyValueCount >= kMaxProxyHandles) {
            LogError("OhosPatch proxy handle limit exceeded");
            return false;
        }
        *handle = static_cast<uint32_t>(active.proxyValueCount);
        active.proxyValues[active.proxyValueCount++] = value;
        return true;
    }

    JSVM_Value ProxyNapiValue(ActiveInvocation &active, napi_value value)
    {
        napi_valuetype type = napi_undefined;
        if (!NapiOk(active.env, napi_typeof(active.env, value, &type), "napi_typeof(proxy value)")) {
            return ProxyError("OhosPatch could not inspect a proxied property");
        }
        if (type == napi_undefined) {
            return ProxyResponse("undefined", nullptr);
        }
        if (type == napi_object || type == napi_function) {
            uint32_t handle = 0;
            if (!FindOrAddProxyValue(active, value, &handle)) {
                return ProxyError("OhosPatch could not retain a proxied property");
            }
            JSVM_Value handleValue = nullptr;
            if (!JsvmOk(OH_JSVM_CreateUint32(env_, handle, &handleValue), "OH_JSVM_CreateUint32(proxy handle)", env_)) {
                return ProxyError("OhosPatch could not create a proxy handle");
            }
            return ProxyResponse(type == napi_function ? "function" : "object", handleValue);
        }
        if (type == napi_symbol || type == napi_external || type == napi_bigint) {
            return ProxyError("OhosPatch does not support this proxied value type");
        }

        std::string json;
        JSVM_Value payload = nullptr;
        if (!NapiJsonStringify(active.env, value, "null", &json) || !ParseJson(json, &payload)) {
            return ProxyError("OhosPatch could not serialize a proxied value");
        }
        return ProxyResponse("value", payload);
    }

    bool GetImportedValue(uint32_t handle, napi_value *output)
    {
        if (!output || !hostEnv_ || handle == 0) {
            LogError("OhosPatch received an invalid imported value handle");
            return false;
        }
        for (size_t index = 0; index < importedValueCount_; ++index) {
            ImportedValue &record = importedValues_[index];
            if (record.handle == handle && record.reference) {
                return NapiOk(hostEnv_, napi_get_reference_value(hostEnv_, record.reference, output),
                              "napi_get_reference_value(imported value)");
            }
        }
        LogError("OhosPatch imported value handle is stale or unavailable");
        return false;
    }

    uint32_t AllocateImportedHandle()
    {
        uint32_t start = nextImportedHandle_;
        do {
            uint32_t candidate = nextImportedHandle_;
            nextImportedHandle_ = nextImportedHandle_ == UINT32_MAX ? 1 : nextImportedHandle_ + 1;
            bool used = false;
            for (size_t index = 0; index < importedValueCount_; ++index) {
                if (importedValues_[index].handle == candidate) {
                    used = true;
                    break;
                }
            }
            if (!used) {
                return candidate;
            }
        } while (nextImportedHandle_ != start);
        LogError("OhosPatch could not allocate an imported value handle");
        return 0;
    }

    bool FindOrAddImportedValue(napi_value value, napi_valuetype type, uint32_t *handle)
    {
        if (!hostEnv_ || !value || !handle) {
            LogError("FindOrAddImportedValue received an invalid argument");
            return false;
        }
        for (size_t index = 0; index < importedValueCount_; ++index) {
            napi_value retained = nullptr;
            bool equal = false;
            ImportedValue &record = importedValues_[index];
            if (!NapiOk(hostEnv_, napi_get_reference_value(hostEnv_, record.reference, &retained),
                        "napi_get_reference_value(imported value for identity)") ||
                !NapiOk(hostEnv_, napi_strict_equals(hostEnv_, retained, value, &equal),
                        "napi_strict_equals(imported value)")) {
                return false;
            }
            if (equal) {
                *handle = record.handle;
                return true;
            }
        }
        if (importedValueCount_ >= kMaxImportedValues) {
            LogError("OhosPatch imported value handle limit exceeded");
            return false;
        }

        ImportedValue &record = importedValues_[importedValueCount_];
        record.handle = AllocateImportedHandle();
        if (record.handle == 0) {
            return false;
        }
        if (!NapiOk(hostEnv_, napi_create_reference(hostEnv_, value, 1, &record.reference),
                    "napi_create_reference(imported value)")) {
            record = {};
            return false;
        }
        record.type = type;
        *handle = record.handle;
        ++importedValueCount_;
        return true;
    }

    JSVM_Value ImportedNapiValue(napi_value value)
    {
        if (!hostEnv_ || !value) {
            return ProxyError("OhosPatch could not access an imported value");
        }
        napi_valuetype type = napi_undefined;
        if (!NapiOk(hostEnv_, napi_typeof(hostEnv_, value, &type), "napi_typeof(imported value)")) {
            return ProxyError("OhosPatch could not inspect an imported value");
        }
        if (type == napi_undefined) {
            return ProxyResponse("undefined", nullptr);
        }
        if (type == napi_object || type == napi_function) {
            uint32_t handle = 0;
            if (!FindOrAddImportedValue(value, type, &handle)) {
                return ProxyError("OhosPatch could not retain an imported value");
            }
            JSVM_Value handleValue = nullptr;
            if (!JsvmOk(OH_JSVM_CreateUint32(env_, handle, &handleValue),
                        "OH_JSVM_CreateUint32(imported handle)", env_)) {
                return ProxyError("OhosPatch could not create an imported value handle");
            }
            return ProxyResponse(type == napi_function ? "function" : "object", handleValue);
        }
        if (type == napi_symbol || type == napi_external || type == napi_bigint) {
            return ProxyError("OhosPatch does not support this imported value type");
        }

        std::string json;
        JSVM_Value payload = nullptr;
        if (!NapiJsonStringify(hostEnv_, value, "null", &json) || !ParseJson(json, &payload)) {
            return ProxyError("OhosPatch could not serialize an imported value");
        }
        return ProxyResponse("value", payload);
    }

    bool DecodeImportedArguments(const std::string &wire, std::array<napi_value, kMaxArguments> *arguments,
                                 uint32_t *argumentCount)
    {
        if (!hostEnv_ || !arguments || !argumentCount) {
            LogError("DecodeImportedArguments received an invalid argument");
            return false;
        }
        napi_value encoded = nullptr;
        napi_value resolved = nullptr;
        if (!NapiJsonParse(hostEnv_, wire, &encoded) ||
            !ResolveBridgeWireValue(hostEnv_, encoded, 0, &resolved) ||
            !NapiOk(hostEnv_, napi_get_array_length(hostEnv_, resolved, argumentCount),
                    "napi_get_array_length(imported arguments)")) {
            return false;
        }
        if (*argumentCount > kMaxArguments) {
            LogError("OhosPatch imported argument count exceeds the limit");
            return false;
        }
        for (uint32_t index = 0; index < *argumentCount; ++index) {
            if (!NapiOk(hostEnv_, napi_get_element(hostEnv_, resolved, index, &(*arguments)[index]),
                        "napi_get_element(imported argument)")) {
                return false;
            }
        }
        return true;
    }

    bool ClearImportedValues()
    {
        bool success = true;
        for (size_t index = 0; index < importedValueCount_; ++index) {
            ImportedValue &record = importedValues_[index];
            if (record.reference && hostEnv_) {
                napi_status status = napi_delete_reference(hostEnv_, record.reference);
                if (status != napi_ok) {
                    LogError("napi_delete_reference(imported value)", static_cast<int>(status));
                    ClearPendingNapiException(hostEnv_);
                    success = false;
                }
            } else if (record.reference) {
                LogError("OhosPatch cannot release an imported value without its ArkTS environment");
                success = false;
            }
            record = {};
        }
        importedValueCount_ = 0;
        return success;
    }

    bool ResolveBridgeWireValue(napi_env napiEnv, napi_value encoded, size_t depth, napi_value *output)
    {
        if (!output || !encoded || depth > 32) {
            LogError("OhosPatch proxy wire value exceeds the supported depth");
            return false;
        }
        napi_valuetype type = napi_undefined;
        if (!NapiOk(napiEnv, napi_typeof(napiEnv, encoded, &type), "napi_typeof(proxy wire value)")) {
            return false;
        }
        if (type != napi_object) {
            *output = encoded;
            return true;
        }

        bool hasRemoteHandle = false;
        if (!NapiOk(napiEnv,
                    napi_has_named_property(napiEnv, encoded, "__ohospatch_proxy_handle__", &hasRemoteHandle),
                    "napi_has_named_property(proxy handle)")) {
            return false;
        }
        if (hasRemoteHandle) {
            uint32_t handle = 0;
            if (!NapiNamedUint32(napiEnv, encoded, "__ohospatch_proxy_handle__", &handle) ||
                activeInvocation_.env != napiEnv || handle >= activeInvocation_.proxyValueCount) {
                LogError("OhosPatch received an invalid proxy handle");
                return false;
            }
            *output = activeInvocation_.proxyValues[handle];
            return true;
        }

        bool hasImportHandle = false;
        if (!NapiOk(napiEnv,
                    napi_has_named_property(napiEnv, encoded, "__ohospatch_import_handle__", &hasImportHandle),
                    "napi_has_named_property(import handle)")) {
            return false;
        }
        if (hasImportHandle) {
            uint32_t handle = 0;
            if (!NapiNamedUint32(napiEnv, encoded, "__ohospatch_import_handle__", &handle) ||
                !GetImportedValue(handle, output)) {
                LogError("OhosPatch received an invalid import handle");
                return false;
            }
            return true;
        }

        bool hasUndefinedMarker = false;
        if (!NapiOk(napiEnv,
                    napi_has_named_property(napiEnv, encoded, "__ohospatch_proxy_undefined__", &hasUndefinedMarker),
                    "napi_has_named_property(proxy undefined)")) {
            return false;
        }
        if (hasUndefinedMarker) {
            *output = NapiUndefined(napiEnv);
            return *output != nullptr;
        }

        napi_value keys = nullptr;
        uint32_t length = 0;
        if (!NapiOk(napiEnv, napi_get_property_names(napiEnv, encoded, &keys),
                    "napi_get_property_names(proxy wire value)") ||
            !NapiOk(napiEnv, napi_get_array_length(napiEnv, keys, &length),
                    "napi_get_array_length(proxy wire keys)")) {
            return false;
        }
        if (length > 1024) {
            LogError("OhosPatch proxy wire object has too many properties");
            return false;
        }
        for (uint32_t index = 0; index < length; ++index) {
            napi_value key = nullptr;
            napi_value child = nullptr;
            napi_value resolved = nullptr;
            if (!NapiOk(napiEnv, napi_get_element(napiEnv, keys, index, &key),
                        "napi_get_element(proxy wire key)") ||
                !NapiOk(napiEnv, napi_get_property(napiEnv, encoded, key, &child),
                        "napi_get_property(proxy wire child)") ||
                !ResolveBridgeWireValue(napiEnv, child, depth + 1, &resolved) ||
                !NapiOk(napiEnv, napi_set_property(napiEnv, encoded, key, resolved),
                        "napi_set_property(proxy wire child)")) {
                return false;
            }
        }
        *output = encoded;
        return true;
    }

    static JSVM_Value ProxyGetCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime *runtime = Current(env);
        if (!runtime || !runtime->activeInvocation_.env) {
            return runtime ? runtime->ProxyError("OhosPatch proxy read occurred outside an active invocation")
                           : Undefined(env);
        }
        size_t argc = 2;
        JSVM_Value argv[2] = {nullptr, nullptr};
        uint32_t handle = 0;
        std::string property;
        if (!JsvmOk(OH_JSVM_GetCbInfo(env, info, &argc, argv, nullptr, nullptr), "OH_JSVM_GetCbInfo(proxy get)", env) ||
            argc < 2 ||
            !JsvmOk(OH_JSVM_GetValueUint32(env, argv[0], &handle), "OH_JSVM_GetValueUint32(proxy get handle)", env) ||
            !runtime->JsvmString(argv[1], &property) || handle >= runtime->activeInvocation_.proxyValueCount) {
            return runtime->ProxyError("OhosPatch proxy read received invalid arguments");
        }
        ActiveInvocation &active = runtime->activeInvocation_;
        napi_value key = nullptr;
        napi_value value = nullptr;
        if (!NapiOk(active.env, napi_create_string_utf8(active.env, property.c_str(), property.size(), &key),
                    "napi_create_string_utf8(proxy property)") ||
            !NapiOk(active.env, napi_get_property(active.env, active.proxyValues[handle], key, &value),
                    "napi_get_property(proxy value)")) {
            return runtime->ProxyError("OhosPatch could not read a proxied property");
        }
        return runtime->ProxyNapiValue(active, value);
    }

    static JSVM_Value ProxySetCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime *runtime = Current(env);
        if (!runtime || !runtime->activeInvocation_.env) {
            return runtime ? runtime->ProxyError("OhosPatch proxy write occurred outside an active invocation")
                           : Undefined(env);
        }
        size_t argc = 3;
        JSVM_Value argv[3] = {nullptr, nullptr, nullptr};
        uint32_t handle = 0;
        std::string property;
        std::string wire;
        if (!JsvmOk(OH_JSVM_GetCbInfo(env, info, &argc, argv, nullptr, nullptr), "OH_JSVM_GetCbInfo(proxy set)", env) ||
            argc < 3 ||
            !JsvmOk(OH_JSVM_GetValueUint32(env, argv[0], &handle), "OH_JSVM_GetValueUint32(proxy set handle)", env) ||
            !runtime->JsvmString(argv[1], &property) || !runtime->JsvmString(argv[2], &wire) ||
            handle >= runtime->activeInvocation_.proxyValueCount) {
            return runtime->ProxyError("OhosPatch proxy write received invalid arguments");
        }
        ActiveInvocation &active = runtime->activeInvocation_;
        napi_value encoded = nullptr;
        napi_value value = nullptr;
        napi_value key = nullptr;
        if (!NapiJsonParse(active.env, wire, &encoded) ||
            !runtime->ResolveBridgeWireValue(active.env, encoded, 0, &value) ||
            !NapiOk(active.env, napi_create_string_utf8(active.env, property.c_str(), property.size(), &key),
                    "napi_create_string_utf8(proxy set property)") ||
            !NapiOk(active.env, napi_set_property(active.env, active.proxyValues[handle], key, value),
                    "napi_set_property(proxy value)")) {
            return runtime->ProxyError("OhosPatch could not write a proxied property");
        }
        return runtime->ProxyResponse("ok", nullptr);
    }

    static JSVM_Value ProxyCallCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime *runtime = Current(env);
        if (!runtime || !runtime->activeInvocation_.env) {
            return runtime ? runtime->ProxyError("OhosPatch proxy call occurred outside an active invocation")
                           : Undefined(env);
        }
        size_t argc = 3;
        JSVM_Value argv[3] = {nullptr, nullptr, nullptr};
        uint32_t functionHandle = 0;
        uint32_t receiverHandle = 0;
        std::string wire;
        if (!JsvmOk(OH_JSVM_GetCbInfo(env, info, &argc, argv, nullptr, nullptr), "OH_JSVM_GetCbInfo(proxy call)", env) ||
            argc < 3 ||
            !JsvmOk(OH_JSVM_GetValueUint32(env, argv[0], &functionHandle),
                    "OH_JSVM_GetValueUint32(proxy function handle)", env) ||
            !JsvmOk(OH_JSVM_GetValueUint32(env, argv[1], &receiverHandle),
                    "OH_JSVM_GetValueUint32(proxy receiver handle)", env) ||
            !runtime->JsvmString(argv[2], &wire) ||
            functionHandle >= runtime->activeInvocation_.proxyValueCount ||
            receiverHandle >= runtime->activeInvocation_.proxyValueCount) {
            return runtime->ProxyError("OhosPatch proxy call received invalid arguments");
        }

        ActiveInvocation &active = runtime->activeInvocation_;
        napi_value encodedArgs = nullptr;
        napi_value resolvedArgs = nullptr;
        uint32_t argumentCount = 0;
        if (!NapiJsonParse(active.env, wire, &encodedArgs) ||
            !runtime->ResolveBridgeWireValue(active.env, encodedArgs, 0, &resolvedArgs) ||
            !NapiOk(active.env, napi_get_array_length(active.env, resolvedArgs, &argumentCount),
                    "napi_get_array_length(proxy call args)")) {
            return runtime->ProxyError("OhosPatch could not decode proxied method arguments");
        }
        if (argumentCount > kMaxArguments) {
            return runtime->ProxyError("OhosPatch proxied method argument count exceeds the limit");
        }
        std::array<napi_value, kMaxArguments> napiArgv{};
        for (uint32_t index = 0; index < argumentCount; ++index) {
            if (!NapiOk(active.env, napi_get_element(active.env, resolvedArgs, index, &napiArgv[index]),
                        "napi_get_element(proxy call arg)")) {
                return runtime->ProxyError("OhosPatch could not read a proxied method argument");
            }
        }
        napi_value result = nullptr;
        if (!NapiOk(active.env,
                    napi_call_function(active.env, active.proxyValues[receiverHandle],
                                       active.proxyValues[functionHandle], argumentCount, napiArgv.data(), &result),
                    "napi_call_function(proxy method)")) {
            return runtime->ProxyError("OhosPatch proxied method call failed");
        }
        return runtime->ProxyNapiValue(active, result);
    }

    static JSVM_Value ImportCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime *runtime = Current(env);
        if (!runtime || !runtime->hostEnv_) {
            return runtime ? runtime->ProxyError("OhosPatch import requires an active ArkTS environment")
                           : Undefined(env);
        }

        size_t argc = 1;
        JSVM_Value argument = nullptr;
        std::string descriptorJson;
        if (!JsvmOk(OH_JSVM_GetCbInfo(env, info, &argc, &argument, nullptr, nullptr),
                    "OH_JSVM_GetCbInfo(import)", env) ||
            argc < 1 || !runtime->JsvmString(argument, &descriptorJson)) {
            return runtime->ProxyError("Fixit.import requires an encoded target descriptor");
        }

        napi_env napiEnv = runtime->hostEnv_;
        napi_value target = nullptr;
        std::string modulePath;
        std::string moduleInfo;
        std::string exportName;
        if (!NapiJsonParse(napiEnv, descriptorJson, &target) ||
            !NapiNamedString(napiEnv, target, "modulePath", &modulePath) ||
            !NapiNamedString(napiEnv, target, "moduleInfo", &moduleInfo) ||
            !NapiNamedString(napiEnv, target, "exportName", &exportName)) {
            return runtime->ProxyError("Fixit.import received an invalid target descriptor");
        }

        napi_value module = nullptr;
        napi_status loadStatus = moduleInfo.empty()
                                     ? napi_load_module(napiEnv, modulePath.c_str(), &module)
                                     : napi_load_module_with_info(napiEnv, modulePath.c_str(), moduleInfo.c_str(),
                                                                  &module);
        if (!NapiOk(napiEnv, loadStatus, "napi_load_module_with_info(import target)")) {
            return runtime->ProxyError("Fixit.import could not load the target module");
        }

        napi_value imported = nullptr;
        napi_valuetype importedType = napi_undefined;
        if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, module, exportName.c_str(), &imported),
                    "napi_get_named_property(import export)") ||
            !NapiOk(napiEnv, napi_typeof(napiEnv, imported, &importedType), "napi_typeof(import export)")) {
            return runtime->ProxyError("Fixit.import could not resolve the exported class");
        }
        if (importedType != napi_function) {
            LogError("Fixit.import target is not a class or callable export: " + exportName);
            return runtime->ProxyError("Fixit.import target must be a class or callable export");
        }
        return runtime->ImportedNapiValue(imported);
    }

    static JSVM_Value ImportGetCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime *runtime = Current(env);
        if (!runtime || !runtime->hostEnv_) {
            return runtime ? runtime->ProxyError("OhosPatch imported read requires an active ArkTS environment")
                           : Undefined(env);
        }
        size_t argc = 2;
        JSVM_Value argv[2] = {nullptr, nullptr};
        uint32_t handle = 0;
        std::string property;
        napi_value target = nullptr;
        if (!JsvmOk(OH_JSVM_GetCbInfo(env, info, &argc, argv, nullptr, nullptr),
                    "OH_JSVM_GetCbInfo(import get)", env) ||
            argc < 2 ||
            !JsvmOk(OH_JSVM_GetValueUint32(env, argv[0], &handle),
                    "OH_JSVM_GetValueUint32(import get handle)", env) ||
            !runtime->JsvmString(argv[1], &property) || !runtime->GetImportedValue(handle, &target)) {
            return runtime->ProxyError("OhosPatch imported read received invalid arguments");
        }

        napi_env napiEnv = runtime->hostEnv_;
        napi_value key = nullptr;
        napi_value value = nullptr;
        if (!NapiOk(napiEnv, napi_create_string_utf8(napiEnv, property.c_str(), property.size(), &key),
                    "napi_create_string_utf8(import property)") ||
            !NapiOk(napiEnv, napi_get_property(napiEnv, target, key, &value),
                    "napi_get_property(imported value)")) {
            return runtime->ProxyError("OhosPatch could not read an imported property");
        }
        return runtime->ImportedNapiValue(value);
    }

    static JSVM_Value ImportSetCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime *runtime = Current(env);
        if (!runtime || !runtime->hostEnv_) {
            return runtime ? runtime->ProxyError("OhosPatch imported write requires an active ArkTS environment")
                           : Undefined(env);
        }
        size_t argc = 3;
        JSVM_Value argv[3] = {nullptr, nullptr, nullptr};
        uint32_t handle = 0;
        std::string property;
        std::string wire;
        napi_value target = nullptr;
        if (!JsvmOk(OH_JSVM_GetCbInfo(env, info, &argc, argv, nullptr, nullptr),
                    "OH_JSVM_GetCbInfo(import set)", env) ||
            argc < 3 ||
            !JsvmOk(OH_JSVM_GetValueUint32(env, argv[0], &handle),
                    "OH_JSVM_GetValueUint32(import set handle)", env) ||
            !runtime->JsvmString(argv[1], &property) || !runtime->JsvmString(argv[2], &wire) ||
            !runtime->GetImportedValue(handle, &target)) {
            return runtime->ProxyError("OhosPatch imported write received invalid arguments");
        }

        napi_env napiEnv = runtime->hostEnv_;
        napi_value encoded = nullptr;
        napi_value value = nullptr;
        napi_value key = nullptr;
        if (!NapiJsonParse(napiEnv, wire, &encoded) ||
            !runtime->ResolveBridgeWireValue(napiEnv, encoded, 0, &value) ||
            !NapiOk(napiEnv, napi_create_string_utf8(napiEnv, property.c_str(), property.size(), &key),
                    "napi_create_string_utf8(import set property)") ||
            !NapiOk(napiEnv, napi_set_property(napiEnv, target, key, value),
                    "napi_set_property(imported value)")) {
            return runtime->ProxyError("OhosPatch could not write an imported property");
        }
        return runtime->ProxyResponse("ok", nullptr);
    }

    static JSVM_Value ImportCallCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime *runtime = Current(env);
        if (!runtime || !runtime->hostEnv_) {
            return runtime ? runtime->ProxyError("OhosPatch imported call requires an active ArkTS environment")
                           : Undefined(env);
        }
        size_t argc = 3;
        JSVM_Value argv[3] = {nullptr, nullptr, nullptr};
        uint32_t functionHandle = 0;
        uint32_t receiverHandle = 0;
        std::string wire;
        napi_value function = nullptr;
        napi_value receiver = nullptr;
        if (!JsvmOk(OH_JSVM_GetCbInfo(env, info, &argc, argv, nullptr, nullptr),
                    "OH_JSVM_GetCbInfo(import call)", env) ||
            argc < 3 ||
            !JsvmOk(OH_JSVM_GetValueUint32(env, argv[0], &functionHandle),
                    "OH_JSVM_GetValueUint32(import function handle)", env) ||
            !JsvmOk(OH_JSVM_GetValueUint32(env, argv[1], &receiverHandle),
                    "OH_JSVM_GetValueUint32(import receiver handle)", env) ||
            !runtime->JsvmString(argv[2], &wire) || !runtime->GetImportedValue(functionHandle, &function) ||
            !runtime->GetImportedValue(receiverHandle, &receiver)) {
            return runtime->ProxyError("OhosPatch imported call received invalid arguments");
        }

        std::array<napi_value, kMaxArguments> arguments{};
        uint32_t argumentCount = 0;
        if (!runtime->DecodeImportedArguments(wire, &arguments, &argumentCount)) {
            return runtime->ProxyError("OhosPatch could not decode imported method arguments");
        }
        napi_value result = nullptr;
        if (!NapiOk(runtime->hostEnv_,
                    napi_call_function(runtime->hostEnv_, receiver, function, argumentCount, arguments.data(), &result),
                    "napi_call_function(imported method)")) {
            return runtime->ProxyError("OhosPatch imported method call failed");
        }
        return runtime->ImportedNapiValue(result);
    }

    static JSVM_Value ImportConstructCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime *runtime = Current(env);
        if (!runtime || !runtime->hostEnv_) {
            return runtime ? runtime->ProxyError("OhosPatch imported constructor requires an active ArkTS environment")
                           : Undefined(env);
        }
        size_t argc = 2;
        JSVM_Value argv[2] = {nullptr, nullptr};
        uint32_t constructorHandle = 0;
        std::string wire;
        napi_value constructor = nullptr;
        if (!JsvmOk(OH_JSVM_GetCbInfo(env, info, &argc, argv, nullptr, nullptr),
                    "OH_JSVM_GetCbInfo(import construct)", env) ||
            argc < 2 ||
            !JsvmOk(OH_JSVM_GetValueUint32(env, argv[0], &constructorHandle),
                    "OH_JSVM_GetValueUint32(import constructor handle)", env) ||
            !runtime->JsvmString(argv[1], &wire) || !runtime->GetImportedValue(constructorHandle, &constructor)) {
            return runtime->ProxyError("OhosPatch imported constructor received invalid arguments");
        }

        std::array<napi_value, kMaxArguments> arguments{};
        uint32_t argumentCount = 0;
        if (!runtime->DecodeImportedArguments(wire, &arguments, &argumentCount)) {
            return runtime->ProxyError("OhosPatch could not decode imported constructor arguments");
        }
        napi_value instance = nullptr;
        if (!NapiOk(runtime->hostEnv_,
                    napi_new_instance(runtime->hostEnv_, constructor, argumentCount, arguments.data(), &instance),
                    "napi_new_instance(imported class)")) {
            return runtime->ProxyError("OhosPatch imported class construction failed");
        }
        return runtime->ImportedNapiValue(instance);
    }

    static JSVM_Value OriginCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime *runtime = Current(env);
        if (!runtime || !runtime->activeInvocation_.env || !runtime->activeInvocation_.hook) {
            LogError("Original method called outside an active OhosPatch invocation");
            return Undefined(env);
        }

        size_t argc = 1;
        JSVM_Value argument = nullptr;
        std::string argsJson;
        if (!JsvmOk(OH_JSVM_GetCbInfo(env, info, &argc, &argument, nullptr, nullptr), "OH_JSVM_GetCbInfo(origin)", env) ||
            argc < 1 || !runtime->JsvmString(argument, &argsJson)) {
            return runtime->ProxyError("Original method requires encoded arguments");
        }

        ActiveInvocation &active = runtime->activeInvocation_;
        napi_value encodedArgs = nullptr;
        napi_value napiArgsArray = nullptr;
        if (!NapiJsonParse(active.env, argsJson, &encodedArgs) ||
            !runtime->ResolveBridgeWireValue(active.env, encodedArgs, 0, &napiArgsArray)) {
            return runtime->ProxyError("Original method arguments could not be decoded");
        }
        uint32_t napiArgc = 0;
        if (!NapiOk(active.env, napi_get_array_length(active.env, napiArgsArray, &napiArgc),
                    "napi_get_array_length(origin args)")) {
            return Undefined(env);
        }
        if (napiArgc > kMaxArguments) {
            LogError("Original method argument count exceeds the OhosPatch limit");
            return Undefined(env);
        }

        std::array<napi_value, kMaxArguments> napiArgv{};
        for (uint32_t index = 0; index < napiArgc; ++index) {
            if (!NapiOk(active.env, napi_get_element(active.env, napiArgsArray, index, &napiArgv[index]),
                        "napi_get_element(origin args)")) {
                return Undefined(env);
            }
        }

        napi_value result = nullptr;
        bool exceptionPending = false;
        if (!CallOriginal(active.env, active.hook, active.receiver, napiArgc, napiArgv.data(), &result,
                          &exceptionPending)) {
            active.originalExceptionPending = exceptionPending;
            return runtime->ProxyError("Original ArkTS method call failed");
        }
        return runtime->ProxyNapiValue(active, result);
    }

    static JSVM_Value HiLogCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime *runtime = Current(env);
        if (!runtime) {
            return Undefined(env);
        }

        size_t argc = 2;
        JSVM_Value argv[2] = {nullptr, nullptr};
        if (!JsvmOk(OH_JSVM_GetCbInfo(env, info, &argc, argv, nullptr, nullptr), "OH_JSVM_GetCbInfo(hilog)", env) ||
            argc < 2) {
            return Undefined(env);
        }

        std::string level;
        std::string message;
        if (!runtime->JsvmString(argv[0], &level) || !runtime->JsvmString(argv[1], &message)) {
            return Undefined(env);
        }
        LogLevel logLevel = LOG_INFO;
        if (level == "debug") {
            logLevel = LOG_DEBUG;
        } else if (level == "warn") {
            logLevel = LOG_WARN;
        } else if (level == "error") {
            logLevel = LOG_ERROR;
        }
        OH_LOG_Print(LOG_APP, logLevel, kLogDomain, kLogTag, "%{public}s", message.c_str());
        return Undefined(env);
    }

    static JSVM_Value ScheduleTimerCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime *runtime = Current(env);
        if (!runtime) {
            return Undefined(env);
        }

        size_t argc = 3;
        JSVM_Value argv[3] = {nullptr, nullptr, nullptr};
        if (!JsvmOk(OH_JSVM_GetCbInfo(env, info, &argc, argv, nullptr, nullptr), "OH_JSVM_GetCbInfo(schedule timer)",
                    env) ||
            argc < 3) {
            LogError("OhosPatch schedule timer requires id, delay, and repeating arguments");
            return Undefined(env);
        }

        uint32_t id = 0;
        uint32_t delay = 0;
        bool repeating = false;
        if (!JsvmOk(OH_JSVM_GetValueUint32(env, argv[0], &id), "OH_JSVM_GetValueUint32(timer ID)", env) ||
            !JsvmOk(OH_JSVM_GetValueUint32(env, argv[1], &delay), "OH_JSVM_GetValueUint32(timer delay)", env) ||
            !JsvmOk(OH_JSVM_GetValueBool(env, argv[2], &repeating), "OH_JSVM_GetValueBool(timer repeating)", env)) {
            return Undefined(env);
        }

        JSVM_Value result = nullptr;
        if (!runtime->Bool(runtime->ScheduleTimer(id, delay, repeating), &result)) {
            return Undefined(env);
        }
        return result;
    }

    static JSVM_Value CancelTimerCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime *runtime = Current(env);
        if (!runtime) {
            return Undefined(env);
        }

        size_t argc = 1;
        JSVM_Value argument = nullptr;
        if (!JsvmOk(OH_JSVM_GetCbInfo(env, info, &argc, &argument, nullptr, nullptr), "OH_JSVM_GetCbInfo(cancel timer)",
                    env) ||
            argc < 1) {
            LogError("OhosPatch cancel timer requires a timer ID");
            return Undefined(env);
        }

        uint32_t id = 0;
        if (!JsvmOk(OH_JSVM_GetValueUint32(env, argument, &id), "OH_JSVM_GetValueUint32(cancel timer ID)", env)) {
            return Undefined(env);
        }

        JSVM_Value result = nullptr;
        if (!runtime->Bool(runtime->CancelTimer(id), &result)) {
            return Undefined(env);
        }
        return result;
    }

    static napi_value HookCallback(napi_env env, napi_callback_info info)
    {
        size_t argc = kMaxArguments;
        napi_value receiver = nullptr;
        void *data = nullptr;
        std::array<napi_value, kMaxArguments> argv{};
        if (!NapiOk(env, napi_get_cb_info(env, info, &argc, argv.data(), &receiver, &data), "napi_get_cb_info(hook)")) {
            return NapiUndefined(env);
        }
        if (argc > kMaxArguments) {
            LogError("Hook arguments were truncated to the OhosPatch limit");
            argc = kMaxArguments;
        }
        if (!data) {
            LogError("OhosPatch hook callback has no HookRecord");
            return NapiUndefined(env);
        }
        return Runtime().InvokeHook(env, static_cast<HookRecord *>(data), receiver, argc, argv.data());
    }

    static JSVM_Value Undefined(JSVM_Env env)
    {
        JSVM_Value value = nullptr;
        if (!JsvmOk(OH_JSVM_GetUndefined(env, &value), "OH_JSVM_GetUndefined", env)) {
            return nullptr;
        }
        return value;
    }

    static bool CallOriginal(napi_env env, HookRecord *hook, napi_value receiver, size_t argc, const napi_value *argv,
                             napi_value *result, bool *exceptionPending)
    {
        if (exceptionPending) {
            *exceptionPending = false;
        }
        if (!hook || !result) {
            LogError("CallOriginal received an invalid argument");
            return false;
        }

        napi_value original = nullptr;
        if (!NapiOk(env, napi_get_reference_value(env, hook->original, &original),
                    "napi_get_reference_value(original)")) {
            return false;
        }
        napi_status status = napi_call_function(env, receiver, original, argc, argv, result);
        if (status == napi_pending_exception) {
            LogError("Original ArkTS method threw an exception");
            if (exceptionPending) {
                *exceptionPending = true;
            }
            return false;
        }
        return NapiOk(env, status, "napi_call_function(original)");
    }

    static bool ApplyTargetPatch(napi_env env, napi_value target, napi_value patch)
    {
        napi_value keys = nullptr;
        if (!NapiOk(env, napi_get_property_names(env, patch, &keys), "napi_get_property_names(target patch)")) {
            return false;
        }
        uint32_t length = 0;
        if (!NapiOk(env, napi_get_array_length(env, keys, &length), "napi_get_array_length(target keys)")) {
            return false;
        }
        for (uint32_t index = 0; index < length; ++index) {
            napi_value key = nullptr;
            napi_value value = nullptr;
            if (!NapiOk(env, napi_get_element(env, keys, index, &key), "napi_get_element(target key)") ||
                !NapiOk(env, napi_get_property(env, patch, key, &value), "napi_get_property(target value)") ||
                !NapiOk(env, napi_set_property(env, target, key, value), "napi_set_property(target value)")) {
                return false;
            }
        }
        return true;
    }

    bool CallUiValueHandler(napi_env napiEnv, uint32_t ruleId, napi_value value, napi_value *envelope, bool *handled)
    {
        if (!envelope || !handled) {
            LogError("CallUiValueHandler received an invalid argument");
            return false;
        }
        *handled = false;

        std::string valueJson;
        if (!NapiJsonStringify(napiEnv, value, "null", &valueJson)) {
            return false;
        }

        JSVM_HandleScope scope = nullptr;
        if (!JsvmOk(OH_JSVM_OpenHandleScope(env_, &scope), "OH_JSVM_OpenHandleScope(component value)", env_)) {
            return false;
        }

        JSVM_Value ruleIdValue = nullptr;
        JSVM_Value valueValue = nullptr;
        JSVM_Value result = nullptr;
        std::string resultJson;
        bool success = JsvmOk(OH_JSVM_CreateUint32(env_, ruleId, &ruleIdValue),
                              "OH_JSVM_CreateUint32(component value rule)", env_) &&
                       ParseJson(valueJson, &valueValue);
        if (success) {
            JSVM_Value args[] = {ruleIdValue, valueValue};
            success = CallGlobal("__ohospatch_callUiValue", args, std::size(args), &result) &&
                      StringifyJson(result, &resultJson);
        }
        if (!JsvmOk(OH_JSVM_CloseHandleScope(env_, scope), "OH_JSVM_CloseHandleScope(component value)", env_)) {
            success = false;
        }
        if (!success || !NapiJsonParse(napiEnv, resultJson, envelope)) {
            return false;
        }

        napi_value handledValue = nullptr;
        return NapiOk(napiEnv, napi_get_named_property(napiEnv, *envelope, "handled", &handledValue),
                      "napi_get_named_property(component value handled)") &&
               NapiOk(napiEnv, napi_get_value_bool(napiEnv, handledValue, handled),
                      "napi_get_value_bool(component value handled)");
    }

    bool CallUiEventHandler(napi_env napiEnv, uint32_t ruleId, napi_value event, napi_value state, napi_value owner,
                            napi_value *envelope, bool *handled)
    {
        if (!envelope || !handled) {
            LogError("CallUiEventHandler received an invalid argument");
            return false;
        }
        *handled = false;

        std::string eventJson;
        std::string stateJson;
        if (!NapiJsonStringify(napiEnv, event, "{}", &eventJson) ||
            !NapiJsonStringify(napiEnv, state, "{}", &stateJson)) {
            return false;
        }

        JSVM_HandleScope scope = nullptr;
        if (!JsvmOk(OH_JSVM_OpenHandleScope(env_, &scope), "OH_JSVM_OpenHandleScope(component event)", env_)) {
            return false;
        }

        JSVM_Value ruleIdValue = nullptr;
        JSVM_Value eventValue = nullptr;
        JSVM_Value stateValue = nullptr;
        JSVM_Value result = nullptr;
        std::string resultJson;
        bool success = JsvmOk(OH_JSVM_CreateUint32(env_, ruleId, &ruleIdValue),
                              "OH_JSVM_CreateUint32(component event rule)", env_) &&
                       ParseJson(eventJson, &eventValue) && ParseJson(stateJson, &stateValue);
        ActiveInvocation previous = activeInvocation_;
        if (owner) {
            activeInvocation_ = {};
            activeInvocation_.env = napiEnv;
            activeInvocation_.receiver = owner;
            activeInvocation_.proxyValues[0] = owner;
            activeInvocation_.proxyValueCount = 1;
        }

        if (success) {
            JSVM_Value ownerHandleValue = nullptr;
            JSVM_Value *args = nullptr;
            size_t argc = 0;
            JSVM_Value ownerArgs[4] = {ruleIdValue, eventValue, stateValue, nullptr};
            JSVM_Value plainArgs[3] = {ruleIdValue, eventValue, stateValue};
            if (owner && JsvmOk(OH_JSVM_CreateUint32(env_, 0, &ownerHandleValue),
                                "OH_JSVM_CreateUint32(component event owner)", env_)) {
                ownerArgs[3] = ownerHandleValue;
                args = ownerArgs;
                argc = std::size(ownerArgs);
            } else {
                args = plainArgs;
                argc = std::size(plainArgs);
            }
            success = CallGlobal("__ohospatch_callUiEvent", args, argc, &result) &&
                      StringifyJson(result, &resultJson);
        }
        if (owner) {
            activeInvocation_ = previous;
        }
        if (!JsvmOk(OH_JSVM_CloseHandleScope(env_, scope), "OH_JSVM_CloseHandleScope(component event)", env_)) {
            success = false;
        }
        if (!success || !NapiJsonParse(napiEnv, resultJson, envelope)) {
            return false;
        }

        napi_value handledValue = nullptr;
        return NapiOk(napiEnv, napi_get_named_property(napiEnv, *envelope, "handled", &handledValue),
                      "napi_get_named_property(component event handled)") &&
               NapiOk(napiEnv, napi_get_value_bool(napiEnv, handledValue, handled),
                      "napi_get_value_bool(component event handled)");
    }

    static napi_value CallOriginalUiEvent(napi_env env, UiEventCallbackRecord *record, napi_value receiver,
                                          size_t argc, const napi_value *argv)
    {
        if (!record || !record->originalEvent) {
            return NapiUndefined(env);
        }
        napi_value original = nullptr;
        if (!NapiOk(env, napi_get_reference_value(env, record->originalEvent, &original),
                    "napi_get_reference_value(original component event)")) {
            return NapiUndefined(env);
        }
        napi_value result = nullptr;
        napi_status status = napi_call_function(env, receiver, original, argc, argv, &result);
        if (status == napi_pending_exception) {
            LogError("Original component event callback produced a pending exception");
            return nullptr;
        }
        if (!NapiOk(env, status, "napi_call_function(original component event)")) {
            return NapiUndefined(env);
        }
        return result;
    }

    void ApplyUiValueRules(napi_env napiEnv, const std::string &targetKey, UiRuleKind kind, napi_value target)
    {
        if (!target) {
            return;
        }
        for (size_t index = 0; index < uiRuleCount_; ++index) {
            UiRule *rule = uiRules_[index].get();
            if (!rule || rule->kind != kind || rule->targetKey != targetKey) {
                continue;
            }

            napi_value current = nullptr;
            if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, target, rule->propertyName.c_str(), &current),
                        "napi_get_named_property(component value)")) {
                continue;
            }
            napi_value envelope = nullptr;
            bool handled = false;
            if (!CallUiValueHandler(napiEnv, rule->ruleId, current, &envelope, &handled) || !handled) {
                continue;
            }
            napi_value replacement = nullptr;
            if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, envelope, "value", &replacement),
                        "napi_get_named_property(component replacement value)")) {
                continue;
            }
            NapiOk(napiEnv, napi_set_named_property(napiEnv, target, rule->propertyName.c_str(), replacement),
                   "napi_set_named_property(component replacement value)");
        }
    }

    void ApplyUiParamValuesToOwner(napi_env napiEnv, const std::string &targetKey, napi_value params,
                                   napi_value owner)
    {
        for (size_t index = 0; index < uiRuleCount_; ++index) {
            UiRule *rule = uiRules_[index].get();
            if (!rule || rule->kind != UiRuleKind::PARAM || rule->targetKey != targetKey) {
                continue;
            }
            bool hasValue = false;
            napi_value value = nullptr;
            if (NapiOk(napiEnv, napi_has_named_property(napiEnv, params, rule->propertyName.c_str(), &hasValue),
                       "napi_has_named_property(component parameter)") &&
                hasValue &&
                NapiOk(napiEnv, napi_get_named_property(napiEnv, params, rule->propertyName.c_str(), &value),
                       "napi_get_named_property(component parameter)")) {
                NapiOk(napiEnv, napi_set_named_property(napiEnv, owner, rule->propertyName.c_str(), value),
                       "napi_set_named_property(component parameter)");
            }
        }
    }

    bool PushUiRenderFrame(const std::string &targetKey, napi_value owner)
    {
        if (uiRenderDepth_ >= kMaxUiRenderDepth) {
            LogError("Component render nesting exceeds the OhosPatch limit");
            return false;
        }
        UiRenderFrame &frame = uiRenderFrames_[uiRenderDepth_++];
        frame.targetKey = targetKey;
        frame.owner = owner;
        frame.counterCount = 0;
        return true;
    }

    void PopUiRenderFrame()
    {
        if (uiRenderDepth_ == 0) {
            return;
        }
        UiRenderFrame &frame = uiRenderFrames_[--uiRenderDepth_];
        frame.targetKey.clear();
        frame.owner = nullptr;
        frame.counterCount = 0;
    }

    bool RuleMatchesNode(const UiRule *rule, const UiNodeCallbackRecord *record) const
    {
        return rule && record && rule->targetKey == record->targetKey && rule->nodeType == record->nodeType &&
               rule->occurrence == record->occurrence;
    }

    bool ResolveUiNodeType(napi_env napiEnv, const std::string &targetKey, napi_value componentApi,
                           std::string *nodeType)
    {
        napi_value global = nullptr;
        if (!NapiOk(napiEnv, napi_get_global(napiEnv, &global), "napi_get_global(component node type)")) {
            return false;
        }
        for (size_t index = 0; index < uiRuleCount_; ++index) {
            UiRule *rule = uiRules_[index].get();
            if (!rule || (rule->kind != UiRuleKind::ATTRIBUTE && rule->kind != UiRuleKind::EVENT) ||
                rule->targetKey != targetKey) {
                continue;
            }
            bool alreadyChecked = false;
            for (size_t previous = 0; previous < index; ++previous) {
                UiRule *candidate = uiRules_[previous].get();
                if (candidate && candidate->targetKey == targetKey && candidate->nodeType == rule->nodeType) {
                    alreadyChecked = true;
                    break;
                }
            }
            if (alreadyChecked) {
                continue;
            }

            bool hasApi = false;
            if (!NapiOk(napiEnv, napi_has_named_property(napiEnv, global, rule->nodeType.c_str(), &hasApi),
                        "napi_has_named_property(component API)") ||
                !hasApi) {
                continue;
            }
            napi_value expectedApi = nullptr;
            bool equal = false;
            if (NapiOk(napiEnv, napi_get_named_property(napiEnv, global, rule->nodeType.c_str(), &expectedApi),
                       "napi_get_named_property(component API)") &&
                NapiOk(napiEnv, napi_strict_equals(napiEnv, componentApi, expectedApi, &equal),
                       "napi_strict_equals(component API)") &&
                equal) {
                *nodeType = rule->nodeType;
                return true;
            }
        }
        return false;
    }

    void WrapUiNodeBuilder(napi_env napiEnv, const std::string &targetKey, size_t argc, napi_value *argv)
    {
        if (uiRenderDepth_ == 0 || argc < 2 || !argv || uiRenderFrames_[uiRenderDepth_ - 1].targetKey != targetKey) {
            return;
        }
        napi_valuetype builderType = napi_undefined;
        if (!NapiOk(napiEnv, napi_typeof(napiEnv, argv[0], &builderType), "napi_typeof(component node builder)") ||
            builderType != napi_function) {
            return;
        }

        std::string nodeType;
        if (!ResolveUiNodeType(napiEnv, targetKey, argv[1], &nodeType)) {
            return;
        }
        UiRenderFrame &frame = uiRenderFrames_[uiRenderDepth_ - 1];
        UiNodeTypeCounter *counter = nullptr;
        for (size_t index = 0; index < frame.counterCount; ++index) {
            if (frame.counters[index].nodeType == nodeType) {
                counter = &frame.counters[index];
                break;
            }
        }
        if (!counter) {
            if (frame.counterCount >= frame.counters.size()) {
                LogError("Component node type count exceeds the OhosPatch limit");
                return;
            }
            counter = &frame.counters[frame.counterCount++];
            counter->nodeType = nodeType;
            counter->count = 0;
        }
        uint32_t occurrence = counter->count++;

        bool hasMatchingRule = false;
        for (size_t index = 0; index < uiRuleCount_; ++index) {
            UiRule *rule = uiRules_[index].get();
            if (rule && (rule->kind == UiRuleKind::ATTRIBUTE || rule->kind == UiRuleKind::EVENT) &&
                rule->targetKey == targetKey && rule->nodeType == nodeType && rule->occurrence == occurrence) {
                hasMatchingRule = true;
                break;
            }
        }
        if (!hasMatchingRule) {
            return;
        }

        UiNodeCallbackRecord *record = new (std::nothrow) UiNodeCallbackRecord();
        if (!record) {
            LogError("Cannot allocate OhosPatch UiNodeCallbackRecord");
            return;
        }
        record->env = napiEnv;
        record->targetKey = targetKey;
        record->nodeType = nodeType;
        record->occurrence = occurrence;
        if (!NapiOk(napiEnv, napi_create_reference(napiEnv, argv[0], 1, &record->originalBuilder),
                    "napi_create_reference(component node builder)") ||
            !NapiOk(napiEnv, napi_create_reference(napiEnv, argv[1], 1, &record->componentApi),
                    "napi_create_reference(component API)") ||
            !NapiOk(napiEnv, napi_create_reference(napiEnv, frame.owner, 0, &record->owner),
                    "napi_create_reference(component owner)")) {
            FinalizeUiNodeCallback(napiEnv, record, nullptr);
            return;
        }

        napi_value wrapper = nullptr;
        if (!NapiOk(napiEnv,
                    napi_create_function(napiEnv, "ohospatchNodeBuilder", NAPI_AUTO_LENGTH, UiNodeBuilderCallback,
                                         record, &wrapper),
                    "napi_create_function(component node builder)") ||
            !NapiOk(napiEnv, napi_add_finalizer(napiEnv, wrapper, record, FinalizeUiNodeCallback, nullptr, nullptr),
                    "napi_add_finalizer(component node builder)")) {
            FinalizeUiNodeCallback(napiEnv, record, nullptr);
            return;
        }
        argv[0] = wrapper;
    }

    size_t PrepareUiEventCaptures(napi_env napiEnv, UiNodeCallbackRecord *record, napi_value componentApi,
                                  UiEventCaptureContext *captures, size_t capacity)
    {
        size_t count = 0;
        for (size_t index = 0; index < uiRuleCount_ && count < capacity; ++index) {
            UiRule *rule = uiRules_[index].get();
            if (!rule || rule->kind != UiRuleKind::EVENT || !RuleMatchesNode(rule, record)) {
                continue;
            }

            napi_value registrar = nullptr;
            napi_valuetype registrarType = napi_undefined;
            if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, componentApi, rule->eventName.c_str(), &registrar),
                        "napi_get_named_property(component event registrar)") ||
                !NapiOk(napiEnv, napi_typeof(napiEnv, registrar, &registrarType),
                        "napi_typeof(component event registrar)") ||
                registrarType != napi_function) {
                LogError(rule->nodeType + "." + rule->eventName + " is not an event registrar");
                continue;
            }

            UiEventCaptureContext &capture = captures[count];
            capture.env = napiEnv;
            capture.rule = rule;
            if (!NapiOk(napiEnv, napi_create_reference(napiEnv, registrar, 1, &capture.originalRegistrar),
                        "napi_create_reference(component event registrar)")) {
                continue;
            }
            napi_value captureFunction = nullptr;
            if (!NapiOk(napiEnv,
                        napi_create_function(napiEnv, "ohospatchEventCapture", NAPI_AUTO_LENGTH, UiEventCaptureCallback,
                                             &capture, &captureFunction),
                        "napi_create_function(component event capture)") ||
                !NapiOk(napiEnv,
                        napi_set_named_property(napiEnv, componentApi, rule->eventName.c_str(), captureFunction),
                        "napi_set_named_property(component event capture)")) {
                DeleteNapiReference(napiEnv, capture.originalRegistrar,
                                    "napi_delete_reference(component event registrar)");
                capture.originalRegistrar = nullptr;
                continue;
            }
            capture.installed = true;
            ++count;
        }
        if (count == capacity) {
            for (size_t index = 0; index < uiRuleCount_; ++index) {
                UiRule *rule = uiRules_[index].get();
                if (rule && rule->kind == UiRuleKind::EVENT && RuleMatchesNode(rule, record)) {
                    LogError("Component event count reached the OhosPatch per-node limit");
                    break;
                }
            }
        }
        return count;
    }

    static void RestoreUiEventCaptures(napi_env napiEnv, napi_value componentApi, UiEventCaptureContext *captures,
                                       size_t count)
    {
        for (size_t index = 0; index < count; ++index) {
            UiEventCaptureContext &capture = captures[index];
            if (!capture.installed || !capture.rule || !capture.originalRegistrar) {
                continue;
            }
            napi_value registrar = nullptr;
            if (NapiOk(napiEnv, napi_get_reference_value(napiEnv, capture.originalRegistrar, &registrar),
                       "napi_get_reference_value(component event registrar)")) {
                NapiOk(napiEnv,
                       napi_set_named_property(napiEnv, componentApi, capture.rule->eventName.c_str(), registrar),
                       "napi_set_named_property(restore component event registrar)");
            }
            capture.installed = false;
        }
    }

    static void ReleaseUiEventCaptures(napi_env napiEnv, UiEventCaptureContext *captures, size_t count)
    {
        for (size_t index = 0; index < count; ++index) {
            DeleteNapiReference(napiEnv, captures[index].originalRegistrar,
                                "napi_delete_reference(component event registrar)");
            DeleteNapiReference(napiEnv, captures[index].originalEvent,
                                "napi_delete_reference(original component event)");
            captures[index].originalRegistrar = nullptr;
            captures[index].originalEvent = nullptr;
        }
    }

    void ApplyUiAttributes(napi_env napiEnv, UiNodeCallbackRecord *record, napi_value componentApi)
    {
        for (size_t index = 0; index < uiRuleCount_; ++index) {
            UiRule *rule = uiRules_[index].get();
            if (!rule || rule->kind != UiRuleKind::ATTRIBUTE || !RuleMatchesNode(rule, record)) {
                continue;
            }

            napi_value arguments = nullptr;
            uint32_t argc = 0;
            if (!NapiJsonParse(napiEnv, rule->argumentsJson, &arguments) ||
                !NapiOk(napiEnv, napi_get_array_length(napiEnv, arguments, &argc),
                        "napi_get_array_length(component attribute arguments)")) {
                continue;
            }
            if (argc > kMaxArguments) {
                LogError("Component attribute argument count exceeds the OhosPatch limit");
                continue;
            }
            std::array<napi_value, kMaxArguments> argv{};
            bool argsReady = true;
            for (uint32_t argumentIndex = 0; argumentIndex < argc; ++argumentIndex) {
                if (!NapiOk(napiEnv, napi_get_element(napiEnv, arguments, argumentIndex, &argv[argumentIndex]),
                            "napi_get_element(component attribute argument)")) {
                    argsReady = false;
                    break;
                }
            }
            if (!argsReady) {
                continue;
            }

            napi_value attribute = nullptr;
            napi_valuetype attributeType = napi_undefined;
            if (!NapiOk(napiEnv,
                        napi_get_named_property(napiEnv, componentApi, rule->attributeName.c_str(), &attribute),
                        "napi_get_named_property(component attribute)") ||
                !NapiOk(napiEnv, napi_typeof(napiEnv, attribute, &attributeType),
                        "napi_typeof(component attribute)") ||
                attributeType != napi_function) {
                LogError(rule->nodeType + "." + rule->attributeName + " is not an attribute function");
                continue;
            }
            napi_value ignored = nullptr;
            NapiOk(napiEnv, napi_call_function(napiEnv, componentApi, attribute, argc, argv.data(), &ignored),
                   "napi_call_function(component attribute)");
        }
    }

    void InstallUiEventCallbacks(napi_env napiEnv, UiNodeCallbackRecord *nodeRecord, napi_value componentApi,
                                 UiEventCaptureContext *captures, size_t count)
    {
        napi_value owner = nullptr;
        NapiOk(napiEnv, napi_get_reference_value(napiEnv, nodeRecord->owner, &owner),
               "napi_get_reference_value(component owner for event)");

        for (size_t index = 0; index < count; ++index) {
            UiEventCaptureContext &capture = captures[index];
            UiRule *rule = capture.rule;
            if (!rule || !capture.originalRegistrar) {
                continue;
            }

            UiEventCallbackRecord *record = new (std::nothrow) UiEventCallbackRecord();
            if (!record) {
                LogError("Cannot allocate OhosPatch UiEventCallbackRecord");
                continue;
            }
            record->env = napiEnv;
            record->ruleId = rule->ruleId;
            record->captureCount = rule->captureCount;
            record->captureProperties = rule->captureProperties;
            record->originalEvent = capture.originalEvent;
            capture.originalEvent = nullptr;
            if (owner && !NapiOk(napiEnv, napi_create_reference(napiEnv, owner, 0, &record->owner),
                                 "napi_create_reference(component event owner)")) {
                FinalizeUiEventCallback(napiEnv, record, nullptr);
                continue;
            }

            napi_value callback = nullptr;
            if (!NapiOk(napiEnv,
                        napi_create_function(napiEnv, "ohospatchEvent", NAPI_AUTO_LENGTH, UiEventCallback, record,
                                             &callback),
                        "napi_create_function(component event)") ||
                !NapiOk(napiEnv, napi_add_finalizer(napiEnv, callback, record, FinalizeUiEventCallback, nullptr, nullptr),
                        "napi_add_finalizer(component event)")) {
                FinalizeUiEventCallback(napiEnv, record, nullptr);
                continue;
            }

            napi_value registrar = nullptr;
            if (!NapiOk(napiEnv, napi_get_reference_value(napiEnv, capture.originalRegistrar, &registrar),
                        "napi_get_reference_value(component event registrar)")) {
                continue;
            }
            napi_value ignored = nullptr;
            NapiOk(napiEnv, napi_call_function(napiEnv, componentApi, registrar, 1, &callback, &ignored),
                   "napi_call_function(install component event)");
        }
    }

    bool ParseUiRule(napi_env napiEnv, napi_value spec, std::unique_ptr<UiRule> *output)
    {
        std::unique_ptr<UiRule> rule(new (std::nothrow) UiRule());
        if (!rule) {
            LogError("Cannot allocate OhosPatch UiRule");
            return false;
        }

        std::string kind;
        if (!NapiNamedString(napiEnv, spec, "kind", &kind) ||
            !NapiNamedUint32(napiEnv, spec, "ruleId", &rule->ruleId) ||
            !NapiNamedString(napiEnv, spec, "className", &rule->className) ||
            !NapiNamedString(napiEnv, spec, "modulePath", &rule->modulePath) ||
            !NapiNamedString(napiEnv, spec, "moduleInfo", &rule->moduleInfo) ||
            !NapiNamedString(napiEnv, spec, "exportName", &rule->exportName) ||
            !NapiNamedString(napiEnv, spec, "targetKey", &rule->targetKey)) {
            return false;
        }

        if (kind == "param") {
            rule->kind = UiRuleKind::PARAM;
            if (!NapiNamedString(napiEnv, spec, "propertyName", &rule->propertyName)) {
                return false;
            }
        } else if (kind == "state") {
            rule->kind = UiRuleKind::STATE;
            if (!NapiNamedString(napiEnv, spec, "propertyName", &rule->propertyName)) {
                return false;
            }
        } else if (kind == "attribute") {
            rule->kind = UiRuleKind::ATTRIBUTE;
            napi_value arguments = nullptr;
            if (!NapiNamedString(napiEnv, spec, "nodeType", &rule->nodeType) ||
                !NapiNamedUint32(napiEnv, spec, "occurrence", &rule->occurrence) ||
                !NapiNamedString(napiEnv, spec, "attributeName", &rule->attributeName) ||
                !NapiOk(napiEnv, napi_get_named_property(napiEnv, spec, "arguments", &arguments),
                        "napi_get_named_property(component attribute arguments)") ||
                !NapiJsonStringify(napiEnv, arguments, "[]", &rule->argumentsJson)) {
                return false;
            }
        } else if (kind == "event") {
            rule->kind = UiRuleKind::EVENT;
            napi_value capture = nullptr;
            uint32_t captureCount = 0;
            if (!NapiNamedString(napiEnv, spec, "nodeType", &rule->nodeType) ||
                !NapiNamedUint32(napiEnv, spec, "occurrence", &rule->occurrence) ||
                !NapiNamedString(napiEnv, spec, "eventName", &rule->eventName) ||
                !NapiOk(napiEnv, napi_get_named_property(napiEnv, spec, "capture", &capture),
                        "napi_get_named_property(component event capture)") ||
                !NapiOk(napiEnv, napi_get_array_length(napiEnv, capture, &captureCount),
                        "napi_get_array_length(component event capture)")) {
                return false;
            }
            if (captureCount > kMaxUiCaptureProperties) {
                LogError("Component event capture count exceeds the OhosPatch limit");
                return false;
            }
            rule->captureCount = captureCount;
            for (uint32_t index = 0; index < captureCount; ++index) {
                napi_value property = nullptr;
                if (!NapiOk(napiEnv, napi_get_element(napiEnv, capture, index, &property),
                            "napi_get_element(component event capture)") ||
                    !NapiString(napiEnv, property, &rule->captureProperties[index])) {
                    return false;
                }
            }
        } else {
            LogError("Unsupported component rule kind: " + kind);
            return false;
        }

        *output = std::move(rule);
        return true;
    }

    bool TargetNeedsRuleKind(const std::string &targetKey, UiRuleKind kind) const
    {
        for (size_t index = 0; index < uiRuleCount_; ++index) {
            UiRule *rule = uiRules_[index].get();
            if (rule && rule->targetKey == targetKey && rule->kind == kind) {
                return true;
            }
        }
        return false;
    }

    bool InstallUiMethod(napi_env napiEnv, UiComponentHook *component, napi_value holder, const char *methodName,
                         UiMethodKind kind)
    {
        if (component->methodCount >= component->methods.size()) {
            LogError("Component method hook count exceeds the OhosPatch limit");
            return false;
        }
        napi_value original = nullptr;
        napi_valuetype originalType = napi_undefined;
        if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, holder, methodName, &original),
                    "napi_get_named_property(component original method)") ||
            !NapiOk(napiEnv, napi_typeof(napiEnv, original, &originalType),
                    "napi_typeof(component original method)")) {
            return false;
        }
        if (originalType != napi_function) {
            LogError(component->className + "." + methodName + " is not available for component patching");
            return false;
        }

        UiMethodHook &method = component->methods[component->methodCount];
        method.component = component;
        method.kind = kind;
        method.methodName = methodName;
        if (!NapiOk(napiEnv, napi_create_reference(napiEnv, original, 1, &method.original),
                    "napi_create_reference(component original method)")) {
            return false;
        }
        napi_value trampoline = nullptr;
        if (!NapiOk(napiEnv,
                    napi_create_function(napiEnv, methodName, NAPI_AUTO_LENGTH, UiMethodCallback, &method, &trampoline),
                    "napi_create_function(component method trampoline)") ||
            !NapiOk(napiEnv, napi_set_named_property(napiEnv, holder, methodName, trampoline),
                    "napi_set_named_property(component method trampoline)")) {
            DeleteNapiReference(napiEnv, method.original, "napi_delete_reference(component original method)");
            method.original = nullptr;
            return false;
        }
        ++component->methodCount;
        return true;
    }

    bool InstallUiComponentHook(napi_env napiEnv, UiRule *rule)
    {
        if (!rule || uiComponentHookCount_ >= kMaxUiComponentHooks) {
            LogError("Component target count exceeds the OhosPatch limit");
            return false;
        }

        napi_value module = nullptr;
        napi_status loadStatus = rule->moduleInfo.empty()
                                     ? napi_load_module(napiEnv, rule->modulePath.c_str(), &module)
                                     : napi_load_module_with_info(napiEnv, rule->modulePath.c_str(),
                                                                  rule->moduleInfo.c_str(), &module);
        if (!NapiOk(napiEnv, loadStatus, "napi_load_module_with_info(component target)")) {
            return false;
        }
        napi_value constructor = nullptr;
        napi_value holder = nullptr;
        if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, module, rule->exportName.c_str(), &constructor),
                    "napi_get_named_property(component export)") ||
            !NapiOk(napiEnv, napi_get_named_property(napiEnv, constructor, "prototype", &holder),
                    "napi_get_named_property(component prototype)")) {
            return false;
        }

        std::unique_ptr<UiComponentHook> component(new (std::nothrow) UiComponentHook());
        if (!component) {
            LogError("Cannot allocate OhosPatch UiComponentHook");
            return false;
        }
        component->env = napiEnv;
        component->className = rule->className;
        component->targetKey = rule->targetKey;
        if (!NapiOk(napiEnv, napi_create_reference(napiEnv, holder, 1, &component->holder),
                    "napi_create_reference(component prototype)")) {
            return false;
        }

        UiComponentHook *componentPointer = component.get();
        uiComponentHooks_[uiComponentHookCount_++] = std::move(component);
        bool needsParam = TargetNeedsRuleKind(rule->targetKey, UiRuleKind::PARAM);
        bool needsState = TargetNeedsRuleKind(rule->targetKey, UiRuleKind::STATE);
        bool needsNodes = TargetNeedsRuleKind(rule->targetKey, UiRuleKind::ATTRIBUTE) ||
                          TargetNeedsRuleKind(rule->targetKey, UiRuleKind::EVENT);
        if (needsParam &&
            (!InstallUiMethod(napiEnv, componentPointer, holder, "setInitiallyProvidedValue",
                              UiMethodKind::PARAM_INITIAL) ||
             !InstallUiMethod(napiEnv, componentPointer, holder, "updateStateVars", UiMethodKind::PARAM_UPDATE))) {
            return false;
        }
        if ((needsState || needsNodes) &&
            !InstallUiMethod(napiEnv, componentPointer, holder, "initialRender", UiMethodKind::INITIAL_RENDER)) {
            return false;
        }
        if (needsNodes &&
            !InstallUiMethod(napiEnv, componentPointer, holder, "observeComponentCreation2",
                             UiMethodKind::OBSERVE_CREATION)) {
            return false;
        }
        return true;
    }

    bool InstallUiRules(napi_env napiEnv)
    {
        JSVM_Value specsValue = nullptr;
        std::string specsJson;
        if (!CallGlobal("__ohospatch_uiSpecs", nullptr, 0, &specsValue) || !JsvmString(specsValue, &specsJson)) {
            return false;
        }
        napi_value specs = nullptr;
        uint32_t length = 0;
        if (!NapiJsonParse(napiEnv, specsJson, &specs) ||
            !NapiOk(napiEnv, napi_get_array_length(napiEnv, specs, &length),
                    "napi_get_array_length(component patch specs)")) {
            return false;
        }
        if (length > kMaxUiRules) {
            LogError("Component patch rule count exceeds the OhosPatch limit");
            return false;
        }

        for (uint32_t index = 0; index < length; ++index) {
            napi_value spec = nullptr;
            std::unique_ptr<UiRule> rule;
            if (!NapiOk(napiEnv, napi_get_element(napiEnv, specs, index, &spec),
                        "napi_get_element(component patch spec)") ||
                !ParseUiRule(napiEnv, spec, &rule)) {
                return false;
            }
            uiRules_[uiRuleCount_++] = std::move(rule);
        }

        for (size_t index = 0; index < uiRuleCount_; ++index) {
            UiRule *rule = uiRules_[index].get();
            bool installed = false;
            for (size_t componentIndex = 0; componentIndex < uiComponentHookCount_; ++componentIndex) {
                if (uiComponentHooks_[componentIndex]->targetKey == rule->targetKey) {
                    installed = true;
                    break;
                }
            }
            if (!installed && !InstallUiComponentHook(napiEnv, rule)) {
                return false;
            }
        }
        return true;
    }

    bool RestoreUiHooks()
    {
        std::array<std::unique_ptr<UiComponentHook>, kMaxUiComponentHooks> retained;
        size_t retainedCount = 0;
        bool success = true;
        for (size_t index = uiComponentHookCount_; index > 0; --index) {
            std::unique_ptr<UiComponentHook> component = std::move(uiComponentHooks_[index - 1]);
            napi_value holder = nullptr;
            bool restored = NapiOk(component->env,
                                   napi_get_reference_value(component->env, component->holder, &holder),
                                   "napi_get_reference_value(component prototype)");
            for (size_t methodIndex = component->methodCount; restored && methodIndex > 0; --methodIndex) {
                UiMethodHook &method = component->methods[methodIndex - 1];
                napi_value original = nullptr;
                restored = NapiOk(component->env,
                                  napi_get_reference_value(component->env, method.original, &original),
                                  "napi_get_reference_value(component original method)") &&
                           NapiOk(component->env,
                                  napi_set_named_property(component->env, holder, method.methodName.c_str(), original),
                                  "napi_set_named_property(restore component method)");
            }
            if (!restored) {
                retained[retainedCount++] = std::move(component);
                success = false;
                continue;
            }
            for (size_t methodIndex = 0; methodIndex < component->methodCount; ++methodIndex) {
                DeleteNapiReference(component->env, component->methods[methodIndex].original,
                                    "napi_delete_reference(component original method)");
                component->methods[methodIndex].original = nullptr;
            }
            DeleteNapiReference(component->env, component->holder, "napi_delete_reference(component prototype)");
            component->holder = nullptr;
        }

        uiComponentHookCount_ = retainedCount;
        for (size_t index = 0; index < retainedCount; ++index) {
            uiComponentHooks_[index] = std::move(retained[index]);
        }
        return success;
    }

    void ClearUiRules()
    {
        for (size_t index = 0; index < uiRuleCount_; ++index) {
            uiRules_[index].reset();
        }
        uiRuleCount_ = 0;
        while (uiRenderDepth_ > 0) {
            PopUiRenderFrame();
        }
    }

    bool InstallNativeFunction(JSVM_Value global, const char *name, JSVM_CallbackStruct *callback)
    {
        JSVM_Value function = nullptr;
        return JsvmOk(OH_JSVM_CreateFunction(env_, name, JSVM_AUTO_LENGTH, callback, &function),
                      "OH_JSVM_CreateFunction", env_) &&
               JsvmOk(OH_JSVM_SetNamedProperty(env_, global, name, function),
                      "OH_JSVM_SetNamedProperty(native function)", env_);
    }

    bool InstallNativeFunctions()
    {
        if (!JsvmOk(OH_JSVM_SetInstanceData(env_, this, nullptr, nullptr), "OH_JSVM_SetInstanceData", env_)) {
            return false;
        }
        JSVM_Value global = nullptr;
        if (!JsvmOk(OH_JSVM_GetGlobal(env_, &global), "OH_JSVM_GetGlobal", env_)) {
            return false;
        }

        static JSVM_CallbackStruct originCallback{OriginCallback, nullptr};
        JSVM_Value originFunction = nullptr;
        if (!JsvmOk(
                OH_JSVM_CreateFunction(env_, "__ohospatch_origin", JSVM_AUTO_LENGTH, &originCallback, &originFunction),
                "OH_JSVM_CreateFunction(origin)", env_) ||
            !JsvmOk(OH_JSVM_SetNamedProperty(env_, global, "__ohospatch_origin", originFunction),
                    "OH_JSVM_SetNamedProperty(origin)", env_)) {
            return false;
        }

        static JSVM_CallbackStruct proxyGetCallback{ProxyGetCallback, nullptr};
        static JSVM_CallbackStruct proxySetCallback{ProxySetCallback, nullptr};
        static JSVM_CallbackStruct proxyCallCallback{ProxyCallCallback, nullptr};
        JSVM_Value proxyGetFunction = nullptr;
        JSVM_Value proxySetFunction = nullptr;
        JSVM_Value proxyCallFunction = nullptr;
        if (!JsvmOk(OH_JSVM_CreateFunction(env_, "__ohospatch_proxyGet", JSVM_AUTO_LENGTH, &proxyGetCallback,
                                           &proxyGetFunction),
                    "OH_JSVM_CreateFunction(proxy get)", env_) ||
            !JsvmOk(OH_JSVM_SetNamedProperty(env_, global, "__ohospatch_proxyGet", proxyGetFunction),
                    "OH_JSVM_SetNamedProperty(proxy get)", env_) ||
            !JsvmOk(OH_JSVM_CreateFunction(env_, "__ohospatch_proxySet", JSVM_AUTO_LENGTH, &proxySetCallback,
                                           &proxySetFunction),
                    "OH_JSVM_CreateFunction(proxy set)", env_) ||
            !JsvmOk(OH_JSVM_SetNamedProperty(env_, global, "__ohospatch_proxySet", proxySetFunction),
                    "OH_JSVM_SetNamedProperty(proxy set)", env_) ||
            !JsvmOk(OH_JSVM_CreateFunction(env_, "__ohospatch_proxyCall", JSVM_AUTO_LENGTH, &proxyCallCallback,
                                           &proxyCallFunction),
                    "OH_JSVM_CreateFunction(proxy call)", env_) ||
            !JsvmOk(OH_JSVM_SetNamedProperty(env_, global, "__ohospatch_proxyCall", proxyCallFunction),
                    "OH_JSVM_SetNamedProperty(proxy call)", env_)) {
            return false;
        }

        static JSVM_CallbackStruct importCallback{ImportCallback, nullptr};
        static JSVM_CallbackStruct importGetCallback{ImportGetCallback, nullptr};
        static JSVM_CallbackStruct importSetCallback{ImportSetCallback, nullptr};
        static JSVM_CallbackStruct importCallCallback{ImportCallCallback, nullptr};
        static JSVM_CallbackStruct importConstructCallback{ImportConstructCallback, nullptr};
        if (!InstallNativeFunction(global, "__ohospatch_import", &importCallback) ||
            !InstallNativeFunction(global, "__ohospatch_importGet", &importGetCallback) ||
            !InstallNativeFunction(global, "__ohospatch_importSet", &importSetCallback) ||
            !InstallNativeFunction(global, "__ohospatch_importCall", &importCallCallback) ||
            !InstallNativeFunction(global, "__ohospatch_importConstruct", &importConstructCallback)) {
            return false;
        }

        static JSVM_CallbackStruct logCallback{HiLogCallback, nullptr};
        JSVM_Value logFunction = nullptr;
        if (!JsvmOk(OH_JSVM_CreateFunction(env_, "__ohospatch_hilog", JSVM_AUTO_LENGTH, &logCallback, &logFunction),
                    "OH_JSVM_CreateFunction(hilog)", env_) ||
            !JsvmOk(OH_JSVM_SetNamedProperty(env_, global, "__ohospatch_hilog", logFunction),
                    "OH_JSVM_SetNamedProperty(hilog)", env_)) {
            return false;
        }

        static JSVM_CallbackStruct scheduleTimerCallback{ScheduleTimerCallback, nullptr};
        JSVM_Value scheduleTimerFunction = nullptr;
        if (!JsvmOk(OH_JSVM_CreateFunction(env_, "__ohospatch_scheduleTimer", JSVM_AUTO_LENGTH, &scheduleTimerCallback,
                                           &scheduleTimerFunction),
                    "OH_JSVM_CreateFunction(schedule timer)", env_) ||
            !JsvmOk(OH_JSVM_SetNamedProperty(env_, global, "__ohospatch_scheduleTimer", scheduleTimerFunction),
                    "OH_JSVM_SetNamedProperty(schedule timer)", env_)) {
            return false;
        }

        static JSVM_CallbackStruct cancelTimerCallback{CancelTimerCallback, nullptr};
        JSVM_Value cancelTimerFunction = nullptr;
        return JsvmOk(OH_JSVM_CreateFunction(env_, "__ohospatch_cancelTimer", JSVM_AUTO_LENGTH, &cancelTimerCallback,
                                             &cancelTimerFunction),
                      "OH_JSVM_CreateFunction(cancel timer)", env_) &&
               JsvmOk(OH_JSVM_SetNamedProperty(env_, global, "__ohospatch_cancelTimer", cancelTimerFunction),
                      "OH_JSVM_SetNamedProperty(cancel timer)", env_);
    }

    bool InstallNativeFunctionsWithScope()
    {
        JSVM_HandleScope scope = nullptr;
        if (!JsvmOk(OH_JSVM_OpenHandleScope(env_, &scope), "OH_JSVM_OpenHandleScope(native functions)", env_)) {
            return false;
        }
        bool installed = InstallNativeFunctions();
        bool closed = JsvmOk(OH_JSVM_CloseHandleScope(env_, scope),
                             "OH_JSVM_CloseHandleScope(native functions)", env_);
        return installed && closed;
    }

    bool Run(const std::string &script)
    {
        JSVM_HandleScope scope = nullptr;
        if (!JsvmOk(OH_JSVM_OpenHandleScope(env_, &scope), "OH_JSVM_OpenHandleScope", env_)) {
            return false;
        }

        bool success = false;
        JSVM_Value source = nullptr;
        JSVM_Script compiled = nullptr;
        JSVM_Value result = nullptr;
        if (String(script, &source) &&
            JsvmOk(OH_JSVM_CompileScript(env_, source, nullptr, 0, true, nullptr, &compiled), "OH_JSVM_CompileScript",
                   env_) &&
            JsvmOk(OH_JSVM_RunScript(env_, compiled, &result), "OH_JSVM_RunScript", env_)) {
            success = true;
        }
        bool closed = JsvmOk(OH_JSVM_CloseHandleScope(env_, scope), "OH_JSVM_CloseHandleScope", env_);
        return success && closed;
    }

    bool InstallHooks(napi_env napiEnv)
    {
        JSVM_Value specsValue = nullptr;
        std::string specsJson;
        if (!CallGlobal("__ohospatch_specs", nullptr, 0, &specsValue) || !JsvmString(specsValue, &specsJson)) {
            return false;
        }

        napi_value specs = nullptr;
        if (!NapiJsonParse(napiEnv, specsJson, &specs)) {
            return false;
        }
        uint32_t length = 0;
        if (!NapiOk(napiEnv, napi_get_array_length(napiEnv, specs, &length), "napi_get_array_length(patch specs)")) {
            return false;
        }
        if (length > kMaxHooks) {
            LogError("Patch hook count exceeds the OhosPatch limit");
            return false;
        }

        for (uint32_t index = 0; index < length; ++index) {
            napi_value spec = nullptr;
            if (!NapiOk(napiEnv, napi_get_element(napiEnv, specs, index, &spec), "napi_get_element(patch spec)") ||
                !InstallHook(napiEnv, spec)) {
                return false;
            }
        }
        return true;
    }

    bool InstallHook(napi_env napiEnv, napi_value spec)
    {
        auto stringProperty = [napiEnv, spec](const char *name, std::string *output) {
            napi_value value = nullptr;
            return NapiOk(napiEnv, napi_get_named_property(napiEnv, spec, name, &value),
                          "napi_get_named_property(patch spec)") &&
                   NapiString(napiEnv, value, output);
        };

        std::string className;
        std::string modulePath;
        std::string moduleInfo;
        std::string exportName;
        std::string targetKey;
        std::string methodName;
        if (!stringProperty("className", &className) || !stringProperty("modulePath", &modulePath) ||
            !stringProperty("moduleInfo", &moduleInfo) || !stringProperty("exportName", &exportName) ||
            !stringProperty("targetKey", &targetKey) || !stringProperty("methodName", &methodName)) {
            return false;
        }

        napi_value classMethodValue = nullptr;
        bool classMethod = false;
        if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, spec, "classMethod", &classMethodValue),
                    "napi_get_named_property(classMethod)") ||
            !NapiOk(napiEnv, napi_get_value_bool(napiEnv, classMethodValue, &classMethod),
                    "napi_get_value_bool(classMethod)")) {
            return false;
        }

        napi_value module = nullptr;
        napi_status loadStatus =
            moduleInfo.empty() ? napi_load_module(napiEnv, modulePath.c_str(), &module)
                               : napi_load_module_with_info(napiEnv, modulePath.c_str(), moduleInfo.c_str(), &module);
        if (!NapiOk(napiEnv, loadStatus, "napi_load_module_with_info(patch target)")) {
            return false;
        }

        napi_value constructor = nullptr;
        if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, module, exportName.c_str(), &constructor),
                    "napi_get_named_property(class export)")) {
            return false;
        }
        napi_value holder = constructor;
        if (!classMethod && !NapiOk(napiEnv, napi_get_named_property(napiEnv, constructor, "prototype", &holder),
                                    "napi_get_named_property(prototype)")) {
            return false;
        }

        napi_value original = nullptr;
        napi_valuetype originalType = napi_undefined;
        if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, holder, methodName.c_str(), &original),
                    "napi_get_named_property(original method)") ||
            !NapiOk(napiEnv, napi_typeof(napiEnv, original, &originalType), "napi_typeof(original method)")) {
            return false;
        }
        if (originalType != napi_function) {
            LogError(className + "." + methodName + " is not a function");
            return false;
        }

        std::unique_ptr<HookRecord> hook(new (std::nothrow) HookRecord());
        if (!hook) {
            LogError("Cannot allocate OhosPatch HookRecord");
            return false;
        }
        hook->env = napiEnv;
        hook->className = className;
        hook->targetKey = targetKey;
        hook->methodName = methodName;
        hook->classMethod = classMethod;

        if (!NapiOk(napiEnv, napi_create_reference(napiEnv, holder, 1, &hook->holder),
                    "napi_create_reference(holder)")) {
            return false;
        }
        if (!NapiOk(napiEnv, napi_create_reference(napiEnv, original, 1, &hook->original),
                    "napi_create_reference(original)")) {
            NapiOk(napiEnv, napi_delete_reference(napiEnv, hook->holder), "napi_delete_reference(holder)");
            return false;
        }

        napi_value trampoline = nullptr;
        if (!NapiOk(napiEnv,
                    napi_create_function(napiEnv, methodName.c_str(), methodName.size(), HookCallback, hook.get(),
                                         &trampoline),
                    "napi_create_function(trampoline)") ||
            !NapiOk(napiEnv, napi_set_named_property(napiEnv, holder, methodName.c_str(), trampoline),
                    "napi_set_named_property(trampoline)")) {
            NapiOk(napiEnv, napi_delete_reference(napiEnv, hook->holder), "napi_delete_reference(holder)");
            NapiOk(napiEnv, napi_delete_reference(napiEnv, hook->original), "napi_delete_reference(original)");
            return false;
        }

        hooks_[hookCount_++] = std::move(hook);
        return true;
    }

    bool RestoreHooks()
    {
        std::array<std::unique_ptr<HookRecord>, kMaxHooks> retained;
        size_t retainedCount = 0;
        bool success = true;

        for (size_t index = hookCount_; index > 0; --index) {
            std::unique_ptr<HookRecord> hook = std::move(hooks_[index - 1]);
            napi_value holder = nullptr;
            napi_value original = nullptr;
            bool restored =
                NapiOk(hook->env, napi_get_reference_value(hook->env, hook->holder, &holder),
                       "napi_get_reference_value(holder)") &&
                NapiOk(hook->env, napi_get_reference_value(hook->env, hook->original, &original),
                       "napi_get_reference_value(original)") &&
                NapiOk(hook->env, napi_set_named_property(hook->env, holder, hook->methodName.c_str(), original),
                       "napi_set_named_property(restore original)");
            if (!restored) {
                retained[retainedCount++] = std::move(hook);
                success = false;
                continue;
            }
            NapiOk(hook->env, napi_delete_reference(hook->env, hook->holder), "napi_delete_reference(holder)");
            NapiOk(hook->env, napi_delete_reference(hook->env, hook->original), "napi_delete_reference(original)");
        }

        hookCount_ = retainedCount;
        for (size_t index = 0; index < retainedCount; ++index) {
            hooks_[index] = std::move(retained[index]);
        }
        return success;
    }

    bool ClearRegistry()
    {
        bool timersCleared = CancelAllTimers();
        JSVM_HandleScope scope = nullptr;
        if (!JsvmOk(OH_JSVM_OpenHandleScope(env_, &scope), "OH_JSVM_OpenHandleScope(clear registry)", env_)) {
            return false;
        }
        JSVM_Value ignored = nullptr;
        bool registryCleared = CallGlobal("__ohospatch_clear", nullptr, 0, &ignored);
        bool closed = JsvmOk(OH_JSVM_CloseHandleScope(env_, scope),
                             "OH_JSVM_CloseHandleScope(clear registry)", env_);
        return timersCleared && registryCleared && closed;
    }

    bool CallGlobal(const char *name, const JSVM_Value *args, size_t argc, JSVM_Value *output)
    {
        JSVM_Value global = nullptr;
        JSVM_Value function = nullptr;
        return JsvmOk(OH_JSVM_GetGlobal(env_, &global), "OH_JSVM_GetGlobal", env_) &&
               JsvmOk(OH_JSVM_GetNamedProperty(env_, global, name, &function),
                      "OH_JSVM_GetNamedProperty(global function)", env_) &&
               JsvmOk(OH_JSVM_CallFunction(env_, global, function, argc, args, output), "OH_JSVM_CallFunction", env_);
    }

    bool String(const std::string &value, JSVM_Value *output)
    {
        return JsvmOk(OH_JSVM_CreateStringUtf8(env_, value.c_str(), value.size(), output), "OH_JSVM_CreateStringUtf8",
                      env_);
    }

    bool Bool(bool value, JSVM_Value *output)
    {
        return JsvmOk(OH_JSVM_GetBoolean(env_, value, output), "OH_JSVM_GetBoolean", env_);
    }

    bool ParseJson(const std::string &json, JSVM_Value *output)
    {
        JSVM_Value global = nullptr;
        JSVM_Value jsonObject = nullptr;
        JSVM_Value parse = nullptr;
        JSVM_Value source = nullptr;
        return JsvmOk(OH_JSVM_GetGlobal(env_, &global), "OH_JSVM_GetGlobal", env_) &&
               JsvmOk(OH_JSVM_GetNamedProperty(env_, global, "JSON", &jsonObject), "OH_JSVM_GetNamedProperty(JSON)",
                      env_) &&
               JsvmOk(OH_JSVM_GetNamedProperty(env_, jsonObject, "parse", &parse), "OH_JSVM_GetNamedProperty(parse)",
                      env_) &&
               String(json, &source) &&
               JsvmOk(OH_JSVM_CallFunction(env_, jsonObject, parse, 1, &source, output),
                      "OH_JSVM_CallFunction(JSON.parse)", env_);
    }

    bool StringifyJson(JSVM_Value value, std::string *output)
    {
        JSVM_Value global = nullptr;
        JSVM_Value jsonObject = nullptr;
        JSVM_Value stringify = nullptr;
        JSVM_Value result = nullptr;
        return JsvmOk(OH_JSVM_GetGlobal(env_, &global), "OH_JSVM_GetGlobal", env_) &&
               JsvmOk(OH_JSVM_GetNamedProperty(env_, global, "JSON", &jsonObject), "OH_JSVM_GetNamedProperty(JSON)",
                      env_) &&
               JsvmOk(OH_JSVM_GetNamedProperty(env_, jsonObject, "stringify", &stringify),
                      "OH_JSVM_GetNamedProperty(stringify)", env_) &&
               JsvmOk(OH_JSVM_CallFunction(env_, jsonObject, stringify, 1, &value, &result),
                      "OH_JSVM_CallFunction(JSON.stringify)", env_) &&
               JsvmString(result, output);
    }

    bool JsvmString(JSVM_Value value, std::string *output)
    {
        size_t length = 0;
        if (!JsvmOk(OH_JSVM_GetValueStringUtf8(env_, value, nullptr, 0, &length), "OH_JSVM_GetValueStringUtf8(length)",
                    env_)) {
            return false;
        }
        std::string text(length + 1, '\0');
        if (!JsvmOk(OH_JSVM_GetValueStringUtf8(env_, value, text.data(), text.size(), &length),
                    "OH_JSVM_GetValueStringUtf8(value)", env_)) {
            return false;
        }
        text.resize(length);
        *output = std::move(text);
        return true;
    }
};

JsvmRuntime &Runtime()
{
    static JsvmRuntime runtime;
    return runtime;
}

napi_value InitRuntime(napi_env env, napi_callback_info info)
{
    Runtime().Ensure();
    return NapiUndefined(env);
}

napi_value ExecuteScript(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    if (!NapiOk(env, napi_get_cb_info(env, info, &argc, args, nullptr, nullptr), "napi_get_cb_info(executeScript)") ||
        argc < 1) {
        LogError("executeScript requires a JavaScript string");
        return NapiUint32(env, 0);
    }

    std::string script;
    if (!NapiString(env, args[0], &script)) {
        return NapiUint32(env, 0);
    }
    size_t count = Runtime().ExecuteAndInstall(env, script);
    return NapiUint32(env, static_cast<uint32_t>(count));
}

napi_value Clear(napi_env env, napi_callback_info info)
{
    Runtime().Clear(env);
    return NapiUndefined(env);
}

} // namespace

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"init", nullptr, InitRuntime, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"clear", nullptr, Clear, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"executeScript", nullptr, ExecuteScript, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    NapiOk(env, napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc), "napi_define_properties");
    return exports;
}
EXTERN_C_END

static napi_module g_ohospatchModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "ohospatch",
    .nm_priv = nullptr,
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterOhosPatchModule(void) { napi_module_register(&g_ohospatchModule); }
