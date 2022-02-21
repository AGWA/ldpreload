CXXFLAGS = -Wall -O2 -fPIC -rdynamic -shared
LDFLAGS =

all: ldpreload-prebind.so ldpreload-unixbind.so

%.so: %.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -ldl -o $@ $<

clean:
	rm -f *.so

.PHONY: all clean
