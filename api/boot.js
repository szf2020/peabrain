import Repl from "./Repl.js";
import {EventEmitter, /*slcanStringify,*/ slcanParse, RemoteDevice, MasterDevice, awaitEvent} from "canopener";

global.awaitEvent=awaitEvent;
global.EventEmitter=EventEmitter;
global.RemoteDevice=RemoteDevice;
global.MasterDevice=MasterDevice;

global.serial=new EventEmitter();
serial.write=data=>serialWrite(data);
setSerialDataFunc(d=>serial.emit("data",d));

global.console={};
console.log=(...args)=>serialWrite(args.map(s=>String(s)).join(" ")+"\r\n");

repl=new Repl(serial);
repl.on("message",message=>{
	if (message.type!="call" && !message.method)
		return;

	//console.log(JSON.stringify(handleRpcMessage(global,message)));

	try {
		let res=global[message.method](...message.params);
		serial.write(JSON.stringify({
			id: message.id,
			result: res
		})+"\n");
	}

	catch (e) {
		serial.write(JSON.stringify({
			id: message.id,
			error: {
				message: String(e)
			}
		})+"\n");
	}
});

global.canBus=new EventEmitter();
global.canBus.write=message=>{
	//console.log("writing can: "+JSON.stringify(message));
	//let s=slcanStringify(message);
	//canWrite(s);
	//console.log("writing can: "+s);//JSON.stringify(message));

	canWrite(message.id,message.data);
}

global.canBus.send=global.canBus.write;

setCanMessageFunc(message=>{
	global.canBus.emit("message",slcanParse(message));
	//console.log("can message: "+message);
});

/*setCanMessageFunc((id, data)=>{
	global.canBus.emit("message",{id, data});//slcanParse(message));
	//console.log("can message: "+message);
});*/
