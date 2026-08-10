#include "ark_runtime/jsvm.h"
#include "fixit_runtime.h"
#include "hilog/log.h"
#include "napi/native_api.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void CheckNapi(napi_status status, const char* operation)
{
    if (status == napi_ok) {
        return;
    }
    throw std::runtime_error(std::string(operation) + " failed with status " + std::to_string(status));
}

std::string NapiString(napi_env env, napi_value value)
{
    size_t length = 0;
    CheckNapi(napi_get_value_string_utf8(env, value, nullptr, 0, &length), "napi_get_value_string_utf8(length)");
    std::string result(length + 1, '\0');
    CheckNapi(napi_get_value_string_utf8(env, value, result.data(), result.size(), &length),
        "napi_get_value_string_utf8(value)");
    result.resize(length);
    return result;
}

napi_value NapiJsonFunction(napi_env env, const char* name, napi_value* jsonObject)
{
    napi_value global = nullptr;
    CheckNapi(napi_get_global(env, &global), "napi_get_global");
    CheckNapi(napi_get_named_property(env, global, "JSON", jsonObject), "napi_get_named_property(JSON)");
    napi_value function = nullptr;
    CheckNapi(napi_get_named_property(env, *jsonObject, name, &function), "napi_get_named_property(JSON function)");
    return function;
}

std::string NapiJsonStringify(napi_env env, napi_value value, const char* undefinedFallback)
{
    napi_value jsonObject = nullptr;
    napi_value stringify = NapiJsonFunction(env, "stringify", &jsonObject);
    napi_value result = nullptr;
    CheckNapi(napi_call_function(env, jsonObject, stringify, 1, &value, &result), "napi_call_function(JSON.stringify)");

    napi_valuetype type = napi_undefined;
    CheckNapi(napi_typeof(env, result, &type), "napi_typeof(JSON.stringify result)");
    return type == napi_string ? NapiString(env, result) : std::string(undefinedFallback);
}

napi_value NapiJsonParse(napi_env env, const std::string& json)
{
    napi_value jsonObject = nullptr;
    napi_value parse = NapiJsonFunction(env, "parse", &jsonObject);
    napi_value source = nullptr;
    CheckNapi(napi_create_string_utf8(env, json.c_str(), json.size(), &source), "napi_create_string_utf8(JSON)");
    napi_value result = nullptr;
    CheckNapi(napi_call_function(env, jsonObject, parse, 1, &source, &result), "napi_call_function(JSON.parse)");
    return result;
}

std::string LastNapiExceptionMessage(napi_env env)
{
    bool pending = false;
    napi_is_exception_pending(env, &pending);
    if (!pending) {
        return "ArkTS method threw an exception";
    }

    napi_value exception = nullptr;
    napi_get_and_clear_last_exception(env, &exception);
    napi_value message = nullptr;
    if (napi_get_named_property(env, exception, "message", &message) != napi_ok) {
        return "ArkTS method threw an exception";
    }
    napi_value text = nullptr;
    if (napi_coerce_to_string(env, message, &text) != napi_ok) {
        return "ArkTS method threw an exception";
    }
    return NapiString(env, text);
}

struct HookRecord {
    napi_env env = nullptr;
    napi_ref holder = nullptr;
    napi_ref original = nullptr;
    std::string className;
    std::string methodName;
    bool classMethod = false;
};

struct ActiveInvocation {
    napi_env env = nullptr;
    HookRecord* hook = nullptr;
    napi_value receiver = nullptr;
};

class JsvmRuntime;
JsvmRuntime& Runtime();

class JsvmRuntime {
public:
    void Ensure()
    {
        if (ready_) {
            return;
        }

        JSVM_InitOptions initOptions {};
        Check(OH_JSVM_Init(&initOptions), "OH_JSVM_Init");

        JSVM_CreateVMOptions vmOptions {};
        Check(OH_JSVM_CreateVM(&vmOptions, &vm_), "OH_JSVM_CreateVM");
        Check(OH_JSVM_OpenVMScope(vm_, &vmScope_), "OH_JSVM_OpenVMScope");
        Check(OH_JSVM_CreateEnv(vm_, 0, nullptr, &env_), "OH_JSVM_CreateEnv");
        Check(OH_JSVM_OpenEnvScope(env_, &envScope_), "OH_JSVM_OpenEnvScope");

        InstallNativeFunctions();
        Run(kFixitRuntimeScript);
        ready_ = true;
    }

