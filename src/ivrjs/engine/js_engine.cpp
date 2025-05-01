#include <fmus/ivrjs/engine.hpp>
#include <stdexcept>
#include <memory>
#include <vector>
#include <unordered_map>

// Include QuickJS header
#include <quickjs.h>

namespace fmus::ivrjs {

class JSEngineImpl : public JSEngine {
public:
    JSEngineImpl() {
        // Initialize QuickJS runtime
        runtime_ = JS_NewRuntime();
        if (!runtime_) {
            throw std::runtime_error("Failed to create JavaScript runtime");
        }

        // Configure runtime
        JS_SetMemoryLimit(runtime_, memory_limit_);
        JS_SetMaxStackSize(runtime_, stack_size_);
    }

    ~JSEngineImpl() override {
        // Free all contexts
        contexts_.clear();

        // Free runtime
        if (runtime_) {
            JS_FreeRuntime(runtime_);
            runtime_ = nullptr;
        }
    }

    std::shared_ptr<JSContext> createContext(const std::string& name) override {
        // Create a new QuickJS context
        JSContext* ctx = JS_NewContext(runtime_);
        if (!ctx) {
            throw std::runtime_error("Failed to create JavaScript context");
        }

        // Create our context wrapper
        auto context = std::make_shared<JSContextImpl>(name, ctx, this);

        // Store in our contexts map
        contexts_[name] = context;

        return context;
    }

    std::shared_ptr<JSContext> getContext(const std::string& name) const override {
        auto it = contexts_.find(name);
        if (it != contexts_.end()) {
            return it->second;
        }
        return nullptr;
    }

    void removeContext(const std::string& name) override {
        contexts_.erase(name);
    }

    void setMemoryLimit(size_t limit) override {
        memory_limit_ = limit;
        if (runtime_) {
            JS_SetMemoryLimit(runtime_, memory_limit_);
        }
    }

    size_t getMemoryLimit() const override {
        return memory_limit_;
    }

    size_t getMemoryUsage() const override {
        if (runtime_) {
            return JS_GetMemoryUsage(runtime_, nullptr);
        }
        return 0;
    }

    void garbageCollect() override {
        if (runtime_) {
            JS_RunGC(runtime_);
        }
    }

private:
    // Implementation of JSContext that uses QuickJS
    class JSContextImpl : public JSContext {
    public:
        JSContextImpl(const std::string& name, JSContext* ctx, JSEngineImpl* engine)
            : name_(name), ctx_(ctx), engine_(engine) {

            // Register standard modules
            registerStandardModules();
        }

        ~JSContextImpl() override {
            // Free any context-specific resources

            // Free the QuickJS context
            if (ctx_) {
                JS_FreeContext(ctx_);
                ctx_ = nullptr;
            }
        }

        // JSContext interface
        std::string name() const override {
            return name_;
        }

        JSValue evaluateScript(const std::string& script, const std::string& filename) override {
            // Execute script in QuickJS
            JSValue result = JS_Eval(ctx_, script.c_str(), script.length(),
                                     filename.c_str(), JS_EVAL_TYPE_GLOBAL);

            // Check for errors
            if (JS_IsException(result)) {
                // Get and format the error
                JSValue error = JS_GetException(ctx_);
                std::string error_msg = formatError(error);
                JS_FreeValue(ctx_, error);

                throw JSException(error_msg);
            }

            // Wrap and return the value
            return wrapValue(result);
        }

        JSValue evaluateModule(const std::string& module_code, const std::string& module_name) override {
            // Create a module object
            JSValue module_obj = JS_NewObject(ctx_);

            // For now, we'll just evaluate as global code
            // In a real implementation, we'd use JS_Eval with proper module flags
            // and properly handle module imports

            return evaluateScript(module_code, module_name);
        }

        JSValue callFunction(const JSValue& function, const std::vector<JSValue>& args) override {
            // Unwrap the function value
            JSValue js_func = unwrapValue(function);

            // Check if it's actually a function
            if (!JS_IsFunction(ctx_, js_func)) {
                throw JSException("Value is not a function");
            }

            // Unwrap arguments
            std::vector<JSValue> js_args;
            js_args.reserve(args.size());
            for (const auto& arg : args) {
                js_args.push_back(unwrapValue(arg));
            }

            // Call the function
            JSValue this_val = JS_UNDEFINED;
            JSValue result = JS_Call(ctx_, js_func, this_val,
                                     js_args.size(), js_args.data());

            // Check for errors
            if (JS_IsException(result)) {
                JSValue error = JS_GetException(ctx_);
                std::string error_msg = formatError(error);
                JS_FreeValue(ctx_, error);

                throw JSException(error_msg);
            }

            // Free the arguments
            for (auto& arg : js_args) {
                JS_FreeValue(ctx_, arg);
            }

            // Wrap and return the result
            return wrapValue(result);
        }

