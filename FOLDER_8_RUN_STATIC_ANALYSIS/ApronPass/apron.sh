#!/bin/bash

APRON_INSTALL=${APRON_INSTALL:-/usr/local}
APRON_LIB=$APRON_INSTALL/lib

LLVM_APRON_PASS_HOME=${LLVM_APRON_PASS_HOME:-/home/apron/llvm_apron_pass}

LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$APRON_LIB:$LLVM_APRON_PASS_HOME/ApronPass:$LLVM_APRON_PASS_HOME/ApronPass/adaptors

APRON_MANAGER=${APRON_MANAGER:-ap_ppl}

opt -load $APRON_LIB/lib${APRON_MANAGER}_debug.so -load $APRON_LIB/libapron_debug.so -load $LLVM_APRON_PASS_HOME/ApronPass/adaptors/lib${APRON_MANAGER}_adaptor.so -load $LLVM_APRON_PASS_HOME/ApronPass/libapronpass.so -apron $*
