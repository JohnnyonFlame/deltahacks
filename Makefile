deltahack.so: hack.cpp
	$(CXX) --std=c++20 -s -Os -Wl,--gc-sections -flto -I./3rdparty/xbyak/xbyak -fPIC -shared hack.cpp -o deltahack.so