        JSValue getGlobalObject() override {
            JSValue global = JS_GetGlobalObject(ctx_);
            return wrapValue(global);
        }

        JSValue createObject() override {
            JSValue obj = JS_NewObject(ctx_);
            return wrapValue(obj);
        }

        JSValue createArray() override {
            JSValue array = JS_NewArray(ctx_);
            return wrapValue(array);
        }

        JSValue createString(const std::string& str) override {
            JSValue js_str = JS_NewStringLen(ctx_, str.c_str(), str.length());
            return wrapValue(js_str);
        }

        JSValue createNumber(double value) override {
            JSValue js_num = JS_NewFloat64(ctx_, value);
            return wrapValue(js_num);
        }

        JSValue createBoolean(bool value) override {
            JSValue js_bool = JS_NewBool(ctx_, value ? 1 : 0);
            return wrapValue(js_bool);
        }

        JSValue createNull() override {
            JSValue js_null = JS_NULL;
            return wrapValue(js_null);
        }

        JSValue createUndefined() override {
            JSValue js_undefined = JS_UNDEFINED;
            return wrapValue(js_undefined);
        }

        JSValue createFunction(const JSNativeFunction& func, const std::string& name) override {
            // Create a native function wrapper that calls our callback
            // Store the callback in our map for later use
            size_t func_id = next_func_id_++;
            native_functions_[func_id] = func;

            // Create a QuickJS function that will call our native function
            JSValue js_func = JS_NewCFunction(ctx_,
                [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
                    // Get the function ID from the function data
                    size_t func_id = JS_GetOpaque(
                        JS_GetPropertyStr(ctx, this_val, "__func_id"), 0);

                    // Get the context impl from the context
                    auto context_impl = static_cast<JSContextImpl*>(
                        JS_GetContextOpaque(ctx));

                    // Find the native function
                    auto func_it = context_impl->native_functions_.find(func_id);
                    if (func_it == context_impl->native_functions_.end()) {
                        return JS_ThrowInternalError(ctx, "Native function not found");
                    }

                    // Wrap arguments
                    std::vector<JSValue> wrapped_args;
                    wrapped_args.reserve(argc);
                    for (int i = 0; i < argc; i++) {
                        wrapped_args.push_back(context_impl->wrapValue(
                            JS_DupValue(ctx, argv[i])));
                    }

                    try {
                        // Call the native function
                        JSValue result = func_it->second(wrapped_args);

                        // Unwrap and return the result
                        return context_impl->unwrapValue(result);
                    } catch (const std::exception& ex) {
                        // Handle any C++ exceptions
                        return JS_ThrowInternalError(ctx, ex.what());
                    }
                },
                name.c_str(), 0);

            // Store the function ID in a property of the function
            JS_SetPropertyStr(ctx_, js_func, "__func_id",
                              JS_NewInt64(ctx_, static_cast<int64_t>(func_id)));

            return wrapValue(js_func);
        }

        JSValue getProperty(const JSValue& object, const std::string& key) override {
            JSValue js_obj = unwrapValue(object);
            JSValue js_prop = JS_GetPropertyStr(ctx_, js_obj, key.c_str());

            // Check for errors
            if (JS_IsException(js_prop)) {
                JSValue error = JS_GetException(ctx_);
                std::string error_msg = formatError(error);
                JS_FreeValue(ctx_, error);

                throw JSException(error_msg);
            }

            JS_FreeValue(ctx_, js_obj);
            return wrapValue(js_prop);
        }

        void setProperty(const JSValue& object, const std::string& key, const JSValue& value) override {
            JSValue js_obj = unwrapValue(object);
            JSValue js_val = unwrapValue(value);

            int ret = JS_SetPropertyStr(ctx_, js_obj, key.c_str(), js_val);

            if (ret != 1) {
                throw JSException("Failed to set property");
            }

            JS_FreeValue(ctx_, js_obj);
        }

