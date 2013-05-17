Power Meter with Logging
==========

Calculating the power by measure the time elapsed between rounds from an electromechanical induction watt-hour meter (in specific a metal disc / turning wheel which is made to rotate at a speed proportional to the power passing through the meter) and send it over to a Raspberry Pi, which logs the data.

Transmitter
-------

The [transmitter](https://github.com/simonstamm/powermeter/tree/master/power_transmitter) checks the metal disc / turning wheel (in German: Ferrarisscheibe) with an reflective optical sensor with transistor output (CNY70) and transmits data to the receiver.

* [Transmitter](https://github.com/simonstamm/powermeter/tree/master/power_transmitter) -- C/C++ on a JeeNode

Receiver
-------

The [receiver](https://github.com/simonstamm/powermeter/tree/master/serial_link) parses the incoming data from the transmitter and logs it. In particiular, it'll currently submit the power to an EmonCMS-API.

* [Receiver](https://github.com/simonstamm/powermeter/tree/master/serial_link) -- PHP on a Raspberry Pi with RFM12Pi