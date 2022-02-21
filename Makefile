CXXFLAGS = -Wall -O2 -fPIC -rdynamic -shared -std=c++14
LDFLAGS =

all: ldpreload-unixbind.so

ldpreload-unixbind.so: ldpreload-unixbind.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -ldl -o $@ $<
