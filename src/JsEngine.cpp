#include "JsEngine.h"

extern const char boot_js[];
//extern const unsigned int boot_js_len;

static JSValue JsEngine_functionTrampoline(JSContext *ctx, JSValueConst this_val,
		int argc, JSValueConst *argv,
		int magic, JSValueConst *datas) {
	auto *wrapper = reinterpret_cast<JSFunctionWrapper*>(JS_VALUE_GET_PTR(datas[0]));
    if (!wrapper) return JS_EXCEPTION;

    try {
        return (*wrapper)(argc, argv);
    } catch (...) {
        return JS_ThrowInternalError(ctx, "C++ exception in std::function");
    }
}

JsEngine::JsEngine(Stream& stream)
		:stream(stream) {
    serialDataFunc=JS_UNDEFINED;
    bootError=JS_UNDEFINED;
	//reset();
}

void JsEngine::addPlugin(JsPlugin *plugin) {
    plugin->setJsEngine(*this);
    plugins.push_back(plugin);
}

void JsEngine::begin() {
	reset();
}

void JsEngine::close() {
    for (JsPlugin* p: plugins)
        p->close();

    for (auto *f : funcs) delete f;
    funcs.clear();

    for (auto t: timers)
        JS_FreeValue(ctx,t.func);

    timers.clear();

    JS_FreeValue(ctx, serialDataFunc);
    JS_FreeValue(ctx, bootError);

    serialDataFunc=JS_UNDEFINED;
    bootError=JS_UNDEFINED;

    if (ctx) JS_FreeContext(ctx);
    ctx=nullptr;

    if (rt) {
        JS_RunGC(rt);
        JS_FreeRuntime(rt);
    }
	rt=nullptr;
}

JSValue JsEngine::newFunction(JSFunctionWrapper func, int length) {
    auto *heapFunc=new JSFunctionWrapper(std::move(func));
    funcs.push_back(heapFunc);

	JSValue data=JS_NewBigInt64(ctx, reinterpret_cast<int64_t>(heapFunc));
    JSValue jsFunc=JS_NewCFunctionData(ctx, JsEngine_functionTrampoline, length, 
    	0, 1, &data);

	JS_FreeValue(ctx, data);

    return jsFunc;
}

void JsEngine::addRawFunction(const char *name, void *func, int argnum) {
    auto f=newFunction([this, func, argnum](int argc, JSValueConst* argv) -> JSValue {
        uint32_t ret;
        uint32_t params[argnum];

        for (int i=0; i<argnum; i++)
            JS_ToUint32(ctx,&params[i],argv[i]);

        auto func0=(uint32_t (*)())func;
        auto func1=(uint32_t (*)(uint32_t))func;
        auto func2=(uint32_t (*)(uint32_t, uint32_t))func;
        auto func3=(uint32_t (*)(uint32_t, uint32_t, uint32_t))func;
 
        switch (argnum) {
            case 0: ret=func0(); break;
            case 1: ret=func1(params[0]); break;
            case 2: ret=func2(params[0],params[1]); break;
            case 3: 
                //stream.printf("setting %d %d %d\n",params[0],params[1],params[2]);
                ret=func3(params[0],params[1],params[2]); 
                break;
        }

        //stream.printf("ret: %d\n",ret);

        return JS_NewUint32(ctx, ret);
    },argnum);

    addGlobal(name,f);
}

void JsEngine::addGlobal(const char *name, JSValue val) {
    JSValue global=JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx,global,name,val);
    JS_FreeValue(ctx,global);
}

JSValue JsEngine::getExceptionMessage() {
    JSValue ex=JS_GetException(ctx);
    const char *msg = JS_ToCString(ctx, ex);
    JSValue res=JS_NewString(ctx, msg);
    JS_FreeCString(ctx, msg);
    JS_FreeValue(ctx, ex);

    return res;
}

