SRC=$(wildcard *.cpp)

-include deltahack.d
deltahack.so: ${SRC}
	$(CXX) --std=c++20 -MMD -s -Os -Wl,--gc-sections -flto -I./3rdparty/xbyak/xbyak -fPIC -shared hack.cpp -o deltahack.so

clean:
	rm -f deltahack.d deltahack.so