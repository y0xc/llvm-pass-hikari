#ifndef __COMMON__
#define __COMMON__

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/NoFolder.h"
#if LLVM_VERSION_MAJOR >= 16
#include "llvm/IR/TypedPointerType.h"
#endif
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Timer.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO/Attributor.h"
#if LLVM_VERSION_MAJOR <= 15
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#endif
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/GlobalStatus.h"
#include "llvm/Transforms/Utils/Local.h"
#if LLVM_VERSION_MAJOR >= 12
#include "llvm/Transforms/Utils/LowerSwitch.h"
#endif
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <stdint.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <string>
#include <unordered_set>

using namespace llvm;
using namespace std;

#define STR(x) #x

#if LLVM_VERSION_MAJOR <= 12
#define ends_with_insensitive endswith_lower
#elif LLVM_VERSION_MAJOR <= 15
#define ends_with_insensitive endswith_insensitive
#endif

#if LLVM_VERSION_MAJOR <= 13
#define GetElementType(x) x->getPointerElementType()
#elif LLVM_VERSION_MAJOR <= 16
#define GetElementType(x) x->getNonOpaquePointerElementType()
#else
#define GetElementType(x) cast<TypedPointerType>(x)->getElementType()
#endif

#if LLVM_VERSION_MAJOR <= 13
#define OptimizationLevel PassBuilder::OptimizationLevel
#endif

#if LLVM_VERSION_MAJOR <= 15
#define starts_with startswith
#endif

#if LLVM_VERSION_MAJOR <= 20
#define getModuleTriple(M) ((M).getTargetTriple())
#else
#define getModuleTriple(M) ((M).getTargetTriple().str())
#endif

#endif 

