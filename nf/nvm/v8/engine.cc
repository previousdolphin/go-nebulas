// Copyright (C) 2017 go-nebulas authors
//
// This file is part of the go-nebulas library.
//
// the go-nebulas library is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// the go-nebulas library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with the go-nebulas library.  If not, see
// <http://www.gnu.org/licenses/>.
//

#include "engine.h"
#include "array_buffer_allocator.h"
#include "lib/execution_env.h"
#include "lib/log_callback.h"
#include "lib/require_callback.h"
#include "lib/storage_object.h"

#include <libplatform/libplatform.h>
#include <v8.h>

#include <assert.h>

using namespace v8;

static Platform *platformPtr = NULL;

void Initialize() {
  platformPtr = platform::CreateDefaultPlatform();

  // Initialize V8.
  V8::InitializeICU();
  V8::InitializeExternalStartupData("");
  V8::InitializePlatform(platformPtr);
  V8::Initialize();
}

void Dispose() {
  V8::Dispose();
  V8::ShutdownPlatform();
  if (platformPtr) {
    delete platformPtr;
    platformPtr = NULL;
  }
}

V8Engine *CreateEngine() {
  ArrayBufferAllocator *allocator = new ArrayBufferAllocator();

  Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = allocator;
  Isolate *isolate = Isolate::New(create_params);

  V8Engine *e = (V8Engine *)calloc(1, sizeof(V8Engine));
  e->allocator = allocator;
  e->isolate = isolate;
  return e;
}

void DeleteEngine(V8Engine *e) {
  Isolate *isolate = static_cast<Isolate *>(e->isolate);
  isolate->Dispose();

  delete static_cast<ArrayBufferAllocator *>(e->allocator);

  free(e);
}

int RunScriptSource(V8Engine *e, const char *data, void *balanceHandler,
                    void *lcsHandler, void *gcsHandler) {
  Isolate *isolate = static_cast<Isolate *>(e->isolate);
  assert(isolate);

  Isolate::Scope isolate_scope(isolate);
  // Create a stack-allocated handle scope.
  HandleScope handle_scope(isolate);

  // Create global object template.
  Local<ObjectTemplate> globalTpl = ObjectTemplate::New(isolate);
  globalTpl->Set(String::NewFromUtf8(isolate, "require"),
                 FunctionTemplate::New(isolate, requireCallback));
  globalTpl->Set(String::NewFromUtf8(isolate, "_native_log"),
                 FunctionTemplate::New(isolate, logCallback));
  NewStorageType(isolate, globalTpl);

  // Create a new context.
  Local<Context> context = Context::New(isolate, NULL, globalTpl);
  // Enter the context for compiling and running the hello world script.
  Context::Scope context_scope(context);

  // Continue put objects to global object.
  NewStorageObject(isolate, context, balanceHandler, lcsHandler, gcsHandler);

  // Setup execution env.
  if (SetupExecutionEnv(isolate, context)) {
    fprintf(stderr, "setup execution env failed.\n");
    return 1;
  }

  // Create a string containing the JavaScript source code.
  Local<String> source =
      String::NewFromUtf8(isolate, data, NewStringType::kNormal)
          .ToLocalChecked();
  // Compile the source code.
  MaybeLocal<Script> script = Script::Compile(context, source);

  if (script.IsEmpty()) {
    fprintf(stderr, "compile error.\n");
    return 1;
  }

  // Run the script to get the result.
  MaybeLocal<Value> ret = script.ToLocalChecked()->Run(context);
  if (!ret.IsEmpty()) {
    Local<Value> ret_str = ret.ToLocalChecked();
    String::Utf8Value s(ret_str);
    fprintf(stdout, "ret value: %s\n", *s);
  }

  return 0;
}
