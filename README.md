# dryer

An arduino based clothes dryer controller.

## Algorithm

The algorithm is based on state machine, comprising the following states:

* Off
* Heat
* Vent
* Observe
* Done

When you flip the switch to ON, the controller starts on state "Heat".

## Hardware used

* Ardiuno, whatever model
* One clothes dryer cabinet
* One duct fan, for forced air evacuation
* Two 240V 10A DC relays
* Two DHT22 temperature and humidity sensors
* One 16x2 character LCD
* One 5VDC power supply (I ripped apart an old USB charger)
* One on/off switch
* One 240V power contact

Optional bonus items:

* Circulation fans inside the dryer cabinet
b