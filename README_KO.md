# [iSH](https://ish.app)

[![Build Status](https://travis-ci.org/ish-app/ish.svg?branch=master)](https://travis-ci.org/ish-app/ish)
[![goto counter](https://img.shields.io/github/search/ish-app/ish/goto.svg)](https://github.com/ish-app/ish/search?q=goto)
[![fuck counter](https://img.shields.io/github/search/ish-app/ish/fuck.svg)](https://github.com/ish-app/ish/search?q=fuck)

<p align="center">
<a href="https://ish.app">
<img src="https://ish.app/assets/github-readme.png">
</a>
</p>

사용자 모드 x86 에뮬레이션과 시스템 call 번역을 사용하여 iOS 에서 리눅스 쉘을 실행할 수 있게 해줍니다.

프로젝트의 현황을 알고 싶으시면 커밋 로그와 이슈 탭을 참고해주세요.

- [애플 앱스토어](https://apps.apple.com/us/app/ish-shell/id1436902243)
- [TestFlight beta](https://testflight.apple.com/join/97i7KM8O)
- [Discord server](https://discord.gg/HFAXj44)
- [도움 문서 Wiki](https://github.com/ish-app/ish/wiki)
- [README중문](https://github.com/ish-app/ish/blob/master/README_ZH.md)


# Hacking

해당 프로젝트는 깃의 서브 모듈이 있습니다. 해당 저장소를 받은 후 `--recurse-submodules` 또는 `git submodule update --init` 을 입력하여 깃 서브 모듈을 클론하세요.

아래 사항은 이 프로젝트를 빌드하기 위해 필요한 것들 입니다:

 - Python 3
   + Meson (`pip3 install meson`)
 - Ninja
 - Clang and LLD (맥에서는, `brew install llvm`, 리눅스에서는, `sudo apt install clang lld` 또는 `sudo pacman -S clang lld` 을 실행하세요)
 - sqlite3 (맥에서는 이미 제공 되어 있을 확률이 높습니다. 만약 그렇지 않다면 `sudo apt install libsqlite3-dev`)
 - libarchive (`brew install libarchive`, `sudo port install libarchive`, `sudo apt install libarchive-dev`) TODO: 앞에 dependency를 번들링 하기

## iOS 로 빌드하는 법 

Xcode로 프로젝트를 열고, iSH.xcconfig 연 후에 `ROOT_BUNDLE_IDENTIFIER`를 해당 프로젝트에 유일한 값으로 바꾸세요. 그후 실행을 누르면 자동으로 나머지를 세팅해줄 스크립트가 제공되어 있습니다. 만약 문제가 생긴다면, issue open을 해주시면 도와드리겠습니다.


## 테스트를 위한 cli 도구 빌드하는 법

환경을 세팅하기 위해서는 프로젝트 디렉토리로 이동하고 `meson build`를 커맨드 라인에 입력하세요. 그 후 빌드 된 디렉토리로 cd 후 `ninja` 커맨드를 입력해 실행하세요.

자체적으로 컨테이너 화 된 Alpine 리눅스 파일 시스템으로 실행하고 싶다면, [Alpine 웹사이트](https://alpinelinux.org/downloads/) 에서 i386을 위한 Alpine minirootfs(Mini Root Filesystem) tarball 을 다운로드 받고 `./tools/fakefsify`으로 실행하세요. 매개인자로 다운로드 받은 minirootfs tarball 파일을 입력하고 출력 받을 디렉토리의 이름을 두번째 인자로 입력하면 됩니다. 그 후에는 `./ish -f {출력받을 디렉토리 이름} /bin/login -f root` 명령어를 사용하여 Alpine 시스템 내에서 원하는 것을 실행할 수 있습니다. 만약 `tools/fakefsify` 가 빌드 디렉토리에 존재하지 않는다면, libarchive를 찾을 수 없어서 그런 것일 수 있습니다. 위를 참고하여 시스템에 설치하는 방법을 참고해주세요.

실제 프로세스로 프로그램을 실행하고 각 단계의 레지스터를 비교하기 위해서 `ish`를 `tools/ptraceomatic`로 바꿔 실행할 수 있습니다. 디버깅을 위해 저는 사용합니다. 64-bit Linux 4.11 이후 버전이 필요합니다.

## 로깅

iSH 는 빌드 시간에 허용될 수 있는 다수의 로깅 채널을 갖고 있습니다. 기본 값으로는 모두 꺼놨는데, 사용을 위해서는:

- Xcode에서: iSH.xcconfig에 있는 `ISH_LOG` 값을 스페이스로 나뉜 로그 채널 리스트로 설정해주세요.
- Meson에서 (테스트를 위한 커맨드 라인 도구): `meson configure -Dlog="<스페이스로 나뉜 로그 채널 리스트>"`을 실행하세요.

제공되는 로그 채널:

- `strace`: 가장 쓸모있는 채널입니다. 매개변수와 거의 모든 시스템 호출의 반환 값을 로깅합니다.
- `instr`: 에뮬레이터에서 실행된 모든 명령어를 로깅합니다. 이로인해 성능저하가 일어날 수 있습니다.
- `verbose`: 다른 카타고리에 들지 않는 로그를 디버깅합니다.
- `DEFAULT_CHANNEL`을 찾아보면 리스트가 업데이트 이후 새로 추가된 로그 채널을 볼수 있습니다.

# JIT(Just In Time 컴파일러)에 대한 추가사항

iSH에서 추가한 것 중 가장 흥미로운 것은 JIT 컴파일러 일 것입니다. 기계 코드를 목적으로 하지 않기 때문에 JIT 실질적으로는 아니긴 합니다. Gadget 이라고 불리는 포인터 배열을 생성하는데, 각각의 이것은 다음 함수를 호출하는 꼬리물기를 합니다. 몇몇 Forth 언어 인터프리터에서 사용된 스레드 코드처럼 말이죠. 결과적으로 순수 에뮬보다 3-5배 더 빨라졌습니다.

불행하게도 저는 어셈블리어로 대부분의 이러한 gadget을 작성했습니다. 이것은 성능적으로는 좋은 선택이었을 지 몰라도(실제로는 알 도리가 없지만), 가독성, 유지보수, 그리고 제 정신상태에 대해서는 좋지 않은 선택이 되었습니다. 컴파일러/어셈블러/링커로 인한 여러 고충은 말도 할 수 없을 정도입니다. 거의 무슨 제 코드의 가독성을 해치지 않으면 컴파일을 막는 그러한 악마가 있는 것 같았습니다. 이 코드를 작성하는 도중 제정신을 유지하기 위해서 저는 네이밍과 코드 구조론을 따른 최적의 선택을 하지 못하였습니다. `ss`, `s` 그리고 `a`와 같은 매크로 그리고 변수 명을 찾을 수 있을 것입니다. 주석 또한 찾기 힘들 것입니다.

그렇기에 주의 하세요: 해당 코드를 장기간 접할 경우 정신질환을 앓게되거나 GAS 매크로와 링커오류에 대한 악몽에 시달리고 또다른 부작용이 있을 수 있습니다. 암, 선천적 결함, 또는 생식기 질환을 야기한다고 질병관리청에서 인정했습니다. 암튼 그랬습니다.
