# Compiler
CC = g++

# Compilation flags
CFLAGS = -std=c++17 -Wall -fPIC -DULAPI

# Paths to search for header files
INCLUDES = -Iinclude \
	   	   -I/usr/include/linuxcnc 


# Link flags
LDFLAGS =  

# Libraries for linking
LIBS = -lyaml-cpp -llinuxcnchal -lopen62541



# Source files
SRCS = main.cpp 


# Output file
OUTPUT =  /usr/bin/opcuaclient

# Rule to compile all source files
all:
	$(CC) $(CFLAGS) $(INCLUDES) $(SRCS) $(LDFLAGS) $(LIBS) -o $(OUTPUT)