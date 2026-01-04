#pragma once
#include <Arduino.h>
extern "C" {
#include "quickjs.h"
}

#include "FS.h"
#include "SPIFFS.h"

using JSFunctionWrapper=std::function<JSValue(int, JSValueConst*)>;

class JsEngine;

class JsPlugin {
public:
    virtual void loop() {}
    virtual void setJsEngine(JsEngine& jsEngine) = 0;
    virtual void init() {};
    virtual void close() {};
    virtual ~JsPlugin() {}
};

struct JsCString {
    JSContext *ctx;
    const char *str;

    JsCString(JSContext *ctx, JSValue val)
        : ctx(ctx), str(JS_ToCString(ctx, val)) {}

    ~JsCString() {
        if (str) JS_FreeCString(ctx, str);
    }

    const char *c_str() { return str; }
};

class JsEngineTimer {
public:
	uint32_t id;
	uint32_t deadline;
	uint32_t interval;
	JSValue func;
};

class JsFile {
public:
	uint32_t id;
	File file;
};

class JsEngine {
public:
	JsEngine(Stream &stream);
	void loop();
	void begin();
	void addGlobal(const char *name, JSValue val);
	void addRawFunction(const char *name, void *func, int argc);
	JSValue newFunction(JSFunctionWrapper func, int length=0);
	template<typename T>
	JSValue newMethod(T* obj, JSValue (T::*memFunc)(int, JSValueConst*), int length=0) {
	    auto wrapper = [obj, memFunc](int argc, JSValueConst *argv) -> JSValue {
	        return (obj->*memFunc)(argc, argv);
	    };

	    return newFunction(wrapper,length);
	}

	void addPlugin(JsPlugin *plugin);
	JSContext* getContext() { return ctx; } 
	Stream* getStream() { return &stream; }
	JSValue getExceptionMessage();
	void printJsValue(JSValue val);

private:
	JSValue digitalWrite(int argc, JSValueConst *argv);
	JSValue digitalRead(int argc, JSValueConst *argv);
	JSValue setTimeout(int argc, JSValueConst *argv);
	JSValue setInterval(int argc, JSValueConst *argv);
	JSValue clearTimer(int argc, JSValueConst *argv);
	JSValue consoleLog(int argc, JSValueConst *argv);
	JSValue serialWrite(int argc, JSValueConst *argv);
	JSValue setSerialDataFunc(int argc, JSValueConst *argv);
	JSValue fileOpen(int argc, JSValueConst *argv);
	JSValue fileClose(int argc, JSValueConst *argv);
	JSValue fileRead(int argc, JSValueConst *argv);
	JSValue fileWrite(int argc, JSValueConst *argv);
	JSValue scheduleReload(int argc, JSValueConst *argv);
	void pumpJobs();
	void reset();
	void close();
	JSRuntime *rt=nullptr;
	JSContext *ctx=nullptr;
	Stream& stream;
	std::vector<JsEngineTimer> timers;
	std::vector<JsFile> files;
	std::vector<JsPlugin*> plugins;
	JSValue serialDataFunc;
	JSValue bootError;
	bool reloadScheduled,began=false;
	int startCount=0;
	std::vector<JSFunctionWrapper*> funcs;
	uint32_t maintenanceDeadline;
	uint32_t resourceCount;
};
