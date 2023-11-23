# Compiler
CC = g++

# Compilation flags
CFLAGS = -std=c++17 -Wall -fPIC -DRTAPI

# Paths to search for header files
INCLUDES = -Iinclude \
	   	   -I/usr/include/linuxcnc 


# Link flags
LDFLAGS = -shared 

# Libraries for linking
LIBS = -lyaml-cpp -llinuxcnchal -lopen62541



# Source files
SRCS = main.cpp 


# Output file
OUTPUT = /usr/lib/linuxcnc/modules/opcuaclient.so

# Rule to compile all source files
all:
	$(CC) $(CFLAGS) $(INCLUDES) $(SRCS) $(LDFLAGS) $(LIBS) -o $(OUTPUT)
