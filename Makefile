# Makefile

# Compiler
CC=gcc

# Compiler flags
CFLAGS= -Wall -Wextra

# Targets
all: sensor system_manager user_console

sensor: sensor.o
	$(CC) $(CFLAGS) $^ -o $@

sensor.o: sensor.c
	$(CC) $(CFLAGS) -c sensor.c

system_manager: system_manager.o
	$(CC) $(CFLAGS) $^ -o $@


system_manager.o: system_manager.c
	$(CC) $(CFLAGS) -c system_manager.c

user_console: user_console.o
	$(CC) $(CFLAGS) $^ -o $@


user_console.o: user_console.c
	$(CC) $(CFLAGS) -c user_console.c


# Clean target
clean:
	rm -f *.o 