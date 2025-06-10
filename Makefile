deltahack.so: hack.cpp
	$(CXX) --std=c++11 -I./3rdparty/xbyak/xbyak -fPIC -shared hack.cpp -o deltahack.so
