import {RemoteDevice, MasterDevice} from "canopener";

global.master=new MasterDevice({bus: global.canBus});
let dev=new RemoteDevice({nodeId: 5});
dev.on("stateChange",()=>{
	console.log("device state: "+dev.getState());
});

master.addDevice(dev);

let blink=dev.entry(0x2000,0).setType("bool");

async function tick() {
	console.log("timeout...");
	digitalWrite(8,!digitalRead(8));

	//if (dev.getState()!="disconnected")
	blink.set(!blink.get());

	setTimeout(tick,1000);
}

tick();
