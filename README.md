# IoT_individual_assignment
Repo for all the code of the individual assignment for the IoT course
by Filippo Zanei

## Setup and fondamentals
In order to achieve all the tasks required for the exercise, we need at least one ESP32 with a LoRa antenna, a signal generator that simulates the signal a potential sensor would send to the ESP32, that can be another ESP32 or a computer (potentially one ESP32 can be enough if it has boh DAC and ACD on board, but our Heltech ESP3-V3 was lacking of the DAC, futhermore to correctly simulate the sensor signal, doing it directly on the same device that is also receiving and analysing it is not really rapresentative of the reality we want simulate, expecially reguarding power consumption).  
In our specific case we ended up using a ESP32 from AZ-Delivery, equipped only with the WiFi but with both DAC and ADC, as the signal generator, while we used the Heltech one as the receiver and analyser. This one is also the one that will transmit all the aggregated data to the servers.
The first problem we encountered with this set up came once 

##Task1: max sampling frequency
For this task we created a stand alone firmware to stress the Heltech ESP32 via sampling using the analogRead() function, as it will be used in the next steps of the exercise. The idea is simple: we count as many samples the ESP32 is able to take in a certain time and with those information we calculate the sampling rate, aka the sampling frequency. In order to get the exact frequency we measure also the exact time interval that is used in total to sample inside the theoretical window of sampling, which gives us the frequency with the following formula  
$$f_s = \frac{# of samples}{t_f - t_i}$$.  
From repeated experiments we found a $f_s \sim 16454 Hz$, with oscillations between $\sim 16448 Hz$ and $\sim 16455 Hz$.
