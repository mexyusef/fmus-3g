#pragma once

#include <fmus/ivrjs/engine.hpp>

namespace fmus::ivrjs::functions {

// Register telephony functions
void registerTelephonyFunctions(std::shared_ptr<JSContext> context, JSValue module);

// Register database functions
void registerDatabaseFunctions(std::shared_ptr<JSContext> context, JSValue module);

// Register I/O functions (file, network, etc.)
void registerIOFunctions(std::shared_ptr<JSContext> context, JSValue module);

// Register media functions (audio/video processing)
void registerMediaFunctions(std::shared_ptr<JSContext> context, JSValue module);

// Register HTTP client functions
void registerHTTPFunctions(std::shared_ptr<JSContext> context, JSValue module);

// Register system functions (timers, logging, etc.)
void registerSystemFunctions(std::shared_ptr<JSContext> context, JSValue module);

// Register all standard functions in a context
void registerAllFunctions(std::shared_ptr<JSContext> context);

} // namespace fmus::ivrjs::functions