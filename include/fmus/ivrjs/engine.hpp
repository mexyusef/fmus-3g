#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>

namespace fmus::ivrjs {

// Forward declarations
class JSContext;
class JSValue;

// Exception thrown by JS engine
class JSException : public std::runtime_error {
public:
    explicit JSException(const std::string& message)
        : std::runtime_error(message) {}
};

// JavaScript value types
enum class JSValueType {
    Undefined,
    Null,
    Boolean,
    Number,
    String,
    Object,
    Array,
    Function,
    Error
};

// A JavaScript value
class JSValue {
public:
    // Constructor with context and data
    JSValue(JSContext* context, void* data)
        : context_(context), data_(data) {}

    // Default constructor creates undefined value
    JSValue() : context_(nullptr), data_(nullptr) {}

    // Move operations
    JSValue(JSValue&& other) noexcept
        : context_(other.context_), data_(other.data_) {
        other.context_ = nullptr;
        other.data_ = nullptr;
    }

    JSValue& operator=(JSValue&& other) noexcept {
        if (this != &other) {
            // Free current data if any
            if (data_) {
                // TODO: Proper cleanup of data
            }

            context_ = other.context_;
            data_ = other.data_;
            other.context_ = nullptr;
            other.data_ = nullptr;
        }
        return *this;
    }

    // Copy operations (not allowed - JS values are owned)
    JSValue(const JSValue&) = delete;
    JSValue& operator=(const JSValue&) = delete;

    // Destructor
    ~JSValue() {
        // Free data if any
        if (data_) {
            // TODO: Proper cleanup of data based on context
        }
    }

    // Get context
    JSContext* context() const { return context_; }

    // Get raw data pointer
    void* data() const { return data_; }

    // Check if value is valid
    bool isValid() const { return data_ != nullptr; }

private:
    JSContext* context_;
    void* data_;
};

// Native function type
using JSNativeFunction = std::function<JSValue(const std::vector<JSValue>&)>;

// Module initializer function type
using JSModuleInitFunc = std::function<void(std::shared_ptr<JSContext>, JSValue)>;

// JavaScript context
class JSContext : public std::enable_shared_from_this<JSContext> {
public:
    virtual ~JSContext() = default;

    // Get context name
    virtual std::string name() const = 0;

    // Evaluate JavaScript code
    virtual JSValue evaluateScript(const std::string& script,
                                  const std::string& filename = "<eval>") = 0;

    // Evaluate a module
    virtual JSValue evaluateModule(const std::string& module_code,
                                  const std::string& module_name) = 0;

    // Call a JavaScript function
    virtual JSValue callFunction(const JSValue& function,
                                const std::vector<JSValue>& args) = 0;

    // Get the global object
    virtual JSValue getGlobalObject() = 0;

    // Create values
    virtual JSValue createObject() = 0;
    virtual JSValue createArray() = 0;
    virtual JSValue createString(const std::string& str) = 0;
    virtual JSValue createNumber(double value) = 0;
    virtual JSValue createBoolean(bool value) = 0;
    virtual JSValue createNull() = 0;
    virtual JSValue createUndefined() = 0;
    virtual JSValue createFunction(const JSNativeFunction& func,
                                  const std::string& name = "anonymous") = 0;

    // Property access
    virtual JSValue getProperty(const JSValue& object, const std::string& key) = 0;
    virtual void setProperty(const JSValue& object, const std::string& key,
                           const JSValue& value) = 0;
    virtual bool hasProperty(const JSValue& object, const std::string& key) = 0;

    // Register a native module
    virtual void registerModule(const std::string& name,
                              const JSModuleInitFunc& init_func) = 0;
};

// JavaScript engine
class JSEngine {
public:
    virtual ~JSEngine() = default;

    // Create a new context
    virtual std::shared_ptr<JSContext> createContext(const std::string& name) = 0;

    // Get an existing context
    virtual std::shared_ptr<JSContext> getContext(const std::string& name) const = 0;

    // Remove a context
    virtual void removeContext(const std::string& name) = 0;

    // Memory management
    virtual void setMemoryLimit(size_t limit) = 0;
    virtual size_t getMemoryLimit() const = 0;
    virtual size_t getMemoryUsage() const = 0;
    virtual void garbageCollect() = 0;

    // Factory method to create an instance
    static std::unique_ptr<JSEngine> create();
};

} // namespace fmus::ivrjs