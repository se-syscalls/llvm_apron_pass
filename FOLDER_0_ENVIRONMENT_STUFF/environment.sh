export LLVM_INSTALL=
export APRON_INSTALL=/usr/local/
export APRON_LIB=$APRON_INSTALL/lib
export LLVM_INSTALL=/usr
export LLVM_LIB=$LLVM_INSTALL/lib
export LD_LIBRARY_PATH=$LLVM_LIB:$APRON_LIB
export LLVM_OPT=$LLVM_INSTALL/bin/opt
export CXXFLAGS=-I/usr/include/i386-linux-gnu/c++/4.8
