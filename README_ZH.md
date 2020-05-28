# [iSH](https://ish.app)

[![Build Status](https://travis-ci.org/ish-app/ish.svg?branch=master)](https://travis-ci.org/tbodt/ish)
[![goto counter](https://img.shields.io/github/search/ish-app/ish/goto.svg)](https://github.com/tbodt/ish/search?q=goto)
[![fuck counter](https://img.shields.io/github/search/ish-app/ish/fuck.svg)](https://github.com/tbodt/ish/search?q=fuck)

<p align="center">
<a href="https://ish.app">
<img src="https://ish.app/assets/github-readme.png">
</a>
</p>

这项目是一个使用用户模式 x86 仿真和系统调用转换, 运行在 iOS 上的 Linux shell.

想要了解项目目前的状况, 详见 issue 和 提交记录.

- [Testflight beta](https://testflight.apple.com/join/97i7KM8O)
- [Discord server](https://discord.gg/HFAXj44)
- [Wiki with help and tutorials](https://github.com/ish-app/ish/wiki)

# 运行项目

本项目有 git 子模块, 请确保在clone项目时使用`--recurse-submodules` ，或者在clone好了之后执行 `git submodule update --init`.

为了构建此项目，你需要以下内容:

 - Python 3
 - Ninja
 - Meson (`pip install meson`)
 - Clang and LLD (在 mac 上`brew install llvm`, 在 linux 上`sudo apt install clang lld` 或者 `sudo pacman -S clang lld` 亦或 随你便)
 - sqlite3 (这个很基础，或许已经安装在 Linux 上了, 并且肯定已经安装在 Mac 上了. 如果没有你可以运行像这样的命令 `sudo apt install libsqlite3-dev`)

## 为 iOS 构建

打开 Xcode 项目, 选择 iSH.xcconfig, 并且修改 `ROOT_BUNDLE_IDENTIFIER` 为其他的并不一样的值. 然后点击 运行. 应该有脚本已经帮你自动执行了相关操作. 如果遇到了任何问题, 提交 issuse, 我们会帮助你解决.

## 为测试构建命令行工具

设置你的运行环境, cd 到项目目录并且运行命令 `meson build`，以在 `build` 中创建构建目录. 然后 cd 到构建目录并执行 `ninja`.

要建立一个自包含的 Alpine linux 文件系统, 请从 [Alpine 网站](https://alpinelinux.org/downloads/) 下载 Alpine minirotfs tarball for i386 并运行 `tools/fakefsify.py` 脚本. 将 minirotfs tarball 指定为第一个参数，将输出目录的名称指定为第二个参数. 然后可以使用 `/ish-f Alpine/bin/login-f root` 在 Alpine 文件系统中运行, 假设输出目录名为 `alpine`.

你可以使用 `tools/ptraceomatic` 替换 `ish`, 以便在实际进程和单个步骤运行程序, 并在每个步骤中比较寄存器. 我都用它来调试. 需要64位 Linux 4.11 或更高版本


## 日志

iSH 有几个日志类型可以在构建时期启用. 默认情况下它们都被禁用了. 想要启用它们需要:

- 在 Xcode 里: 将 iSH.xcconfig 中的 `ISH_LOG` 设置, 设置为以空格分隔的日志类型列表.
- 在 Meson(测试命令行工具) 中: 执行 `meson configure -Dlog="<space-separated list of log channels>` 命令.

可用的日子通道:

- `strace`: 最有用的类型, 记录几乎每个系统调用的参数和返回值.
- `instr`: 记录模拟器执行的每个指令, 会令让让所有执行变得很慢.
- `verbose`: 记录不属于其他类别的调试日志.
- `DEFAULT_CHANNEL` 查看更新此列表后是否添加了更多日志频道.

# 关于 JIT

可能我在写 iSH 中最有趣的部分就是 JIT 了. 它实际上不是真正的 JIT, 因为它不以机器代码为目标. 相反，它生成一个指向称为 gadgets 的函数的指针数组, 并且每个 gadget 都以对下一个函数的尾调用结束; 就像一些 Forth 解释器使用的线程化代码技术一样. 最终结果就是, 与纯仿真相比, 速度提高了大约3-5倍.

不幸的是, 我决定用汇编语言编写几乎所有的 gadgets。这可能从性能方面来说是一个很好的决定(虽然我永远也不确定), 但是从可读性、可维护性和我的理智来说, 这是一个可怕的决定. 我所不得不忍受的来自编译器/汇编程序/链接器的大量的乱七八糟的东西数量是疯狂的. 仿佛在那里面有一个魔鬼, 让我的代码足够畸形，就算没有急性, 也会编造愚蠢的理由告诉我为什么它不能够编译. 为了在编写代码时保持理智, 我不得不忽略代码结构和命名方面的最佳实践。您将发现宏和变量具有诸如 `ss`、`s`和`a`等描述性的名称. 汇编器的宏嵌套超出传统的写法. 最重要的是, 几乎没有注释.

所以一个警告: 长期接触此代码可能会导致你失去理智, 对 GAS 宏和链接器错误产生噩梦，或是任何其他使人虚弱的副作用. 在加利福尼亚州众所周知的是，这些代码会导致癌症、出生缺陷和生殖伤害.