void JsEngine::reset() {
	close();
    if (!rt)
        rt=JS_NewRuntime();

    resourceCount=0;
    serialDataFunc=JS_UNDEFINED;

    ctx=JS_NewContext(rt);
    JS_SetContextOpaque(ctx,this);
    maintenanceDeadline=millis();

    addGlobal("digitalWrite",newMethod(this,&JsEngine::digitalWrite,2));
    addGlobal("digitalRead",newMethod(this,&JsEngine::digitalRead,1));
    addGlobal("serialWrite",newMethod(this,&JsEngine::serialWrite,1));
    addGlobal("setTimeout",newMethod(this,&JsEngine::setTimeout,2));
    addGlobal("setInterval",newMethod(this,&JsEngine::setInterval,2));
    addGlobal("clearTimeout",newMethod(this,&JsEngine::clearTimer,1));
    addGlobal("clearInterval",newMethod(this,&JsEngine::clearTimer,1));
    addGlobal("setSerialDataFunc",newMethod(this,&JsEngine::setSerialDataFunc,1));
    addGlobal("fileOpen",newMethod(this,&JsEngine::fileOpen,2));
    addGlobal("fileClose",newMethod(this,&JsEngine::fileClose,1));
    addGlobal("fileRead",newMethod(this,&JsEngine::fileRead,1));
    addGlobal("fileWrite",newMethod(this,&JsEngine::fileWrite,1));
    addGlobal("scheduleReload",newMethod(this,&JsEngine::scheduleReload,1));

    JSValue global=JS_GetGlobalObject(ctx);
    addGlobal("global",global);

    for (JsPlugin* p: plugins)
        p->init();

    bootError=JS_UNDEFINED;
    JSValue val=JS_Eval(ctx, boot_js, strlen(boot_js), "<builtin>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val))
        bootError=getExceptionMessage();

    JS_FreeValue(ctx, val);

    if (JS_IsUndefined(bootError)) {
        File f = SPIFFS.open("/boot.js", FILE_READ);
        if (f) {
            String content = f.readString();
            f.close();

            int len=strlen(content.c_str());
            stream.printf("running program...\n");
            JSValue bootval=JS_Eval(ctx, content.c_str(), len, "<builtin>", JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(bootval)) {
                bootError=getExceptionMessage();
                stream.printf("Boot error!\n");
            }

            JS_FreeValue(ctx,bootval);
        }
    }

    addGlobal("bootError",JS_DupValue(ctx,bootError));

    startCount++;
    stream.printf("Started, count=%d\n",startCount);
    stream.printf("{\"type\": \"started\"}\n",startCount);
}

void JsEngine::printJsValue(JSValue val) {
    const char *out=JS_ToCString(ctx,val);
    if (out) {
        stream.println(out);
        JS_FreeCString(ctx, out);
    }

    else {
        stream.println("<unknown>");
    }
}

void JsEngine::pumpJobs() {
    JSContext *tmpctx=ctx;
    int ret=1;

    while (ret>0) {
        ret=JS_ExecutePendingJob(rt, &tmpctx);
        if (ret<0) {
            JSValue ex=JS_GetException(tmpctx);
            const char *s=JS_ToCString(tmpctx, ex);
            if (s) {
                Serial.printf("Unhandled promise rejection: %s\n",s);
                JS_FreeCString(tmpctx,s);
            }
            JS_FreeValue(tmpctx,ex);
            break;
        }
    }
}

void JsEngine::loop() {
    if (!began) {
        began=true;
        begin();
    }

    for (JsPlugin* p: plugins)
        p->loop();

    if (reloadScheduled) {
        reloadScheduled=false;
        reset();
    }

    pumpJobs();

    uint32_t now=millis();
    if (now>maintenanceDeadline) {
        //Serial.printf("maintenance...\n");
        maintenanceDeadline=now+1000;

        JS_RunGC(rt);

        if (!JS_IsUndefined(bootError)) {
            stream.println("**** BOOT ERROR ****");
            printJsValue(bootError);
        }
    }

    std::vector<JsEngineTimer> expired;
    for (auto it=timers.begin(); it!=timers.end();) {
        if (now>=it->deadline) {
            expired.push_back(*it);
            if (it->interval) {
                it->deadline=now+it->interval;
                it++;
            }

            else {
                it=timers.erase(it);
            }
        }

        else {
            it++;
        }
    }

    for (auto &t: expired) {
        JSValue val=JS_Call(ctx,t.func,JS_UNDEFINED,0,nullptr);
        if (JS_IsException(val)) {
            JSValue err=getExceptionMessage();
            printJsValue(err);
            JS_FreeValue(ctx,err);
        }

        JS_FreeValue(ctx,val);

        if (!t.interval) {
            JS_FreeValue(ctx,t.func);
        }
    }

    int len=stream.available();
    if (len) {
        char s[len+1];
        for (int i=0; i<len; i++) {
            int c=stream.read();
            //stream.write(c);
            s[i]=c;
        }

        s[len]='\0';

        //stream.printf("reading: %d %s\n",len,s);

        ///*if (s[0]=='#') {
        //    stream.printf("restart...\n");
        //    reloadScheduled=true;
        //}

        JSValue args[1];
        args[0]=JS_NewString(ctx,s);

        JSValue ret=JS_Call(ctx,serialDataFunc,JS_UNDEFINED,1,args);
        JS_FreeValue(ctx,args[0]);
        JS_FreeValue(ctx,ret);
    }
}

JSValue JsEngine::serialWrite(int argc, JSValueConst *argv) {
    const char *s = JS_ToCString(ctx, argv[0]);
    if (s) {
        for (int i=0; i<strlen(s); i++)
            stream.write(s[i]);

        //stream.print(s);
        JS_FreeCString(ctx, s);
    }

    return JS_UNDEFINED;
}

JSValue JsEngine::setSerialDataFunc(int argc, JSValueConst *argv) {
    JS_FreeValue(ctx, serialDataFunc);

    serialDataFunc=JS_DupValue(ctx, argv[0]);

    return JS_UNDEFINED;
}

JSValue JsEngine::digitalRead(int argc, JSValueConst *argv) {
    int32_t pin=0,val;

    JS_ToInt32(ctx,&pin,argv[0]);
    val=::digitalRead(pin);

    return JS_NewUint32(ctx,val);
}

JSValue JsEngine::digitalWrite(int argc, JSValueConst *argv) {
    int32_t pin=0, val=0;

    JS_ToInt32(ctx,&pin,argv[0]);
    JS_ToInt32(ctx,&val,argv[1]);

    ::digitalWrite(pin,val);

    return JS_UNDEFINED;
}

JSValue JsEngine::setTimeout(int argc, JSValueConst *argv) {
    uint32_t until;
    JS_ToUint32(ctx,&until,argv[1]);

    resourceCount++;
    JsEngineTimer t;
    t.id=resourceCount;
    t.deadline=millis()+until;
    t.func=JS_DupValue(ctx, argv[0]);
    t.interval=0;
    timers.push_back(t);

    return JS_NewUint32(ctx,t.id);
}

JSValue JsEngine::setInterval(int argc, JSValueConst *argv) {
    uint32_t interval;
    JS_ToUint32(ctx,&interval,argv[1]);

    resourceCount++;
    JsEngineTimer t;
    t.id=resourceCount;
    t.deadline=millis()+interval;
    t.func=JS_DupValue(ctx, argv[0]);
    t.interval=interval;
    timers.push_back(t);

    return JS_NewUint32(ctx,t.id);
}

JSValue JsEngine::clearTimer(int argc, JSValueConst *argv) {
    uint32_t tid;
    JS_ToUint32(ctx,&tid,argv[0]);

    auto it=std::find_if(timers.begin(), timers.end(),
        [&](const JsEngineTimer& t) { return t.id == tid; });

    if (it==timers.end())
        return JS_UNDEFINED;
        //return JS_ThrowInternalError(ctx, "invalid timeout id");

    JS_FreeValue(ctx,it->func);
    timers.erase(it);
    return JS_UNDEFINED;
}


JSValue JsEngine::fileOpen(int argc, JSValueConst *argv) {
    JsCString path(ctx, argv[0]);
    JsCString mode(ctx, argv[1]);

    File f=SPIFFS.open(path.c_str(), mode.c_str());
    if (!f)
        return JS_ThrowInternalError(ctx, "failed to open file");

    resourceCount++;
    JsFile jsf;
    jsf.id=resourceCount;
    jsf.file=f;
    files.push_back(jsf);

    return JS_NewUint32(ctx,jsf.id);
}

JSValue JsEngine::fileRead(int argc, JSValueConst *argv) {
    uint32_t fid;
    JS_ToUint32(ctx,&fid,argv[0]);

    auto it=std::find_if(files.begin(), files.end(),
        [&](const JsFile& f) { return f.id == fid; });

    if (it==files.end())
        return JS_ThrowInternalError(ctx, "invalid file id");

    const size_t N=128;
    char buffer[N];
    size_t bytesRead=it->file.readBytes(buffer, N);

    return JS_NewStringLen(ctx, buffer, bytesRead);
}

JSValue JsEngine::fileWrite(int argc, JSValueConst *argv) {
    uint32_t fid;
    JS_ToUint32(ctx,&fid,argv[0]);
    JsCString data(ctx, argv[1]);

    auto it=std::find_if(files.begin(), files.end(),
        [&](const JsFile& f) { return f.id == fid; });

    if (it==files.end())
        return JS_ThrowInternalError(ctx, "invalid file id");

    //it->file.print("helloooo");
    //it->file.write((uint8_t *)data.c_str(),strlen(data.c_str()));
    it->file.print(data.c_str());
    return JS_UNDEFINED;
}

JSValue JsEngine::fileClose(int argc, JSValueConst *argv) {
    uint32_t fid;
    JS_ToUint32(ctx,&fid,argv[0]);

    auto it=std::find_if(files.begin(), files.end(),
        [&](const JsFile& f) { return f.id == fid; });

    if (it==files.end())
        return JS_ThrowInternalError(ctx, "invalid file id");

    it->file.close();
    files.erase(it);
    return JS_UNDEFINED;
}

JSValue JsEngine::scheduleReload(int argc, JSValueConst *argv) {
    stream.printf("Schedule reload...\n");
    reloadScheduled=true;
    return JS_UNDEFINED;
}
