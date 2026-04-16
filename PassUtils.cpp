#include "PassUtils.h"

uint64_t get_ts_ms() {
    uint64_t us = chrono::system_clock::now().time_since_epoch().count();
    return us / 1000;
}

vector<string> split(const string& s, const string& d) {
    size_t pos_start = 0;
    size_t pos_end;
    size_t delim_len = d.length();
    string token;
    vector<string> res;
    while ((pos_end = s.find(d, pos_start)) != string::npos) {
        token = s.substr(pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back(token);
    }
    token = s.substr(pos_start);
    if (token.length() > 0) {
      res.push_back(token);
    }
    return res;
}

bool startswith(const string& str, const string& prefix) {
    return prefix.size() <= str.size() && equal(prefix.cbegin(), prefix.cend(), str.cbegin());
}

string ts_to_date(int64_t ts) {
    chrono::duration<int64_t, ratio<1,1>> dur(ts);
    auto dc = chrono::duration_cast<chrono::system_clock::duration>(dur);
    auto tp = chrono::system_clock::time_point(dc);
    time_t in_time_t = chrono::system_clock::to_time_t(tp);
    string format = "%Y-%m-%d %H:%M:%S";
    char buff[64];
    strftime(buff, 64, format.c_str(), localtime(&in_time_t));
    return buff;
}

string getModuleFile(Module& M) {
    string file = M.getSourceFileName();
    if (file.empty()) {
        file = M.getName().str();
    }
    return file;
}

bool isNameReserved(Value* V) {
    StringRef name = V->getName();
    if (name.starts_with("llvm.") || name.starts_with("clang.")) {
        return true;
    }
    return false;
}

bool ensure_dir(const string& path) {
    string dir = filesystem::path(path).parent_path().string();
    if (filesystem::exists(dir)) {
        return true;
    }
    std::error_code err;
    return filesystem::create_directories(dir, err);
}

bool dumpIR(const string& suffix, Function* F) {
    string name_prefix = GlobalValue::dropLLVMManglingEscape(F->getName()).str();
    filesystem::path prefix = filesystem::path("llvm_dump") / name_prefix;
    string path = prefix.string() + suffix;
    if (!ensure_dir(path)) {
        return false;
    }
    string content;
    raw_string_ostream stream(content);
    cast<Value>(F)->print(stream, true);
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        return false;
    }
    ofs.write(content.data(), content.size());
    return ofs.good();
}

bool dumpIR(const string& suffix, Module* M) {
    string name_prefix = filesystem::path(M->getSourceFileName()).filename().string();
    filesystem::path prefix = filesystem::path("llvm_dump") / name_prefix;
    string path = prefix.string() + suffix;
    if (!ensure_dir(path)) {
        return false;
    }
    string content;
    raw_string_ostream stream(content);
    M->print(stream, nullptr, false, true);
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        return false;
    }
    ofs.write(content.data(), content.size());
    return ofs.good();
}

void lowerSwitch(Function& F) {
    // LLVM_VERSION_MAJOR >= 12
    PassBuilder PB;
    FunctionAnalysisManager FAM;
    FunctionPassManager FPM;
    PB.registerFunctionAnalyses(FAM);
    FPM.addPass(LowerSwitchPass());
    FPM.run(F, FAM);
}

