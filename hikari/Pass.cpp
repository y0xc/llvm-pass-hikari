#include <json.hpp>
#include "common.h"
#include "PassUtils.h"
#include "CryptoUtils.h"
#include "CallSite.h"

#include "AntiClassDump.h"
#include "AntiDebugging.h"
#include "AntiHooking.h"
#include "BogusControlFlow.h"
#include "ConstantEncryption.h"
#include "Flattening.h"
#include "FunctionCallObfuscate.h"
#include "FunctionWrapper.h"
#include "IndirectBranch.h"
#include "Split.h"
#include "StringEncryption.h"
#include "Substitution.h"

#define PLUGIN_NAME Hikari
#define PLUGIN_VER "1.0"

namespace llvm {
int EnableABIBreakingChecks;
int DisableABIBreakingChecks;
__attribute__((visibility("default"))) void Value::dump() const { 
    print(dbgs(), /*IsForDebug=*/true); dbgs() << '\n'; 
}
__attribute__((visibility("default"))) void Module::dump() const {
    print(llvm::errs(), 0, true);
}
};

class PLUGIN_NAME : public PassInfoMixin<PLUGIN_NAME> {
public:
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        errs() << "Running Hikari On " << M.getSourceFileName() << "\n";
        Config& conf = Config::inst();
        uint64_t aesSeed = conf.getGlobalConf().value("aesSeed", 0x1337);
        if (aesSeed != 0x1337) {
            cryptoutils->prng_seed(aesSeed);
        }
        else{
            cryptoutils->prng_seed();
        }
        nlohmann::json policy = conf.getModulePolicy(M);
        if (policy == nullptr) {
            return PreservedAnalyses::none();
        }
        const nlohmann::json& module_policy = policy[".mp"];
        const nlohmann::json& func_policy_map = policy[".fp"];
        TimerGroup tg("Hikari", "Hikari");
        Timer timer("total", "total", tg);
        timer.startTimer();
        bool dump = module_policy.value("enable-dump", false);
        if (dump) {
            dumpIR(".hikari.orig.ll", &M);
        }
        // antihook
        if (module_policy.value("has_antihook", false)) {
            Timer timer("antihook", "antihook", tg);
            timer.startTimer();
            string adhexrirpath = module_policy.value("adhexrirpath", "");
            AntiHook ah;
            ah.PreCompiledIRPath = adhexrirpath;
            ah.initialize(M);
            for (Function& F : M) {
                string func_name = GlobalValue::dropLLVMManglingEscape(F.getName()).str();
                if (!func_policy_map.contains(func_name)) {
                    continue;
                }
                const nlohmann::json& func_policy = func_policy_map[func_name];
                if (!func_policy.value("enable-antihook", false)) {
                    continue;
                }
                bool ah_inline = func_policy.value("ah_inline", true);
                bool ah_objcruntime = func_policy.value("ah_objcruntime", true);
                bool ah_antirebind = func_policy.value("ah_antirebind", false);
                errs() << ">>>> run antihook on " << func_name << "\n";
                bool dump = func_policy.value("dump", false);
                if (dump) {
                    dumpIR(".hikari.ah.orig.ll", &F);
                }
                ah.CheckInlineHook = ah_inline;
                ah.CheckObjectiveCRuntimeHook = ah_objcruntime;
                ah.AntiRebindSymbol = ah_antirebind;
                ah.runOnFunction(F);
                if (dump) {
                    dumpIR(".hikari.ah.ll", &F);
                }
            }
            timer.stopTimer();
            errs() << "antihook time: " << format("%.7f", timer.getTotalTime().getWallTime()) << "s\n";
        }
        // acd
        if (module_policy.value("enable-acdobf", false)) {
            Timer timer("acd", "acd", tg);
            timer.startTimer();
            bool acd_use_initialize = module_policy.value("acd-use-initialize", true);
            bool acd_rename_methodimp = module_policy.value("acd-rename-methodimp", false);
            AntiClassDump acd;
            acd.UseInitialize = acd_use_initialize;
            acd.RenameMethodIMP = acd_rename_methodimp;
            acd.doInitialization(M);
            errs() << ">>>> run acdobf\n";
            acd.runOnModule(M);
            timer.stopTimer();
            errs() << "acd time: " << format("%.7f", timer.getTotalTime().getWallTime()) << "s\n";
        }
        // fco
        if (module_policy.value("has_fco", false)) {
            Triple Tri(M.getTargetTriple());
            if (!Tri.isAndroid() && !Tri.isOSDarwin()) {
                errs() << "Unsupported Target Triple:"<< getModuleTriple(M) << "\n";
            } else {
                Timer timer("fco", "fco", tg);
                timer.startTimer();
                bool dlopen_flag = module_policy.value("fco_flag", -1);
                string fcoconfig = module_policy.value("fcoconfig", "+-x/");
                FunctionCallObfuscate fco;
                fco.SymbolConfigPath = fcoconfig;
                fco.dlopen_flag = dlopen_flag;
                fco.initialize(M);
                for (Function& F : M) {
                    string func_name = GlobalValue::dropLLVMManglingEscape(F.getName()).str();
                    if (!func_policy_map.contains(func_name)) {
                        continue;
                    }
                    const nlohmann::json& func_policy = func_policy_map[func_name];
                    if (!func_policy.value("enable-fco", false)) {
                        continue;
                    }
                    errs() << ">>>> run fco on " << func_name << "\n";
                    bool dump = func_policy.value("dump", false);
                    if (dump) {
                        dumpIR(".hikari.fco.orig.ll", &F);
                    }
                    fco.runOnFunction(F);
                    if (dump) {
                        dumpIR(".hikari.fco.ll", &F);
                    }
                }
                timer.stopTimer();
                errs() << "fco time: " << format("%.7f", timer.getTotalTime().getWallTime()) << "s\n";
            }
        }
        // antidbg
        if (module_policy.value("has_adb", false)) {
            Timer timer("antidbg", "antidbg", tg);
            timer.startTimer();
            string adbextirpath = module_policy.value("adbextirpath", "");
            AntiDebugging adb;
            adb.PreCompiledIRPath = adbextirpath;
            adb.initialize(M);
            for (Function& F : M) {
                string func_name = GlobalValue::dropLLVMManglingEscape(F.getName()).str();
                if (!func_policy_map.contains(func_name)) {
                    continue;
                }
                const nlohmann::json& func_policy = func_policy_map[func_name];
                if (!func_policy.value("enable-adb", false)) {
                    continue;
                }
                uint32_t adb_prob = func_policy.value("adb_prob", 40);
                if (adb_prob <= 1 || adb_prob > 100) {
                    errs() << "adb_prob must be 0 < x <= 100\n";
                    continue;
                }
                if (F.getName() != "ADBCallBack" && F.getName() != "InitADB") {
                    if (cryptoutils->get_range(100) <= adb_prob) {
                        errs() << ">>>> run antidbg on " << func_name << "\n";
                        bool dump = func_policy.value("dump", false);
                        if (dump) {
                            dumpIR(".hikari.adb.orig.ll", &F);
                        }
                        adb.runOnFunction(F);
                        if (dump) {
                            dumpIR(".hikari.adb.ll", &F);
                        }
                    }
                }
            }
            timer.stopTimer();
            errs() << "antidbg time: " << format("%.7f", timer.getTotalTime().getWallTime()) << "s\n";
        }
        // strenc
        if (module_policy.value("has_strcry", false)) {
            Timer timer("strenc", "strenc", tg);
            timer.startTimer();
            StringEncryption strenc;
            strenc.initialize(M);
            for (Function& F : M) {
                string func_name = GlobalValue::dropLLVMManglingEscape(F.getName()).str();
                if (!func_policy_map.contains(func_name)) {
                    continue;
                }
                const nlohmann::json& func_policy = func_policy_map[func_name];
                if (!func_policy.value("enable-strcry", false)) {
                    continue;
                }
                int strcry_prob = func_policy.value("strcry_prob", 100);
                if (strcry_prob <= 1 || strcry_prob > 100) {
                    errs() << "strcry_prob must be 0 < x <= 100\n";
                    continue;
                }
                errs() << ">>>> run strenc on " << func_name << "\n";
                strenc.ElementEncryptProb = strcry_prob;
                bool dump = func_policy.value("dump", false);
                if (dump) {
                    dumpIR(".hikari.strenc.orig.ll", &F);
                }
                strenc.runOnFunction(F);
                if (dump) {
                    dumpIR(".hikari.strenc.ll", &F);
                }
            }
            timer.stopTimer();
            errs() << "strenc time: " << format("%.7f", timer.getTotalTime().getWallTime()) << "s\n";
        }
        // split
        if (module_policy.value("has_splitobf", false)) {
            Timer timer("split", "split", tg);
            timer.startTimer();
            SplitBasicBlock split;
            for (Function& F : M) {
                string func_name = GlobalValue::dropLLVMManglingEscape(F.getName()).str();
                if (!func_policy_map.contains(func_name)) {
                    continue;
                }
                const nlohmann::json& func_policy = func_policy_map[func_name];
                if (!func_policy.value("enable-splitobf", false)) {
                    continue;
                }
                int split_num = func_policy.value("split_num", 2);
                if (split_num <= 1 || split_num > 10) {
                    errs() << "split_num must be 1 < x <= 10\n";
                    continue;
                }
                errs() << ">>>> run split on " << func_name << "\n";
                bool dump = func_policy.value("dump", false);
                if (dump) {
                    dumpIR(".hikari.split.orig.ll", &F);
                }
                split.SplitNum = split_num;
                split.runOnFunction(F);
                if (dump) {
                    dumpIR(".hikari.split.ll", &F);
                }
            }
            timer.stopTimer();
            errs() << "split time: " << format("%.7f", timer.getTotalTime().getWallTime()) << "s\n";
        }
        // bcf
        if (module_policy.value("has_bcfobf", false)) {
            Timer timer("bcf", "bcf", tg);
            timer.startTimer();
            BogusControlFlow bcf;
            for (Function& F : M) {
                string func_name = GlobalValue::dropLLVMManglingEscape(F.getName()).str();
                if (!func_policy_map.contains(func_name)) {
                    continue;
                }
                const nlohmann::json& func_policy = func_policy_map[func_name];
                if (!func_policy.value("enable-bcfobf", false)) {
                    continue;
                }
                int bcf_prob = func_policy.value("bcf_prob", 70);
                int bcf_loop = func_policy.value("bcf_loop", 1);
                int bcf_cond_compl = func_policy.value("bcf_cond_compl", 3);
                bool bcf_onlyjunkasm = func_policy.value("bcf_onlyjunkasm", false);
                bool bcf_junkasm = func_policy.value("bcf_junkasm", false);
                int bcf_junkasm_maxnum = func_policy.value("bcf_junkasm_maxnum", 4);
                int bcf_junkasm_minnum = func_policy.value("bcf_junkasm_minnum", 2);
                bool bcf_createfunc = func_policy.value("bcf_createfunc", false);
                if (bcf_loop <= 0) {
                    errs() << "bcf_loop must be x > 0\n";
                    continue;
                }
                if (bcf_prob <= 0 || bcf_prob > 100) {
                    errs() << "bcf_prob must be 0 < x <= 100\n";
                    continue;
                }
                if (bcf_cond_compl <= 0) {
                    errs() << "bcf_cond_compl must be x > 0\n";
                    continue;
                }
                if (bcf_junkasm_maxnum < bcf_junkasm_minnum) {
                    errs() << "bcf_junkasm_minnum must be bcf_junkasm_maxnum > bcf_junkasm_minnum\n";
                    continue;
                }
                if (!F.isPresplitCoroutine()) {
                    errs() << ">>>> run bcf on " << func_name << "\n";
                    bool dump = func_policy.value("dump", false);
                    if (dump) {
                        dumpIR(".hikari.bcf.orig.ll", &F);
                    }
                    bcf.ObfProbRate = bcf_prob;
                    bcf.ObfTimes = bcf_loop;
                    bcf.ConditionExpressionComplexity = bcf_cond_compl;
                    bcf.OnlyJunkAssembly = bcf_onlyjunkasm;
                    bcf.JunkAssembly = bcf_junkasm;
                    bcf.MaxNumberOfJunkAssembly = bcf_junkasm_maxnum;
                    bcf.MinNumberOfJunkAssembly = bcf_junkasm_minnum;
                    bcf.CreateFunctionForOpaquePredicate = bcf_createfunc;
                    bcf.runOnFunction(F);
                    if (dump) {
                        dumpIR(".hikari.bcf.ll", &F);
                    }
                }
            }
            timer.stopTimer();
            errs() << "bcf time: " << format("%.7f", timer.getTotalTime().getWallTime()) << "s\n";
        }
        // fla
        if (module_policy.value("has_cffobf", false)) {
            Timer timer("fla", "fla", tg);
            timer.startTimer();
            Flattening fla;
            for (Function& F : M) {
                string func_name = GlobalValue::dropLLVMManglingEscape(F.getName()).str();
                if (!func_policy_map.contains(func_name)) {
                    continue;
                }
                const nlohmann::json& func_policy = func_policy_map[func_name];
                if (!func_policy.value("enable-cffobf", false)) {
                    continue;
                }
                if (F.isPresplitCoroutine()) {
                    continue;
                }
                errs() << ">>>> run fla on " << func_name << "\n";
                bool dump = func_policy.value("dump", false);
                if (dump) {
                    dumpIR(".hikari.fla.orig.ll", &F);
                }
                fla.runOnFunction(F);
                lowerSwitch(F);
                if (dump) {
                    dumpIR(".hikari.fla.ll", &F);
                }
            }
            timer.stopTimer();
            errs() << "fla time: " << format("%.7f", timer.getTotalTime().getWallTime()) << "s\n";
        }
        // sub
        if (module_policy.value("has_subobf", false)) {
            Timer timer("sub", "sub", tg);
            timer.startTimer();
            Substitution sub;
            for (Function& F : M) {
                string func_name = GlobalValue::dropLLVMManglingEscape(F.getName()).str();
                if (!func_policy_map.contains(func_name)) {
                    continue;
                }
                const nlohmann::json& func_policy = func_policy_map[func_name];
                if (!func_policy.value("enable-subobf", false)) {
                    continue;
                }
                int sub_loop = func_policy.value("sub_loop", 1);
                int sub_prob = func_policy.value("sub_prob", 50);
                if (sub_loop <= 0) {
                    errs() << "sub_loop must be x > 0\n";
                    continue;
                }
                if (sub_prob <= 0 || sub_prob > 100) {
                    errs() << "sub_prob must be 0 < x <= 100\n";
                    continue;
                }
                errs() << ">>>> run sub on " << func_name << "\n";
                bool dump = func_policy.value("dump", false);
                if (dump) {
                    dumpIR(".hikari.sub.orig.ll", &F);
                }
                sub.ObfTimes = sub_loop;
                sub.ObfProbRate = sub_prob;
                sub.runOnFunction(F);
                if (dump) {
                    dumpIR(".hikari.sub.ll", &F);
                }
            }
            timer.stopTimer();
            errs() << "sub time: " << format("%.7f", timer.getTotalTime().getWallTime()) << "s\n";
        }
        // constenc
        if (module_policy.value("has_constenc", false)) {
            Timer timer("constenc", "constenc", tg);
            timer.startTimer();
            ConstantEncryption constenc;
            constenc.initialize(M);
            for (Function& F : M) {
                string func_name = GlobalValue::dropLLVMManglingEscape(F.getName()).str();
                if (!func_policy_map.contains(func_name)) {
                    continue;
                }
                const nlohmann::json& func_policy = func_policy_map[func_name];
                if (!func_policy.value("enable-constenc", false)) {
                    continue;
                }
                bool constenc_subxor = func_policy.value("constenc_subxor", false);
                int constenc_subxor_prob = func_policy.value("constenc_subxor_prob", 40);
                bool constenc_togv = func_policy.value("constenc_togv", false);
                int constenc_togv_prob = func_policy.value("constenc_togv_prob", 50);
                int constenc_times = func_policy.value("constenc_times", 1);
                if (constenc_subxor_prob <= 1 || constenc_subxor_prob > 100) {
                    errs() << "constenc_subxor_prob must be 0 < x <= 100\n";
                    continue;
                }
                if (constenc_togv_prob <= 1 || constenc_togv_prob > 100) {
                    errs() << "constenc_togv_prob must be 0 < x <= 100\n";
                    continue;
                }
                if (constenc_times <= 0) {
                    errs() << "constenc_times must be x > 0\n";
                    continue;
                }
                if (F.isPresplitCoroutine()) {
                    continue;
                }
                errs() << ">>>> run constenc on " << func_name << "\n";
                bool dump = func_policy.value("dump", false);
                if (dump) {
                    dumpIR(".hikari.constenc.orig.ll", &F);
                }
                constenc.SubstituteXor = constenc_subxor;
                constenc.SubstituteXorProb = constenc_subxor_prob;
                constenc.ConstToGV = constenc_togv;
                constenc.ConstToGVProb = constenc_togv_prob;
                constenc.ObfTimes = constenc_times;
                constenc.runOnFunction(F);
                if (dump) {
                    dumpIR(".hikari.constenc.ll", &F);
                }
            }
            timer.stopTimer();
            errs() << "constenc time: " << format("%.7f", timer.getTotalTime().getWallTime()) << "s\n";
        }
        // indibr
        if (module_policy.value("has_indibran", false)) {
            Timer timer("indibr", "indibr", tg);
            timer.startTimer();
            bool indibran_use_stack = module_policy.value("indibran-use-stack", true);
            bool indibran_enc_jump_target = module_policy.value("indibran-enc-jump-target", false);
            IndirectBranch indibr;
            indibr.UseStack = indibran_use_stack;
            indibr.EncryptJumpTarget = indibran_enc_jump_target;
            for (Function& F : M) {
                string func_name = GlobalValue::dropLLVMManglingEscape(F.getName()).str();
                if (!func_policy_map.contains(func_name)) {
                    continue;
                }
                const nlohmann::json& func_policy = func_policy_map[func_name];
                if (!func_policy.value("enable-indibran", false)) {
                    continue;
                }
                indibr.to_obf_funcs.insert(&F);
            }
            indibr.initialize(M);
            for (Function* pF : indibr.to_obf_funcs) {
                Function& F = *pF;
                string func_name = GlobalValue::dropLLVMManglingEscape(F.getName()).str();
                const nlohmann::json& func_policy = func_policy_map[func_name];
                errs() << ">>>> run indibr on " << func_name << "\n";
                bool dump = func_policy.value("dump", false);
                if (dump) {
                    dumpIR(".hikari.indibr.orig.ll", &F);
                }
                indibr.runOnFunction(F);
                if (dump) {
                    dumpIR(".hikari.indibr.ll", &F);
                }
            }
            timer.stopTimer();
            errs() << "indibr time: " << format("%.7f", timer.getTotalTime().getWallTime()) << "s\n";
        }
        // fw
        if (module_policy.value("has_funcwra", false)) {
            Timer timer("fw", "fw", tg);
            timer.startTimer();
            FunctionWrapper fw;
            for (Function& F : M) {
                string func_name = GlobalValue::dropLLVMManglingEscape(F.getName()).str();
                if (!func_policy_map.contains(func_name)) {
                    continue;
                }
                const nlohmann::json& func_policy = func_policy_map[func_name];
                if (!func_policy.value("enable-funcwra", false)) {
                    continue;
                }
                int fw_times = func_policy.value("fw_times", 2);
                int fw_prob = func_policy.value("fw_prob", 30);
                if (fw_times <= 0) {
                    errs() << "fw_times must be x > 0\n";
                    continue;
                }
                if (fw_prob <= 0 || fw_prob > 100) {
                    errs() << "fw_prob must be 0 < x <= 100\n";
                    continue;
                }
                errs() << ">>>> run fw on " << func_name << "\n";
                bool dump = func_policy.value("dump", false);
                if (dump) {
                    dumpIR(".hikari.fw.orig.ll", &F);
                }
                fw.ObfTimes = fw_times;
                fw.ProbRate = fw_prob;
                fw.runOnFunction(F);
                if (dump) {
                    dumpIR(".hikari.fw.ll", &F);
                }
            }
            timer.stopTimer();
            errs() << "fw time: " << format("%.7f", timer.getTotalTime().getWallTime()) << "s\n";
        }
        if (dump) {
            dumpIR(".hikari.ll", &M);
        }
        timer.stopTimer();
        errs() << "Hikari total time: " << format("%.7f", timer.getTotalTime().getWallTime()) << "s\n";
        return PreservedAnalyses::all();
    };
    static bool isRequired() { return true; }
};

extern "C" __attribute__((visibility("default")))
LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        .APIVersion = LLVM_PLUGIN_API_VERSION,
        .PluginName = STR(PLUGIN_NAME),
        .PluginVersion = PLUGIN_VER,
        .RegisterPassBuilderCallbacks = [](PassBuilder &PB) {
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager& MPM
#if LLVM_VERSION_MAJOR >= 12
                , OptimizationLevel Level
#endif
                ) {
                    MPM.addPass(PLUGIN_NAME());
            });
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager& MPM, ArrayRef<PassBuilder::PipelineElement>) {
                    MPM.addPass(PLUGIN_NAME());
                    return true;
            });
        }
    };
}

