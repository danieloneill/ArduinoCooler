# ArduinoCooler
Air conditioner -> Beverage Cooler conversion code for the Arduino Uno.

Used to convert an 8000BTU "window shaker" air conditioner into a cooling unit for an in-built 4-door retail beverage cabinet.

By default this code uses the following timeouts:

MINRUN 5:00
MAXRUN 15:00
THAWTIME 5:00

By default the code will attempt to cool the cabinet/room to 7c +- 3c. This "buffer" implies that if it reaches (for example) 9c in 15 minutes it will circulate rather than thaw, but also that it can cool to as low as 4c before terminating the cooling phase (otherwise it'll run until MINRUN is reached, 5 minutes).

This expects an I2C LCD panel to be connected (particularly via LiquidCrystal_I2C) and a DHT22 (or DHT11, but you'd need to change a line of code) temperature sensor.

The display will show current phase, temperature, and sensor status.

Pins are declared near the top such that you can select your Low, Medium, and High Fan relay pins as well as the Compressor pin.

Some compressors use a start capacitor to get going but still pull a noticable in-rush current. This frigidaire that I've converted, as an example, blew my 25A relay on the first run. (The AC unit is fused at 15A mind you!) In my case I used a solid-state relay (since I have boxes of them), but a furnace relay now cuts the mustard nicely.

Some people would put a disclaimer here to remind you of the hazards of dissecting a high-pressure and high-voltage appliance, but you wouldn't be reading this if you weren't either well-qualified or partially unscrewed in the head, so instead I'll point out just a couple safety reminders:

 * Even residential compressors can pressure up to 5k PSI. This is a bomb.
 * Ensure your unit doesn't use electrically actuated valves, this code can't/won't handle those. Again, this would be a bomb.
 * Start/run capacitors are dinky, yes, but can kill you. Discharge them before you touch ANYthing, and be careful about shorting them.

One more tip (since it happened to me): Does your compressor fire up but sound like a Harley Davidson &copy;? Your relay is probably solid state and overtaxed. When this happens the fancier ones will "back off" and click back on after cooling down, but this may happen 200 to 1000ms later, causing hammering. Cheaper relays sometimes just turn into diodes, making the compressor operate at half phase. They don't run well at half phase, and will sound more like a jackhammer. The solution is to use your relay to actuate a mechanical 110ac coil relay or contactor.

Also make sure you know which wire goes where and does what before you start ripping out the stock controller. This should go without saying, but, well, it takes all kinds, doesn't it?

Now to the fun stuff. Here are some declarations of importance:

 * int adjustment - In degrees Celsius, this is meant to compensate for cheapo sensors that run hot or cold by a few degrees.
 * int tempBuffer - In degrees Celsius, how close to the "tempTarget" is "good enough". Defaults to 3c and tempTarget defaults to 7c, so 8c is considered "good enough", while 11c wouldn't be.
 * int tempTarget - How cold you want your whatever, it'll pound away as long as it takes trying to hit this sweet spot.

 * THAWTIME - How long to run the DEFROST cycle. This happens when the COOLING phase runs for a maximum interval (15 mins by default) but can't hit the target temperature.
 * MINRUN - In the cooling phase it's possible that after only 5-10 seconds the room will reach the target temperature, but the phase will continue until either this time has expired or the temperature hits ( tempTarget - tempBuffer )
 * MAXRUN - If the cooling phase has run this long (and we still haven't hit our target temperature), run a DEFROST phase before trying again.

 * DHT11_PIN - ... or DHT22. This is which pin you're using to talk to the sensor.
 * FAN_PIN - The pin going to the "low speed" fan relay.
 * FANMEDIUM_PIN - The pin going to the "medium speed" fan relay.
 * FANHIGH_PIN - The pin going to the "high speed" fan relay.
 * COMPRESSOR_PIN - This goes to your compressor relay. The logic pin is like 3 or 5v (can't recall) and dribbles out a few milliamps, so you may need to use two relays: one for small to medium voltage, one from medium to high.
 * LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE); - See the LiquidCrystal_I2C documentation, I still don't know what this means, but it works for me. Optionally you could strip out all the LCD code pretty easily.