    size_t ExecuteAndInstall(napi_env napiEnv, const std::string& script)
    {
        Ensure();
        Clear(napiEnv);
        Run(script);
        try {
            InstallHooks(napiEnv);
        } catch (...) {
            RestoreHooks();
            ClearRegistry();
            throw;
        }
        return hooks_.size();
    }

    void Clear(napi_env napiEnv)
    {
        if (!ready_) {
            return;
        }
        if (!hooks_.empty() && hooks_.front()->env != napiEnv) {
            throw std::runtime_error("OhosPatch must be cleared on the ArkTS VM where it was installed");
        }
        RestoreHooks();
        ClearRegistry();
    }

    napi_value InvokeHook(
        napi_env napiEnv, HookRecord* hook, napi_value receiver, size_t argc, const napi_value* argv)
    {
        Ensure();

        napi_value argsArray = nullptr;
        CheckNapi(napi_create_array_with_length(napiEnv, argc, &argsArray), "napi_create_array_with_length");
        for (size_t index = 0; index < argc; ++index) {
            CheckNapi(napi_set_element(napiEnv, argsArray, static_cast<uint32_t>(index), argv[index]),
                "napi_set_element(arguments)");
        }

        std::string targetJson = hook->classMethod ? "{}" : NapiJsonStringify(napiEnv, receiver, "{}");
        std::string argsJson = NapiJsonStringify(napiEnv, argsArray, "[]");

        ActiveInvocation previous = activeInvocation_;
        activeInvocation_ = { napiEnv, hook, receiver };
        std::string resultJson;
        try {
            JSVM_Value result = CallGlobal("__ohospatch_callPatch", {
                String(hook->className),
                String(hook->methodName),
                Bool(hook->classMethod),
                ParseJson(targetJson),
                ParseJson(argsJson),
            });
            resultJson = StringifyJson(result);
            activeInvocation_ = previous;
        } catch (...) {
            activeInvocation_ = previous;
            throw;
        }

        napi_value envelope = NapiJsonParse(napiEnv, resultJson);
        napi_value handledValue = nullptr;
        CheckNapi(napi_get_named_property(napiEnv, envelope, "handled", &handledValue),
            "napi_get_named_property(handled)");
        bool handled = false;
        CheckNapi(napi_get_value_bool(napiEnv, handledValue, &handled), "napi_get_value_bool(handled)");
        if (!handled) {
            return CallOriginal(napiEnv, hook, receiver, argc, argv);
        }

        if (!hook->classMethod) {
            bool hasTarget = false;
            CheckNapi(napi_has_named_property(napiEnv, envelope, "target", &hasTarget),
                "napi_has_named_property(target)");
            if (hasTarget) {
                napi_value targetPatch = nullptr;
                CheckNapi(napi_get_named_property(napiEnv, envelope, "target", &targetPatch),
                    "napi_get_named_property(target)");
                ApplyTargetPatch(napiEnv, receiver, targetPatch);
            }
        }

        bool hasResult = false;
        CheckNapi(napi_has_named_property(napiEnv, envelope, "result", &hasResult),
            "napi_has_named_property(result)");
        if (!hasResult) {
            napi_value undefined = nullptr;
            CheckNapi(napi_get_undefined(napiEnv, &undefined), "napi_get_undefined");
            return undefined;
        }

        napi_value result = nullptr;
        CheckNapi(napi_get_named_property(napiEnv, envelope, "result", &result),
            "napi_get_named_property(result)");
        return result;
    }

private:
    JSVM_VM vm_ = nullptr;
    JSVM_VMScope vmScope_ = nullptr;
    JSVM_Env env_ = nullptr;
    JSVM_EnvScope envScope_ = nullptr;
    bool ready_ = false;
    ActiveInvocation activeInvocation_;
    std::vector<std::unique_ptr<HookRecord>> hooks_;

    static JsvmRuntime* Current(JSVM_Env env)
    {
        void* data = nullptr;
        OH_JSVM_GetInstanceData(env, &data);
        return static_cast<JsvmRuntime*>(data);
    }

