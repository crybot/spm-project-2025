CXX := g++
# CXXFLAGS := -std=c++23 -O3 -pthread -Wall -I$(FASTFLOW_DIR)
CXXFLAGS := -std=c++23 -O3 -pthread -Wall
TARGET := main
SRC := main.cpp

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)


