kaleidoscope: kaleidoscope.hpp kaleidoscope.cpp MCJITHelper.hpp
	clang++ -fno-rtti -std=c++11 -O0 -g `/homebrew/opt/llvm/bin/llvm-config --cxxflags --ldflags --system-libs --libs all` -o kaleidoscope kaleidoscope.cpp

toy: toy.cpp
	clang++ -fno-rtti -std=c++11 -O0 -g `/homebrew/opt/llvm/bin/llvm-config --cxxflags --ldflags --system-libs --libs all` -o toy toy.cpp