    static JSVM_Value OriginCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime* runtime = Current(env);
        if (!runtime || !runtime->activeInvocation_.env || !runtime->activeInvocation_.hook) {
            OH_JSVM_ThrowError(env, nullptr, "No active ArkTS invocation");
            return Undefined(env);
        }

        size_t argc = kMaxArguments;
        std::vector<JSVM_Value> argv(kMaxArguments);
        OH_JSVM_GetCbInfo(env, info, &argc, argv.data(), nullptr, nullptr);

        JSVM_Value argsArray = nullptr;
        OH_JSVM_CreateArrayWithLength(env, argc, &argsArray);
        for (size_t index = 0; index < argc; ++index) {
            OH_JSVM_SetElement(env, argsArray, static_cast<uint32_t>(index), argv[index]);
        }

        try {
            std::string argsJson = runtime->StringifyJson(argsArray);
            ActiveInvocation& active = runtime->activeInvocation_;
            napi_value napiArgsArray = NapiJsonParse(active.env, argsJson);
            uint32_t napiArgc = 0;
            CheckNapi(napi_get_array_length(active.env, napiArgsArray, &napiArgc), "napi_get_array_length(origin args)");
            std::vector<napi_value> napiArgv(napiArgc);
            for (uint32_t index = 0; index < napiArgc; ++index) {
                CheckNapi(napi_get_element(active.env, napiArgsArray, index, &napiArgv[index]),
                    "napi_get_element(origin args)");
            }

            napi_value result = CallOriginal(active.env, active.hook, active.receiver, napiArgv.size(), napiArgv.data());
            napi_valuetype type = napi_undefined;
            CheckNapi(napi_typeof(active.env, result, &type), "napi_typeof(origin result)");
            if (type == napi_undefined) {
                return Undefined(env);
            }
            return runtime->ParseJson(NapiJsonStringify(active.env, result, "null"));
        } catch (const std::exception& error) {
            OH_JSVM_ThrowError(env, nullptr, error.what());
            return Undefined(env);
        }
    }

    static JSVM_Value LogCallback(JSVM_Env env, JSVM_CallbackInfo info)
    {
        JsvmRuntime* runtime = Current(env);
        if (!runtime) {
            return Undefined(env);
        }

        size_t argc = 2;
        JSVM_Value argv[2] = { nullptr, nullptr };
        OH_JSVM_GetCbInfo(env, info, &argc, argv, nullptr, nullptr);
        if (argc < 2) {
            return Undefined(env);
        }

        std::string level = runtime->JsvmString(argv[0]);
        std::string message = runtime->JsvmString(argv[1]);
        LogLevel logLevel = LOG_INFO;
        if (level == "debug") {
            logLevel = LOG_DEBUG;
        } else if (level == "warn") {
            logLevel = LOG_WARN;
        } else if (level == "error") {
            logLevel = LOG_ERROR;
        }
        OH_LOG_Print(LOG_APP, logLevel, 0xD003900, "OhosPatch", "%{public}s", message.c_str());
        return Undefined(env);
    }

    static napi_value HookCallback(napi_env env, napi_callback_info info)
    {
        try {
            size_t argc = kMaxArguments;
            napi_value receiver = nullptr;
            void* data = nullptr;
            std::vector<napi_value> argv(kMaxArguments);
            CheckNapi(napi_get_cb_info(env, info, &argc, argv.data(), &receiver, &data), "napi_get_cb_info(values)");
            return Runtime().InvokeHook(env, static_cast<HookRecord*>(data), receiver, argc, argv.data());
        } catch (const std::exception& error) {
            napi_throw_error(env, nullptr, error.what());
            return nullptr;
        }
    }

    static JSVM_Value Undefined(JSVM_Env env)
    {
        JSVM_Value value = nullptr;
        OH_JSVM_GetUndefined(env, &value);
        return value;
    }

    static constexpr size_t kMaxArguments = 64;

    static napi_value CallOriginal(
        napi_env env, HookRecord* hook, napi_value receiver, size_t argc, const napi_value* argv)
    {
        napi_value original = nullptr;
        CheckNapi(napi_get_reference_value(env, hook->original, &original), "napi_get_reference_value(original)");
        napi_value result = nullptr;
        napi_status status = napi_call_function(env, receiver, original, argc, argv, &result);
        if (status == napi_pending_exception) {
            throw std::runtime_error(LastNapiExceptionMessage(env));
        }
        CheckNapi(status, "napi_call_function(original)");
        return result;
    }

    static void ApplyTargetPatch(napi_env env, napi_value target, napi_value patch)
    {
        napi_value keys = nullptr;
        CheckNapi(napi_get_property_names(env, patch, &keys), "napi_get_property_names(target patch)");
        uint32_t length = 0;
        CheckNapi(napi_get_array_length(env, keys, &length), "napi_get_array_length(target keys)");
        for (uint32_t index = 0; index < length; ++index) {
            napi_value key = nullptr;
            napi_value value = nullptr;
            CheckNapi(napi_get_element(env, keys, index, &key), "napi_get_element(target key)");
            CheckNapi(napi_get_property(env, patch, key, &value), "napi_get_property(target value)");
            CheckNapi(napi_set_property(env, target, key, value), "napi_set_property(target value)");
        }
    }

    static void Check(JSVM_Status status, const char* operation)
    {
        if (status == JSVM_OK) {
            return;
        }
        throw std::runtime_error(std::string(operation) + " failed with status " + std::to_string(status));
    }

    void InstallNativeFunctions()
    {
        Check(OH_JSVM_SetInstanceData(env_, this, nullptr, nullptr), "OH_JSVM_SetInstanceData");
        JSVM_Value global = nullptr;
        Check(OH_JSVM_GetGlobal(env_, &global), "OH_JSVM_GetGlobal");

        JSVM_CallbackStruct originCallback { OriginCallback, nullptr };
        JSVM_Value originFunction = nullptr;
        Check(OH_JSVM_CreateFunction(env_, "__ohospatch_origin", JSVM_AUTO_LENGTH, &originCallback, &originFunction),
            "OH_JSVM_CreateFunction(origin)");
        Check(OH_JSVM_SetNamedProperty(env_, global, "__ohospatch_origin", originFunction),
            "OH_JSVM_SetNamedProperty(origin)");

        JSVM_CallbackStruct logCallback { LogCallback, nullptr };
        JSVM_Value logFunction = nullptr;
        Check(OH_JSVM_CreateFunction(env_, "__ohospatch_log", JSVM_AUTO_LENGTH, &logCallback, &logFunction),
            "OH_JSVM_CreateFunction(log)");
        Check(OH_JSVM_SetNamedProperty(env_, global, "__ohospatch_log", logFunction),
            "OH_JSVM_SetNamedProperty(log)");
    }

    void Run(const std::string& script)
    {
        JSVM_HandleScope scope = nullptr;
        Check(OH_JSVM_OpenHandleScope(env_, &scope), "OH_JSVM_OpenHandleScope");
        try {
            JSVM_Value source = String(script);
            JSVM_Script compiled = nullptr;
            Check(OH_JSVM_CompileScript(env_, source, nullptr, 0, true, nullptr, &compiled), "OH_JSVM_CompileScript");
            JSVM_Value result = nullptr;
            Check(OH_JSVM_RunScript(env_, compiled, &result), "OH_JSVM_RunScript");
            Check(OH_JSVM_CloseHandleScope(env_, scope), "OH_JSVM_CloseHandleScope");
        } catch (...) {
            OH_JSVM_CloseHandleScope(env_, scope);
            throw;
        }
    }

    void InstallHooks(napi_env napiEnv)
    {
        std::string specsJson = JsvmString(CallGlobal("__ohospatch_specs", {}));
        napi_value specs = NapiJsonParse(napiEnv, specsJson);
        uint32_t length = 0;
        CheckNapi(napi_get_array_length(napiEnv, specs, &length), "napi_get_array_length(patch specs)");

        for (uint32_t index = 0; index < length; ++index) {
            napi_value spec = nullptr;
            CheckNapi(napi_get_element(napiEnv, specs, index, &spec), "napi_get_element(patch spec)");
            InstallHook(napiEnv, spec);
        }
    }

    void InstallHook(napi_env napiEnv, napi_value spec)
    {
        auto stringProperty = [napiEnv, spec](const char* name) {
            napi_value value = nullptr;
            CheckNapi(napi_get_named_property(napiEnv, spec, name, &value), "napi_get_named_property(patch spec)");
            return NapiString(napiEnv, value);
        };

        std::string className = stringProperty("className");
        std::string modulePath = stringProperty("modulePath");
        std::string moduleInfo = stringProperty("moduleInfo");
        std::string exportName = stringProperty("exportName");
        std::string methodName = stringProperty("methodName");
        napi_value classMethodValue = nullptr;
        CheckNapi(napi_get_named_property(napiEnv, spec, "classMethod", &classMethodValue),
            "napi_get_named_property(classMethod)");
        bool classMethod = false;
        CheckNapi(napi_get_value_bool(napiEnv, classMethodValue, &classMethod), "napi_get_value_bool(classMethod)");

        napi_value module = nullptr;
        napi_status loadStatus = moduleInfo.empty()
            ? napi_load_module(napiEnv, modulePath.c_str(), &module)
            : napi_load_module_with_info(napiEnv, modulePath.c_str(), moduleInfo.c_str(), &module);
        CheckNapi(loadStatus, "napi_load_module_with_info(patch target)");
        napi_value constructor = nullptr;
        CheckNapi(napi_get_named_property(napiEnv, module, exportName.c_str(), &constructor),
            "napi_get_named_property(class export)");
        napi_value holder = constructor;
        if (!classMethod) {
            CheckNapi(napi_get_named_property(napiEnv, constructor, "prototype", &holder),
                "napi_get_named_property(prototype)");
        }

        napi_value original = nullptr;
        CheckNapi(napi_get_named_property(napiEnv, holder, methodName.c_str(), &original),
            "napi_get_named_property(original method)");
        napi_valuetype originalType = napi_undefined;
        CheckNapi(napi_typeof(napiEnv, original, &originalType), "napi_typeof(original method)");
        if (originalType != napi_function) {
            throw std::runtime_error(className + "." + methodName + " is not a function");
        }

        auto hook = std::make_unique<HookRecord>();
        hook->env = napiEnv;
        hook->className = className;
        hook->methodName = methodName;
        hook->classMethod = classMethod;
        CheckNapi(napi_create_reference(napiEnv, holder, 1, &hook->holder), "napi_create_reference(holder)");
        CheckNapi(napi_create_reference(napiEnv, original, 1, &hook->original), "napi_create_reference(original)");

        napi_value trampoline = nullptr;
        CheckNapi(napi_create_function(napiEnv, methodName.c_str(), methodName.size(), HookCallback, hook.get(), &trampoline),
            "napi_create_function(trampoline)");
        CheckNapi(napi_set_named_property(napiEnv, holder, methodName.c_str(), trampoline),
            "napi_set_named_property(trampoline)");
        hooks_.push_back(std::move(hook));
    }

    void RestoreHooks()
    {
        for (auto iterator = hooks_.rbegin(); iterator != hooks_.rend(); ++iterator) {
            HookRecord* hook = iterator->get();
            napi_value holder = nullptr;
            napi_value original = nullptr;
            napi_get_reference_value(hook->env, hook->holder, &holder);
            napi_get_reference_value(hook->env, hook->original, &original);
            napi_set_named_property(hook->env, holder, hook->methodName.c_str(), original);
            napi_delete_reference(hook->env, hook->holder);
            napi_delete_reference(hook->env, hook->original);
        }
        hooks_.clear();
    }

    void ClearRegistry()
    {
        CallGlobal("__ohospatch_clear", {});
    }

    JSVM_Value CallGlobal(const char* name, std::initializer_list<JSVM_Value> args)
    {
        JSVM_Value global = nullptr;
        Check(OH_JSVM_GetGlobal(env_, &global), "OH_JSVM_GetGlobal");
        JSVM_Value function = nullptr;
        Check(OH_JSVM_GetNamedProperty(env_, global, name, &function), "OH_JSVM_GetNamedProperty(global function)");
        JSVM_Value result = nullptr;
        Check(OH_JSVM_CallFunction(env_, global, function, args.size(), args.begin(), &result), "OH_JSVM_CallFunction");
        return result;
    }

    JSVM_Value String(const std::string& value)
    {
        JSVM_Value result = nullptr;
        Check(OH_JSVM_CreateStringUtf8(env_, value.c_str(), value.size(), &result), "OH_JSVM_CreateStringUtf8");
        return result;
    }

    JSVM_Value Bool(bool value)
    {
        JSVM_Value result = nullptr;
        Check(OH_JSVM_GetBoolean(env_, value, &result), "OH_JSVM_GetBoolean");
        return result;
    }

    JSVM_Value ParseJson(const std::string& json)
    {
        JSVM_Value global = nullptr;
        Check(OH_JSVM_GetGlobal(env_, &global), "OH_JSVM_GetGlobal");
        JSVM_Value jsonObject = nullptr;
        Check(OH_JSVM_GetNamedProperty(env_, global, "JSON", &jsonObject), "OH_JSVM_GetNamedProperty(JSON)");
        JSVM_Value parse = nullptr;
        Check(OH_JSVM_GetNamedProperty(env_, jsonObject, "parse", &parse), "OH_JSVM_GetNamedProperty(parse)");
        JSVM_Value source = String(json);
        JSVM_Value result = nullptr;
        Check(OH_JSVM_CallFunction(env_, jsonObject, parse, 1, &source, &result), "OH_JSVM_CallFunction(JSON.parse)");
        return result;
    }

    std::string StringifyJson(JSVM_Value value)
    {
        JSVM_Value global = nullptr;
        Check(OH_JSVM_GetGlobal(env_, &global), "OH_JSVM_GetGlobal");
        JSVM_Value jsonObject = nullptr;
        Check(OH_JSVM_GetNamedProperty(env_, global, "JSON", &jsonObject), "OH_JSVM_GetNamedProperty(JSON)");
        JSVM_Value stringify = nullptr;
        Check(OH_JSVM_GetNamedProperty(env_, jsonObject, "stringify", &stringify),
            "OH_JSVM_GetNamedProperty(stringify)");
        JSVM_Value result = nullptr;
        Check(OH_JSVM_CallFunction(env_, jsonObject, stringify, 1, &value, &result),
            "OH_JSVM_CallFunction(JSON.stringify)");
        return JsvmString(result);
    }

    std::string JsvmString(JSVM_Value value)
    {
        size_t length = 0;
        Check(OH_JSVM_GetValueStringUtf8(env_, value, nullptr, 0, &length), "OH_JSVM_GetValueStringUtf8(length)");
        std::string output(length + 1, '\0');
        Check(OH_JSVM_GetValueStringUtf8(env_, value, output.data(), output.size(), &length),
            "OH_JSVM_GetValueStringUtf8(value)");
        output.resize(length);
        return output;
    }
};

