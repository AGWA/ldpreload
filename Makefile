CXXFLAGS = -Wall -O2 -fPIC -rdynamic -shared -std=c++14
LDFLAGS =

all: tlsdify.so

tlsdify.so: tlsdify.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -ldl -o $@ $<
