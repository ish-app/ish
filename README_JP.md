# [iSH](https://ish.app)

[![Build Status](https://github.com/ish-app/ish/actions/workflows/ci.yml/badge.svg)](https://github.com/ish-app/ish/actions)
[![goto counter](https://img.shields.io/github/search/ish-app/ish/goto.svg)](https://github.com/ish-app/ish/search?q=goto)
[![fuck counter](https://img.shields.io/github/search/ish-app/ish/fuck.svg)](https://github.com/ish-app/ish/search?q=fuck)
[![shit counter](https://img.shields.io/github/search/ish-app/ish/shit.svg)](https://github.com/ish-app/ish/search?q=shit)

<p align="center">
<a href="https://ish.app">
<img src="https://ish.app/assets/github-readme.png">
</a>
</p>

iSHは、ユーザーモードのx86エミュレーションとシステムコールの翻訳を使用して、iOS上でLinuxシェルを実行するプロジェクトです。

プロジェクトの現状については、issueタブとコミットログを確認してください。

- [App Storeページ](https://apps.apple.com/us/app/ish-shell/id1436902243)
- [TestFlightベータ](https://testflight.apple.com/join/97i7KM8O)
- [Discordサーバー](https://discord.gg/HFAXj44)
- [ヘルプとチュートリアルのWiki](https://github.com/ish-app/ish/wiki)

# ハッキング

このプロジェクトにはgitサブモジュールがあります。`--recurse-submodules`を使用してクローンするか、クローン後に`git submodule update --init`を実行してください。

プロジェクトをビルドするには、以下のものが必要です：

 - Python 3
   + Meson (`pip3 install meson`)
 - Ninja
 - ClangとLLD（macでは`brew install llvm`、linuxでは`sudo apt install clang lld`または`sudo pacman -S clang lld`など）
 - sqlite3（これは非常に一般的で、linuxではすでにインストールされているかもしれませんし、macでは確実にインストールされています。もしインストールされていない場合は、`sudo apt install libsqlite3-dev`などを実行してください）
 - libarchive（`brew install libarchive`、`sudo port install libarchive`、`sudo apt install libarchive-dev`など）

## iOS用にビルドする

プロジェクトをXcodeで開き、iSH.xcconfigを開いて、`ROOT_BUNDLE_IDENTIFIER`を一意の値に変更します。また、プロジェクト（ターゲットではなく！）のビルド設定で開発チームIDを更新する必要があります。その後、実行をクリックします。他のすべてを自動的に行うスクリプトがあります。問題が発生した場合は、issueを開いてください。お手伝いします。

## テスト用のコマンドラインツールをビルドする

環境を設定するには、プロジェクトディレクトリに移動し、`meson build`を実行して`build`ディレクトリを作成します。その後、buildディレクトリに移動し、`ninja`を実行します。

自己完結型のAlpine linuxファイルシステムを設定するには、[Alpineウェブサイト](https://alpinelinux.org/downloads/)からi386用のAlpine minirootfs tarballをダウンロードし、`./tools/fakefsify`を実行します。minirootfs tarballを最初の引数として、出力ディレクトリの名前を2番目の引数として指定します。その後、`./ish -f alpine /bin/sh`を使用して、Alpineファイルシステム内でコマンドを実行できます。出力ディレクトリの名前が`alpine`であると仮定します。`tools/fakefsify`がbuildディレクトリに存在しない場合、それはシステム上でlibarchiveを見つけられなかったためかもしれません（インストール方法については上記を参照してください）。

`ish`を`tools/ptraceomatic`に置き換えることで、実際のプロセスでプログラムを実行し、各ステップでレジスタを比較しながらシングルステップ実行できます。デバッグに使用します。64ビットLinux 4.11以降が必要です。

## ロギング

iSHには、ビルド時に有効にできるいくつかのロギングチャネルがあります。デフォルトでは、すべて無効になっています。有効にするには：

- Xcodeで：iSH.xcconfigの`ISH_LOG`設定をスペースで区切られたログチャネルのリストに設定します。
- Meson（テスト用のコマンドラインツール）で：`meson configure -Dlog="<ログチャネルのスペース区切りリスト>"`を実行します。

利用可能なチャネル：

- `strace`：最も有用なチャネルで、ほぼすべてのシステムコールのパラメータと戻り値をログに記録します。
- `instr`：エミュレータが実行するすべての命令をログに記録します。これにより、実行速度が大幅に低下します。
- `verbose`：他のカテゴリに該当しないデバッグログを記録します。
- `DEFAULT_CHANNEL`をgrepして、このリストが更新された後に追加されたログチャネルがあるかどうかを確認します。

# JITに関する注意事項

iSHの一部として書いた中で最も興味深いものの1つはJITです。実際には、マシンコードをターゲットにしていないため、実際のJITではありません。代わりに、ガジェットと呼ばれる関数へのポインタの配列を生成し、各ガジェットは次の関数へのテールコールで終了します。これは、一部のForthインタープリタが使用するスレッド化コード技術に似ています。その結果、純粋なエミュレーションと比較して、速度が約3〜5倍向上します。

残念ながら、ほぼすべてのガジェットをアセンブリ言語で書くという決定を下しました。これは、パフォーマンスに関してはおそらく良い決定でしたが（確かではありませんが）、可読性、保守性、および私の正気に関してはひどい決定でした。コンパイラ、アセンブラ、リンカからのたくさんの問題に対処しなければなりませんでした。コードが十分に変形していることを確認し、そうでない場合は、コンパイルできない理由をでっち上げる悪魔がいるようなものです。このコードを書いている間に正気を保つために、コード構造と命名のベストプラクティスを無視しなければなりませんでした。`ss`、`s`、`a`などの説明的な名前を持つマクロや変数が見つかるでしょう。信じられないほどネストされたアセンブラマクロ。そして、ほとんどコメントがありません。

したがって、警告です：このコードに長期間さらされると、正気を失い、GASマクロやリンカエラーについての悪夢に悩まされる可能性があります。カリフォルニア州では、このコードが癌、先天性欠損症、および生殖障害を引き起こすことが知られています。
