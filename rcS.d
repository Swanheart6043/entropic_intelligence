#!/bin/sh
# vim /etc/init.d/rcS
echo "Loading LED driver..."
insmod /warm_home/led.ko
if [ $? -eq 0 ]; then
    echo "LED driver loaded successfully"
else
    echo "Failed to load LED driver"
fi

echo "Loading Buzzer driver..."
insmod /warm_home/buzzer.ko
if [ $? -eq 0 ]; then
    echo "Buzzer driver loaded successfully"
else
    echo "Failed to load Buzzer driver"
fi

echo "Loading Adc driver..."
insmod /warm_home/adc.ko
if [ $? -eq 0 ]; then
    echo "Adc driver loaded successfully"
else
    echo "Failed to load Adc driver"
fi

echo "Loading App..."
./warm_home/warm_home_app

echo "Loading Backend..."
./warm_home/warm_home_backend

echo "Loading Boa..."
./boa/boa