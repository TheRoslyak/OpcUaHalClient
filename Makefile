# Compiler
CC = g++

# Compilation flags
CFLAGS = -std=c++17 -Wall -fPIC -DULAPI

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
OUTPUT = /usr/lib/linuxcnc/modules/opcuaclient

# Rule to compile all source files
all:
	$(CC) $(CFLAGS) $(INCLUDES) $(SRCS) $(LDFLAGS) $(LIBS) -o $(OUTPUT)