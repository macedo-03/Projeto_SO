# Makefile

# Compiler
CC=gcc

# Compiler flags
CFLAGS= -Wall -Wextra

# Targets
all: sensor user_console home_iot

custumio.o: costumio.c costumio.h
	$(CC) $(CFLAGS) -c costumio.c



sensor: sensor.o costumio.o
	$(CC) $(CFLAGS)  $^ -o $@

sensor.o: sensor.c costumio.h
	$(CC) $(CFLAGS) -c sensor.c 



home_iot: system_manager.o costumio.o
	$(CC) $(CFLAGS) -pthread $^ -o $@

system_manager.o: system_manager.c costumio.h
	$(CC) $(CFLAGS) -c system_manager.c



user_console: user_console.o costumio.o
	$(CC) $(CFLAGS) $^ -o $@

user_console.o: user_console.c costumio.h
	$(CC) $(CFLAGS) -c user_console.c


# Clean target
clean:
	rm -f *.o sensor user_console home_iot