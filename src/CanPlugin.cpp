#include "CanPlugin.h"
#include <stdlib.h>

using namespace canopener;

CanPlugin::CanPlugin(int txPin, int rxPin)
		:espBus(txPin, rxPin) {
    canMessageFunc=JS_UNDEFINED;
}

void CanPlugin::setJsEngine(JsEngine& jsEngine_) {
	jsEngine=&jsEngine_;
}

void CanPlugin::init() {
	jsEngine->addGlobal("canWrite",jsEngine->newMethod(this,&CanPlugin::canWrite,2));
    jsEngine->addGlobal("setCanMessageFunc",jsEngine->newMethod(this,&CanPlugin::setCanMessageFunc,1));
}

void CanPlugin::loop() {
	espBus.loop();

    while (espBus.available()) {
        cof_t frame;
        espBus.read(&frame);
        char s[256];
        cof_to_slcan(&frame,s);

        auto ctx=jsEngine->getContext();
        JSValue args[1];
        args[0]=JS_NewString(ctx,s);
        JSValue ret=JS_Call(ctx,canMessageFunc,JS_UNDEFINED,1,args);
        JS_FreeValue(ctx,args[0]);
        if (JS_IsException(ret)) {
            JSValue err=jsEngine->getExceptionMessage();
            jsEngine->printJsValue(err);
            JS_FreeValue(ctx,err);
        }

        JS_FreeValue(ctx,ret);
    }

    /*while (espBus.available()) {
        cof_t frame;
        espBus.read(&frame);

        JSContext *ctx = jsEngine->getContext();

        // 1. Create ArrayBuffer
        JSValue ab = JS_NewArrayBuffer(ctx, NULL, frame.len, NULL, NULL, 0);
        if (JS_IsException(ab))
            return;

        // 2. Get pointer and copy
        size_t psize;
        uint8_t *buf = JS_GetArrayBuffer(ctx, &psize, ab);
        memcpy(buf, frame.data, frame.len);

        / *
        // 3. Create Uint8Array view (constructor-style)
        JSValue ta_argv[3];
        ta_argv[0] = ab;                      // buffer
        ta_argv[1] = JS_NewUint32(ctx, 0);    // byteOffset
        ta_argv[2] = JS_NewUint32(ctx, frame.len); // length

        JSValue u8 = JS_NewTypedArray(
            ctx,
            3,
            ta_argv,
            JS_TYPED_ARRAY_UINT8
        );

        JS_FreeValue(ctx, ta_argv[1]);
        JS_FreeValue(ctx, ta_argv[2]);

        // 4. Call function
        JSValue argv[2];
        argv[0] = JS_NewUint32(ctx, frame.id);
        argv[1] = u8;

        JSValue ret = JS_Call(ctx, canMessageFunc, JS_UNDEFINED, 2, argv);
        if (JS_IsException(ret)) {
            JSValue err=jsEngine->getExceptionMessage();
            jsEngine->printJsValue(err);
            JS_FreeValue(ctx,err);
        }

        // 5. Cleanup
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, argv[0]); 
        JS_FreeValue(ctx, u8);* /
        JS_FreeValue(ctx, ab);
    }*/
}

void CanPlugin::close() {
    JS_FreeValue(jsEngine->getContext(),canMessageFunc);
    canMessageFunc=JS_UNDEFINED;
}

JSValue CanPlugin::canWrite(int argc, JSValueConst *argv) {
    if (argc<2)
        return JS_ThrowTypeError(jsEngine->getContext(), "too few arguments");

    uint8_t *data;
    size_t offset, len, bpe;
    JSValue buffer;
    uint32_t id;

    JS_ToUint32(jsEngine->getContext(),&id,argv[0]);
    buffer=JS_GetTypedArrayBuffer(
        jsEngine->getContext(),
        argv[1],
        &offset,
        &len,
        &bpe
    );

    if (JS_IsException(buffer))
        return JS_EXCEPTION;

    data=JS_GetArrayBuffer(jsEngine->getContext(),&len,buffer);
    if (!data) {
        JS_FreeValue(jsEngine->getContext(),buffer);
        return JS_EXCEPTION;
    }

    data+=offset;

    cof_t cof;
    cof.id=id;
    cof.len=len;
    for (int i=0; i<len; i++)
        cof.data[i]=data[i];

    espBus.write(&cof);

    JS_FreeValue(jsEngine->getContext(),buffer);
    return JS_UNDEFINED;
}

/*JSValue CanPlugin::canWrite(int argc, JSValueConst *argv) {
    const char *s=JS_ToCString(jsEngine->getContext(),argv[0]);
    if (!s)
	    return JS_UNDEFINED;

    cof_t frame;
    if (cof_from_slcan(&frame,s)) {
        //jsEngine->getStream()->printf("can write: %s\n",s);
        espBus.write(&frame);
    }

    JS_FreeCString(jsEngine->getContext(),s);

    return JS_UNDEFINED;
}*/

JSValue CanPlugin::setCanMessageFunc(int argc, JSValueConst *argv) {
    JS_FreeValue(jsEngine->getContext(),canMessageFunc);
    canMessageFunc=JS_DupValue(jsEngine->getContext(),argv[0]);
    return JS_UNDEFINED;
}
