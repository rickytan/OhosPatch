#include "ark_runtime/jsvm.h"
#include "fixit_runtime.h"
#include "hilog/log.h"
#include "napi/native_api.h"

#include <array>
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

struct ActiveInvocation {
    napi_env env = nullptr;
    HookRecord *hook = nullptr;
    napi_value receiver = nullptr;
    bool originalExceptionPending = false;
};

class JsvmRuntime;
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
            !InstallNativeFunctions() || !Run(kFixitRuntimeScript)) {
            ResetVm();
            return false;
        }

        ready_ = true;
        return true;
    }

    size_t ExecuteAndInstall(napi_env napiEnv, const std::string &script)
    {
        if (!Ensure() || !Clear(napiEnv)) {
            return 0;
        }
        if (!Run(script)) {
            ClearRegistry();
            return 0;
        }
        if (!InstallHooks(napiEnv)) {
            RestoreHooks();
            ClearRegistry();
            return 0;
        }
        return hookCount_;
    }

    bool Clear(napi_env napiEnv)
    {
        if (!ready_) {
            return true;
        }
        if (hookCount_ > 0 && hooks_[0]->env != napiEnv) {
            LogError("OhosPatch must be cleared on the ArkTS VM where it was installed");
            return false;
        }
        bool restored = RestoreHooks();
        bool cleared = ClearRegistry();
        return restored && cleared;
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

        std::string targetJson = "{}";
        std::string argsJson;
        if ((!hook->classMethod && !NapiJsonStringify(napiEnv, receiver, "{}", &targetJson)) ||
            !NapiJsonStringify(napiEnv, argsArray, "[]", &argsJson)) {
            return callOriginal();
        }

        JSVM_Value targetKeyValue = nullptr;
        JSVM_Value methodNameValue = nullptr;
        JSVM_Value classMethodValue = nullptr;
        JSVM_Value targetValue = nullptr;
        JSVM_Value argsValue = nullptr;
        if (!String(hook->targetKey, &targetKeyValue) || !String(hook->methodName, &methodNameValue) ||
            !Bool(hook->classMethod, &classMethodValue) || !ParseJson(targetJson, &targetValue) ||
            !ParseJson(argsJson, &argsValue)) {
            return callOriginal();
        }

        JSVM_Value patchArgs[] = {targetKeyValue, methodNameValue, classMethodValue, targetValue, argsValue};
        ActiveInvocation previous = activeInvocation_;
        activeInvocation_ = {napiEnv, hook, receiver, false};
        JSVM_Value patchResult = nullptr;
        bool called = CallGlobal("__ohospatch_callPatch", patchArgs, std::size(patchArgs), &patchResult);
        bool originalExceptionPending = activeInvocation_.originalExceptionPending;
        activeInvocation_ = previous;
        if (originalExceptionPending) {
            return nullptr;
        }
        if (!called) {
            return callOriginal();
        }

        std::string resultJson;
        if (!StringifyJson(patchResult, &resultJson)) {
            return callOriginal();
        }
        napi_value envelope = nullptr;
        if (!NapiJsonParse(napiEnv, resultJson, &envelope)) {
            return callOriginal();
        }

        napi_value handledValue = nullptr;
        bool handled = false;
        if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, envelope, "handled", &handledValue),
                    "napi_get_named_property(handled)") ||
            !NapiOk(napiEnv, napi_get_value_bool(napiEnv, handledValue, &handled), "napi_get_value_bool(handled)")) {
            return callOriginal();
        }
        if (!handled) {
            return callOriginal();
        }

        if (!hook->classMethod) {
            bool hasTarget = false;
            if (NapiOk(napiEnv, napi_has_named_property(napiEnv, envelope, "target", &hasTarget),
                       "napi_has_named_property(target)") &&
                hasTarget) {
                napi_value targetPatch = nullptr;
                if (NapiOk(napiEnv, napi_get_named_property(napiEnv, envelope, "target", &targetPatch),
                           "napi_get_named_property(target)")) {
                    ApplyTargetPatch(napiEnv, receiver, targetPatch);
                }
            }
        }

        bool hasResult = false;
        if (!NapiOk(napiEnv, napi_has_named_property(napiEnv, envelope, "result", &hasResult),
                    "napi_has_named_property(result)")) {
            return callOriginal();
        }
        if (!hasResult) {
            return NapiUndefined(napiEnv);
        }

        napi_value result = nullptr;
        if (!NapiOk(napiEnv, napi_get_named_property(napiEnv, envelope, "result", &result),
                    "napi_get_named_property(result)")) {
            return callOriginal();
        }
        return result;
    }

  private:
    static constexpr size_t kMaxArguments = 64;
    static constexpr size_t kMaxHooks = 256;

    JSVM_VM vm_ = nullptr;
    JSVM_VMScope vmScope_ = nullptr;
    JSVM_Env env_ = nullptr;
    JSVM_EnvScope envScope_ = nullptr;
    bool initialized_ = false;
    bool ready_ = false;
    ActiveInvocation activeInvocation_;
    std::array<std::unique_ptr<HookRecord>, kMaxHooks> hooks_;
    size_t hookCount_ = 0;

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

    static JSVM_Value OriginCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime *runtime = Current(env);
        if (!runtime || !runtime->activeInvocation_.env || !runtime->activeInvocation_.hook) {
            LogError("Original method called outside an active OhosPatch invocation");
            return Undefined(env);
        }

        size_t argc = kMaxArguments;
        std::array<JSVM_Value, kMaxArguments> argv{};
        if (!JsvmOk(OH_JSVM_GetCbInfo(env, info, &argc, argv.data(), nullptr, nullptr), "OH_JSVM_GetCbInfo(origin)",
                    env)) {
            return Undefined(env);
        }
        if (argc > kMaxArguments) {
            LogError("Original method arguments were truncated to the OhosPatch limit");
            argc = kMaxArguments;
        }

        JSVM_Value argsArray = nullptr;
        if (!JsvmOk(OH_JSVM_CreateArrayWithLength(env, argc, &argsArray), "OH_JSVM_CreateArrayWithLength", env)) {
            return Undefined(env);
        }
        for (size_t index = 0; index < argc; ++index) {
            if (!JsvmOk(OH_JSVM_SetElement(env, argsArray, static_cast<uint32_t>(index), argv[index]),
                        "OH_JSVM_SetElement(origin argument)", env)) {
                return Undefined(env);
            }
        }

        std::string argsJson;
        if (!runtime->StringifyJson(argsArray, &argsJson)) {
            return Undefined(env);
        }

        ActiveInvocation &active = runtime->activeInvocation_;
        napi_value napiArgsArray = nullptr;
        if (!NapiJsonParse(active.env, argsJson, &napiArgsArray)) {
            return Undefined(env);
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
            return Undefined(env);
        }

        napi_valuetype type = napi_undefined;
        if (!NapiOk(active.env, napi_typeof(active.env, result, &type), "napi_typeof(origin result)")) {
            return Undefined(env);
        }
        if (type == napi_undefined) {
            return Undefined(env);
        }

        std::string resultJson;
        JSVM_Value jsvmResult = nullptr;
        if (!NapiJsonStringify(active.env, result, "null", &resultJson) ||
            !runtime->ParseJson(resultJson, &jsvmResult)) {
            return Undefined(env);
        }
        return jsvmResult;
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

    bool InstallNativeFunctions()
    {
        if (!JsvmOk(OH_JSVM_SetInstanceData(env_, this, nullptr, nullptr), "OH_JSVM_SetInstanceData", env_)) {
            return false;
        }
        JSVM_Value global = nullptr;
        if (!JsvmOk(OH_JSVM_GetGlobal(env_, &global), "OH_JSVM_GetGlobal", env_)) {
            return false;
        }

        JSVM_CallbackStruct originCallback{OriginCallback, nullptr};
        JSVM_Value originFunction = nullptr;
        if (!JsvmOk(
                OH_JSVM_CreateFunction(env_, "__ohospatch_origin", JSVM_AUTO_LENGTH, &originCallback, &originFunction),
                "OH_JSVM_CreateFunction(origin)", env_) ||
            !JsvmOk(OH_JSVM_SetNamedProperty(env_, global, "__ohospatch_origin", originFunction),
                    "OH_JSVM_SetNamedProperty(origin)", env_)) {
            return false;
        }

        JSVM_CallbackStruct logCallback{HiLogCallback, nullptr};
        JSVM_Value logFunction = nullptr;
        return JsvmOk(OH_JSVM_CreateFunction(env_, "__ohospatch_hilog", JSVM_AUTO_LENGTH, &logCallback, &logFunction),
                      "OH_JSVM_CreateFunction(hilog)", env_) &&
               JsvmOk(OH_JSVM_SetNamedProperty(env_, global, "__ohospatch_hilog", logFunction),
                      "OH_JSVM_SetNamedProperty(hilog)", env_);
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
        JSVM_Value ignored = nullptr;
        return CallGlobal("__ohospatch_clear", nullptr, 0, &ignored);
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
