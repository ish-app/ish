# [iSH](https://ish.app)

[![Build Status](https://travis-ci.org/ish-app/ish.svg?branch=master)](https://travis-ci.org/tbodt/ish)
[![goto counter](https://img.shields.io/github/search/ish-app/ish/goto.svg)](https://github.com/tbodt/ish/search?q=goto)
[![fuck counter](https://img.shields.io/github/search/ish-app/ish/fuck.svg)](https://github.com/tbodt/ish/search?q=fuck)

<p align="center">
<a href="https://ish.app">
<img src="https://ish.app/assets/github-readme.png">
</a>
</p>

iSH 是一个运行在 iOS 上的 Linux shell。本项目使用了 x86 用户模式仿真和系统调用翻译转换。

请查看 issue 和提交记录以了解本项目当前的状态。

- [App Store 页面](https://apps.apple.com/us/app/ish-shell/id1436902243)
- [Testflight 测试](https://testflight.apple.com/join/97i7KM8O)
- [Discord 服务器](https://discord.gg/HFAXj44)
- [维基帮助与教程](https://github.com/ish-app/ish/wiki)

# 上手

本项目下包含了其他 git 项目作为子模块，请确保在克隆时使用参数`--recurse-submodules`，即 `git clone --recurse-submodules https://github.com/ish-app/ish.git`。或是在克隆好了之后执行 `git submodule update --init`。

编译此项目需要以下依赖:

 - Python 3
    + Meson (`pip3 install meson`)
 - Ninja 请查看[此处](https://ninja-build.org/)
 - Clang and LLD (在安装了 `brew` 的 macOS 系统上运行 `brew install llvm`。在 Linux 系统上请根据你的包管理器，选择运行相应的安装命令 `sudo apt install clang lld` 或者 `sudo pacman -S clang lld`)
 - sqlite3 (通常 sqlite3 在 macOS 上是预安装的，但它或许没有安装在你的 Linux 上，运行 `which sqlite3` 以查看它是否存在。如果没有，你可以根据你的包管理器运行 `sudo apt install libsqlite3-dev` 之类的安装命令)
 - libarchive (在 macOS 系统上使用 `brew install libarchive` 或 `sudo port install libarchive` 来安装。在 Linux 系统上请根据你的包管理器，选择运行相应的安装命令如 `sudo apt install libarchive-dev` 来安装)

## 创建iOS应用

使用 Xcode 打开项目，选择 iSH.xcconfig，并且修改 `ROOT_BUNDLE_IDENTIFIER` 为你的[唯一值](https://help.apple.com/xcode/mac/current/#/dev91fe7130a)。此外，还需要在项目（project）的构建设置（build settings）中更新开发团队 ID，注意这里指的不是目标（target）的构建设置（build settings）。然后点击 `运行`，之后应该有脚本帮你自动执行相关操作。如果遇到了任何问题，请提交 issue，我们会帮你解决。

## 为测试构建命令行工具

在项目目录中运行命令 `meson build`，之后 `build` 目录会被创建。进入到 `build` 目录并运行命令 `ninja`。

为了建立一个自有的 Alpine linux 文件系统，请从 [Alpine 网站](https://alpinelinux.org/downloads/) 下载 `Alpine minirotfs tarball for i386` 并运行 `tools/fakefsify` 。将 minirotfs tarball 指定为第一个参数，将输出目录的名称（如`alpine`）指定为第二个参数，即 `tools/fakefsify $MinirotfsTarballFilename alpine` 然后在 Alpine 文件系统中运行 `/ish -f alpine/bin/login -f root`。如果 `build` 目录下找不到 `tools/fakefsify`，可能是系统上找不到 `libarchive` 的依赖（请参照前面的章节进行安装）。

除了可以使用 `ish`，你也可以使用 `tools/ptraceomatic` 替代它，以便在某个真实进程中单步比较寄存器。我通常使用它来进行调试（需要 64 位 Linux 4.11 或更高版本）。

## 日志

在编译过程中，iSH 提供数种日志类型，默认情况下它们都被禁用，想要启用它们需要:

- 在 Xcode 中将 iSH.xcconfig 中 `ISH_LOG` 设置为以空格分隔的日志类型列表。
- 在 Meson (测试使用的命令行工具) 中执行命令 `meson configure -Dlog="<space-separated list of log channels>"`。

可用的日志类型:

- `strace`: 最有用的类型，记录几乎每个系统调用的参数和返回值。
- `instr`: 记录模拟器执行的每个指令，这会让所有执行变得很慢。
- `verbose`: 记录不属于其他类别的调试日志。
- 使用 `grep` 命令查看 `DEFAULT_CHANNEL` 变量，以确认在更新此列表后是否添加了更多日志频道。

# 关于 JIT

可能我在写 iSH 中最有趣的部分就是 JIT 了。实际上它不是真正的 JIT，因为它不并以机器代码为目标，而是生成一个称为 gadgets 的函数指针数组，并且每个 gadget 都以对下一个函数的尾调用结束，类似于一些 Forth 解释器使用的线程化代码技术。好处就是，与纯仿真相比，它的速度提高了 3-5 倍。

但不幸的是，我最开始决定用汇编语言编写几乎所有的 gadgets。这可能从性能方面来说是一个好的决定（虽然我永远也无法确定），但是对可读性、可维护性和我的理智来说，这是一个可怕的决定。我承受了大量来自编译器、汇编程序以及链接器的乱七八糟的东西。那里面就像有一个魔鬼，把我的代码搞得畸形，就算没有畸形，也会编造一些愚蠢的理由说它不能够编译。为了在编写代码时保持理智，我不得不忽略代码结构和命名方面的最佳实践。你会发现宏和变量具有诸如 `ss`、`s` 和 `a` 等描述性的名称，并且汇编器的宏嵌套层数超乎你的想象。最重要的是，代码中几乎没有任何注释。

所以这是一个警告: 长期接触此代码可能会使你失去理智，对 GAS 宏和链接器错误产生噩梦，或是任何其他使人虚弱的副作用。在加利福尼亚，众所周知这样的代码会导致癌症、生产缺陷和重复伤害。
