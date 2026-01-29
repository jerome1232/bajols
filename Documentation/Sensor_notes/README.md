# Sensor Addresses

|Address|Sensor Name|Width|Scale|Affects|Units|
|-----:|:---------:|:---:|:---:|:-----:|:--|
|0x00|IntV6|2|1E2|-|Volts|
|0x01|Tem6|2|1E2 + 400|-|Celsius|
|0x02|Mot2|2|-|-|RPM|
|0x03|ExtV4|2|1E2|-|Volts|
|0x04|Indexed|2|-|-|
|0x05|
|0x06|
|0x07|
|0x08|
|0x09|
|0x0A|
|0x16|
|0x41|PRES3|4|1E2|ALT3|hPa|
|0x7C|Odo12|2|?|-|Km|
|0x7D|Odo22|2|?|-|Km|
|0x7E|Spe2|2|1E2|-|Km/h
|0xF9|ALT3|4|1E2*|-|Meters


*0xF9 goes haywire when written to, I think it's meant to be calculated from pressure and not directly written to

I believe the number after each sensor is actually the index of the sensor.

All addresses between 0x03 and 0x41 exclusive appear to be unamed, unscaled sensors

---

- Critical sensors
    - Depth
        - pressure sensor
    - Heading
        - 3 axis magnetometer
        - Bonus points if it has an accelerometer built in
            - An accelerometer may help make speed calculations more accurate (if we are accelerating we haven't reached full speed at current rpm)
    - Speed
        - Encoder sensor
        - Calculated based on rpm, and possible incorporating an accelerometer to increase acuracy
    - Voltage
        - We will want to know our voltage
            - The only way I know to do this is voltage divder into analog input. Is there a better way?
        - Maybe incorporate automated actions when voltage dips low (blow ballast, atempt return to 'home' position)
- Nice-to-have
    - Distance from home (will be error prone calculating)