        bool hasProperty(const JSValue& object, const std::string& key) override {
            JSValue js_obj = unwrapValue(object);

            JSAtom atom = JS_NewAtom(ctx_, key.c_str());
            bool has_prop = JS_HasProperty(ctx_, js_obj, atom) == 1;
            JS_FreeAtom(ctx_, atom);

            JS_FreeValue(ctx_, js_obj);
            return has_prop;
        }

        void registerModule(const std::string& name, const JSModuleInitFunc& init_func) override {
            // Store the module initializer
            module_initializers_[name] = init_func;

            // In a real implementation, we would register with QuickJS module system
            // For now, we'll create a global object with the module name
            JSValue global = JS_GetGlobalObject(ctx_);

            // Create the module object
            JSValue module_obj = JS_NewObject(ctx_);

            // Initialize the module
            init_func(shared_from_this(), module_obj);

            // Set as a property on the global object
            JS_SetPropertyStr(ctx_, global, name.c_str(), module_obj);

            JS_FreeValue(ctx_, global);
        }

        // QuickJS-specific methods
        JSContext* getQuickJSContext() const {
            return ctx_;
        }

    private:
        // Helper to wrap QuickJS value in our JSValue
        JSValue wrapValue(JSValue js_val) {
            // Create a managed JSValue that will free the QuickJS value
            // when it's destroyed
            return JSValue(this, new JSValueData(js_val));
        }

        // Helper to unwrap our JSValue to QuickJS value
        JSValue unwrapValue(const JSValue& value) {
            // Get the wrapped QuickJS value and duplicate it
            // (because the caller will free it)
            JSValueData* data = static_cast<JSValueData*>(value.data());
            return JS_DupValue(ctx_, data->js_val);
        }

        // Format a QuickJS error for display
        std::string formatError(JSValue error) {
            std::string result;

            // Get the error message
            const char* error_msg = nullptr;
            if (JS_IsError(ctx_, error)) {
                JSValue message = JS_GetPropertyStr(ctx_, error, "message");
                if (!JS_IsUndefined(message)) {
                    error_msg = JS_ToCString(ctx_, message);
                }
                JS_FreeValue(ctx_, message);

                // Get stack trace if available
                JSValue stack = JS_GetPropertyStr(ctx_, error, "stack");
                if (!JS_IsUndefined(stack)) {
                    const char* stack_str = JS_ToCString(ctx_, stack);
                    if (stack_str) {
                        if (error_msg) {
                            result = std::string(error_msg) + "\n" + stack_str;
                        } else {
                            result = stack_str;
                        }
                        JS_FreeCString(ctx_, stack_str);
                    }
                }
                JS_FreeValue(ctx_, stack);
            }

            if (result.empty() && error_msg) {
                result = error_msg;
            }

            if (error_msg) {
                JS_FreeCString(ctx_, error_msg);
            }

            return result.empty() ? "Unknown error" : result;
        }

        // Register standard QuickJS modules
        void registerStandardModules() {
            // Here we would register standard modules like 'os', 'std', etc.
            // For now we'll just ensure the standard objects are available
            JSValue global = JS_GetGlobalObject(ctx_);

            // Add global objects like JSON, Math, etc.
            JS_SetPropertyStr(ctx_, global, "JSON", JS_NewObject(ctx_));
            // ... more standard objects

            JS_FreeValue(ctx_, global);
        }

        // JSValue data structure to be stored in our JSValue wrapper
        struct JSValueData {
            JSValue js_val;

            explicit JSValueData(JSValue val) : js_val(val) {}

            ~JSValueData() {
                // The QuickJS context might already be freed in some cases,
                // so this is not 100% safe. A proper implementation would need
                // to ensure values are freed before contexts.
            }
        };

        std::string name_;
        JSContext* ctx_ = nullptr;
        JSEngineImpl* engine_ = nullptr;

        // Maps function IDs to native function wrappers
        std::unordered_map<size_t, JSNativeFunction> native_functions_;
        size_t next_func_id_ = 1;

        // Maps module names to initializer functions
        std::unordered_map<std::string, JSModuleInitFunc> module_initializers_;
    };

    JSRuntime* runtime_ = nullptr;
    size_t memory_limit_ = 16 * 1024 * 1024; // 16 MB default
    size_t stack_size_ = 1024 * 1024; // 1 MB default

    std::unordered_map<std::string, std::shared_ptr<JSContextImpl>> contexts_;
};

// Factory method to create the engine
std::unique_ptr<JSEngine> JSEngine::create() {
    return std::make_unique<JSEngineImpl>();
}

} // namespace fmus::ivrjs