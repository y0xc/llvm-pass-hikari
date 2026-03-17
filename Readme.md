[中文说明](#简介)

## Introduction

This project is based on `LLVM NewPass` and turns `original OLLVM` and `Hikari` into `standalone Pass`, with the following goals:
* Verify that `IR` level obfuscation can be implemented using independent `Pass`
* Assist in validating another project: `ida_mcp`

This project has been tested on `macOS 15` with `LLVM 15–19`. Because it preserves the original code as much as possible (with minimal modifications), it does not provide enhancements in obfuscation strength. If you’re interested in stronger obfuscation capabilities, you can follow my other project: `SLLVM`.

Pros and cons of implementing obfuscation as independent `Pass`:
* `Pro #1:` If you already installed `LLVM` via `Homebrew` (macOS), `apt` (Debian), etc., you don’t need to build `LLVM` yourself—faster development iteration
* `Pro #2:` `Pass` can be compiled quickly and the output binaries are small
* `Con #1:` You must use a matching `Clang`—at minimum, the major `LLVM` version must match

## Strategy

In many real-world projects, it’s often impossible to obfuscate the entire project due to reasons such as:
* The project is large, has many dependencies, or uses many `header-only` libraries; obfuscating too much unnecessary code makes the final binary too large
* The project is large with many dependencies; using flattening (or other techniques) on unnecessary code makes compile time too long or even causes the build to hang
* Obfuscating complex algorithms increases runtime overhead significantly; flattening typically increases runtime by `10%+`
* Excessive obfuscation may violate `AppStore` / `GooglePlay` policies (e.g., may prevent publishing)

In practice, you often need different obfuscation levels based on the importance of modules/functions. Therefore, a policy is needed to specify which modules/functions use which obfuscation options. Common strategy configuration approaches in open-source `OLLVM` include:
* Add command-line options only to modules that need obfuscation (e.g., `-llvm -fla`). This works with any compiler frontend that supports LLVM command-line options.
* Use environment variables to specify obfuscation options
* Annotate functions that should be obfuscated, e.g. `__attribute((__annotate__(("fla"))))` (new syntax: `[[clang::annotate("fla")]]`). This only supports `C/C++`; `Objective-C` and other languages do not support it.
* Use a marker function inside functions that should be obfuscated, as shown below (this supports `Objective-C`):
```objc
extern void hikari_fla(void);
@implementation foo2:NSObject
+(void)foo{
  hikari_fla();
  NSLog(@"FOOOO2");
}
@end
```

### `Pass` Policy Syntax

All of the approaches above have limitations: they either require too much code modification, cannot control at function granularity, or only support specific languages. This project uses a configuration file to specify which functions and modules should be obfuscated, making it compatible with most compiler frontends and development languages.

The policy file is `policy.json` in the working directory, provided by the user. Fields are:
|                 |Type      |Meaning                               |Required|Note    |
|:-               |:-        |:-                                    |:-      |:-      |
|`globals`        |dictionary|Global options                        |No      |        |
|`policy_map`     |dictionary|Mapping of `policy name -> option set`|Yes     |        |
|`policies`       |array     |All policy rules                      |Yes     |        |
|`policies.module`|string    |Regex match for module name           |Yes     |        |
|`policies.func`  |string    |Regex match for function name         |No      |Used to distinguish module-level vs function-level policies|
|`policies.policy`|string    |Policy name, refers to `policy_map`   |Yes     |        |

Rules:
* A policy that specifies only the `module` field is a `module-level` policy; a policy that specifies both `module` and `func` is a `function-level` policy
* Forward override: For `function-level` policies in the `policies` array, if a later item’s matched `module`/`func` set is a subset of an earlier item’s matches, it overrides the earlier policy. The same applies to `module-level` policies.
* Inheritance is supported: the base strategy can be specified in the `base` field.
* Comments supported: Any optional sub-field supports `#` comments, e.g. `"#enable-strcry": true`
* Name demangling supported: My other project `SLLVM` supports matching `module`/`func` even under name obfuscation for languages like `C++`/`Swift`
* The `enable-dump` field in a policy is used to print module IR / function IR (depending on the policy type)

```json
{
    "globals": {
        "aesSeed": 4919
    },
    "policy_map": {
        "my_mod_pol": {
            "acd-use-initialize": true,
            ...
        },
        "my_func_pol": {
            "enable-strcry": true,
            "enable-splitobf": false,
            "split_num": 2,
            ...
        }
    },
    "policies": [
        {
            "module": ".*",
            "policy": "my_mod_pol" // module level policy
        },
        {
            "module": ".*",
            "func": ".*",
            "policy": "my_func_pol" // function level policy
        }
    ]
}
```

In the `test` directory of each subproject (`original OLLVM / Hikari / ...`), you can find a `policy.json` that contains all configurable fields.

## Build

If you can locate LLVM’s build directory (for example, if you installed LLVM via `Homebrew` (macOS) or `apt` (Debian), you can find `./cmake/AddLLVM.cmake`), then you can skip building LLVM:
```bash
# optional: -DLLVM_ENABLE_PROJECTS=clang -DCMAKE_BUILD_TYPE=Debug
cmake -S llvm -G Ninja -B llvm_build_dir
cmake --build llvm_build_dir
```

Example: macOS + original OLLVM
```bash
git clone https://github.com/lich4/llvm-pass-hikari
cd llvm-pass-hikari
export LLVM_DIR=/path/to/llvm_build_dir
cmake -S obfuscator -G Ninja -B obfuscator/build
cmake --build obfuscator/build
```

## Test

Example: macOS + original OLLVM. Notes:
* The `Pass` must match the major version of the corresponding `LLVM/Clang`
* If you get header-related errors when testing `objc++`, ensure the open-source `clang` version matches the `clang` version used by `Xcode`

```bash
cd test
# test c
/path/to/llvm_build_dir/bin/clang -isysroot `xcrun --sdk macosx --show-sdk-path` -fpass-plugin=../build/Hikari.dylib test.c -o test
# test cpp
/path/to/llvm_build_dir/bin/clang -isysroot `xcrun --sdk macosx --show-sdk-path` -fpass-plugin=../build/Hikari.dylib -std=c++11 -stdlib=libc++ -lc++ test.cpp -o test
# test objc
/path/to/llvm_build_dir/bin/clang -isysroot `xcrun --sdk macosx --show-sdk-path` -fpass-plugin=../build/Hikari.dylib -framework Foundation  test.m -o test
# test objc++
/path/to/llvm_build_dir/bin/clang -isysroot `xcrun --sdk macosx --show-sdk-path` -fpass-plugin=../build/Hikari.dylib -framework Foundation -std=c++11 -stdlib=libc++ -lc++ test.mm -o test
```

## Adapting to Xcode

Because the open-source LLVM `Clang` differs from Xcode’s `Clang`, a dynamic Pass cannot be used directly in Xcode. You can consider the following approaches:

* In `Xcode - Build Settings`, set the `CC` variable to an open-source `clang` (e.g., `brew install llvm@15`), and set `Other C Flags` to include `-fpass-plugin` pointing to the Pass path
* In `Xcode - Build Settings`, set `CC` to a build script. The script logic is: “use `clang -emit-llvm` to generate bitcode first, then run `opt` to execute the Pass, and finally run `clang -c` to produce the object file as usual.” This approach can use Xcode’s built-in `Apple clang`, and has better compatibility with the `arm64e` architecture. (This repo includes my own script: `xcode_cc.sh`.)
* Develop a dynamic Pass specifically for Xcode’s built-in `Apple clang`, and set `Other C Flags` to include `-fpass-plugin` pointing to the Pass. This is more complex (you must handle many symbol conflicts) and is only suitable for developers highly proficient with LLVM. This approach can also use Apple clang directly and has better compatibility with `arm64e`.



## 简介

本项目基于`LLVM NewPass`实现`原版OLLVM`和`Hikari`的`Pass`化，有以下目标:
* 验证可以用独立`Pass`实现`IR`层混淆
* 用于辅助验证另一个项目`ida_mcp`

本项目在`MacOS15`+`LLVM15-19`上测试，因为最大限度保留原始代码未改动因此不会有混淆功能方面的增强。对更强的混淆功能有兴趣的可以关注我的另一个项目`SLLVM`

使用独立`Pass`实现混淆的优缺点：
* 优点1：如果已经用`Homebrew`(Mac)/`apt`(Debian)等安装过`LLVM`则无需编译`LLVM`，开发速度快
* 优点2：`Pass`本身编译速度快，编译出的文件小
* 缺点1：搭配对应的`Clang`一起使用，至少保证`LLVM`大版本匹配

## 策略

在很多实际项目中，由于以下原因无法对整个项目完全混淆：
* 项目较大，依赖较多，或使用了很多`header-only`的库，混淆了很多不需要混淆的代码，导致编译出来的二进制过大
* 项目较大，依赖较多，使用了平坦化(或其他方式)混淆了很多不需要混淆的代码，导致编译时间过久甚至卡死
* 混淆了复杂算法，导致运行时耗时比正常大很多，一般使用平坦化后耗时会增加10%以上
* 混淆过多可能不允许上架`AppStore`/`GooglePlay`等

实际操作时, 常常需要根据模块/函数的重要性使用不同程度的混淆，因此需要配置策略来指定哪些模块/函数需要用哪种混淆，而开源的`OLLVM`常见设置策略的方式如下：
* 对需要混淆的模块单独指定命令行参数，如`-llvm -fla`，这种方式兼容所有支持`LLVM`命令行参数的编译器前端
* 使用环境变量指定混淆参数
* 对需要混淆的函数指定注解，如`__attribute((__annotate__(("fla"))))`(新式语法`[[clang::annotate("fla")]]`)，这种方式仅支持`C/C++`，`Objective-C`和其他语言均不支持
* 对需要混淆的函数指定标记函数，如下所示，这种方式支持`Objective-C`
```objc
extern void hikari_fla(void);
@implementation foo2:NSObject
+(void)foo{
  hikari_fla();
  NSLog(@"FOOOO2");
}
@end
```

### `Pass`策略语法

以上方式均有局限性，或对代码改动太大，或无法控制到函数粒度，或只支持特定语言。本项目使用配置文件来指定需要混淆的函数和模块，兼容大部分编译器前端及开发语言。
策略文件为工作目录下名为`policy.json`的文件，需用户提供，字段如下：
|               |字段类型   |字段含义                   |必须| 特殊说明              |
|:-             |:-        |:-                       |:-  |:-                   |
|policy_map     |字典       |`策略名 - 混淆选项集合`的映射|是  |                      |
|policies       |数组       |所有策略                  |是  |                      |
|policies.module|字符串     |正则匹配模块名             |是  |                      |
|policies.func  |字符串     |正则匹配函数名             |否  |用以区分模块级/函数级策略 |
|policies.policy|字符串     |策略名，对应`policies`     |是  |                      |

语法如下：
* 仅指定`module`字段的策略为模块级策略，同时指定`module`/`func`字段的策略为函数级策略
* 前向覆盖：对于`policies`数组中的函数级策略，如果后面的项匹配的`module`/`func`是在其之前项匹配的子集，则覆盖之前的策略；模块级策略同理
* 支持继承：通过`base`字段指定基策略
* 支持注释：非必须的子字段，都支持`#`注释，如`"#enable-strcry": true`
* 支持名称混淆：本人另一个项目`SLLVM`支持`c++`/`swift`等语言名称混淆情况下的`module`/`func`匹配
* 策略中`enable-dump`字段用于打印模块IR/函数IR(取决于策略类型)

```json
{
    "globals": {
        "aesSeed": 4919
    },
    "policy_map": {
        "my_mod_pol": {
            "acd-use-initialize": true,
            ...
        },
        "my_func_pol": {
            "enable-strcry": true,
            "enable-splitobf": false,
            "split_num": 2,
            ...
        }
    },
    "policies": [
        {
            "module": ".*",
            "policy": "my_mod_pol" // 模块级策略
        },
        {
            "module": ".*",
            "func": ".*",
            "policy": "my_func_pol" // 函数级策略
        }
    ]
}
```

在每个子工程(`原版OLLVM`/`Hikari`/...)中的`test`目录可找到`policy.json`，其中包含了所有可配置字段

## 编译

若能找到LLVM对应的编译目录位置(如已经用`Homebrew`(Mac)/`apt`(Debian)等安装过`LLVM`，可定位到`./cmake/AddLLVM.cmake`)则可跳过编译
```bash
# optional: -DLLVM_ENABLE_PROJECTS=clang -DCMAKE_BUILD_TYPE=Debug
cmake -S llvm -G Ninja -B llvm_build_dir
cmake --build llvm_build_dir
```

以Mac系统+原版OLLVM为例
```bash
git clone https://github.com/lich4/llvm-pass-hikari
cd llvm-pass-hikari
export LLVM_DIR=/path/to/llvm_build_dir
cmake -S obfuscator -G Ninja -B obfuscator/build
cmake --build obfuscator/build
```

## 测试

以Mac系统+原版OLLVM为例，注意：
* `Pass`要匹配对应的`LLVM/Clang`大版本
* 如果测试`objc++`时报头文件相关错误则需要开源`clang`版本和`Xcode`对应`clang`版本一致

```bash
cd test
# test c
/path/to/llvm_build_dir/bin/clang -isysroot `xcrun --sdk macosx --show-sdk-path` -fpass-plugin=../build/Hikari.dylib test.c -o test
# test cpp
/path/to/llvm_build_dir/bin/clang -isysroot `xcrun --sdk macosx --show-sdk-path` -fpass-plugin=../build/Hikari.dylib -std=c++11 -stdlib=libc++ -lc++ test.cpp -o test
# test objc
/path/to/llvm_build_dir/bin/clang -isysroot `xcrun --sdk macosx --show-sdk-path` -fpass-plugin=../build/Hikari.dylib -framework Foundation  test.m -o test
# test objc++
/path/to/llvm_build_dir/bin/clang -isysroot `xcrun --sdk macosx --show-sdk-path` -fpass-plugin=../build/Hikari.dylib -framework Foundation -std=c++11 -stdlib=libc++ -lc++ test.mm -o test
```

## 适配`Xcode`

### Clang

由于开源`LLVM`的`Clang`不同于`Xcode`的`Clang`，因此动态`Pass`不能直接用于`Xcode`，可以考虑以下方式：
* 在`Xcode - Build Settings`中创建`CC`变量为开源`clang`(如`brew install llvm@15`)，且指定`Other C Flags`的`-fpass-plugin`为对应`Pass`路径
* 在`Xcode - Build Settings`中创建`CC`变量为编译脚本，脚本逻辑为"先用`clang -emit-llvm`参数生成`bitcode`，然后运行`opt`执行`Pass`，最后用`clang -c`生成原本要生成的`obj`文件"。此种方式可以直接使用`Xcode`自带的`Apple clang`，能比较好的兼容`arm64e`架构. (本项目中公开本人自用的`xcode_cc.sh`)
* 直接针对`Xcode`自带的`Apple clang`开发动态`Pass`，在`Xcode`中指定`Other C Flags`指定`-fpass-plugin`为`Pass`路径。此种方式复杂度较高，需要处理大量符号冲突，只适合精通`LLVM`的开发者。此种方式可以直接使用`Xcode`自带的`Apple clang`，能比较好的兼容`arm64e`架构

除了指定`CC`变量, 还可以创建`Config.xcconfig`并在其中创建`CC`变量, 本质是一样的. 本项目中的`xcode_cc.sh`为参考脚本

### Swift

对于Swift可以参考Clang, 只是不是创建`CC`而是`SWIFT_EXEC`, 本项目中的`xcode_swift.sh`为参考脚本, `Config.xcconfig`如下:
```
SWIFT_EXEC = /path/to/xcode_swift.sh
SWIFT_USE_INTEGRATED_DRIVER = NO
```
如果直接在`Xcode - Build Settings`分别创建这2个变量也是一样的

