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

    /*if (!data)
        return JS_ThrowTypeError(jsEngine->getContext(), "typed array expected");*/

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