JsvmRuntime& Runtime()
{
    static JsvmRuntime runtime;
    return runtime;
}

napi_value Throw(napi_env env, const std::exception& error)
{
    napi_throw_error(env, nullptr, error.what());
    return nullptr;
}

napi_value InitRuntime(napi_env env, napi_callback_info info)
{
    try {
        Runtime().Ensure();
        napi_value undefined = nullptr;
        napi_get_undefined(env, &undefined);
        return undefined;
    } catch (const std::exception& error) {
        return Throw(env, error);
    }
}

napi_value ExecuteScript(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = { nullptr };
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    try {
        size_t count = Runtime().ExecuteAndInstall(env, NapiString(env, args[0]));
        napi_value result = nullptr;
        napi_create_uint32(env, static_cast<uint32_t>(count), &result);
        return result;
    } catch (const std::exception& error) {
        return Throw(env, error);
    }
}

napi_value Clear(napi_env env, napi_callback_info info)
{
    try {
        Runtime().Clear(env);
        napi_value undefined = nullptr;
        napi_get_undefined(env, &undefined);
        return undefined;
    } catch (const std::exception& error) {
        return Throw(env, error);
    }
}

} // namespace

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        { "init", nullptr, InitRuntime, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "clear", nullptr, Clear, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "executeScript", nullptr, ExecuteScript, nullptr, nullptr, nullptr, napi_default, nullptr },
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
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
    .reserved = { 0 },
};

extern "C" __attribute__((constructor)) void RegisterOhosPatchModule(void)
{
    napi_module_register(&g_ohospatchModule);
}
