CXX= g++
CXXFLAGS= -std=c++11
LDFLAGS= -lpthread

BIN= fastproxy
OBJS= \
			Cmd.o ConfigChunk.o ConfigError.o \
			FastProxy.o SockInfo.o Common.o \
			Config.o ConfigValue.o Server.o
$(BIN): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $^
	
clean:
	rm -r $(OBJS) 2>/dev/null;