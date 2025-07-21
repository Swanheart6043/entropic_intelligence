savedcmd_/home/swanheart/codes/drivers/led/led.mod := printf '%s\n'   led.o | awk '!x[$$0]++ { print("/home/swanheart/codes/drivers/led/"$$0) }' > /home/swanheart/codes/drivers/led/led.mod
