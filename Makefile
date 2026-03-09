# CMPE 412 – Multi-Server Queue Simulation
# Build: make
# Clean: make clean

CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall
TARGET   = simulation
SRC      = main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	del /Q $(TARGET).exe 2>nul || rm -f $(TARGET)

.PHONY: all clean
