# 2015-OS-pintos

# getting started

## Setup Boshs
* bochs 폴더로 이동
* $ ./configure --enable-gdb-stub --with-nogui $ make
* $ sudo make install

### Trouble Shootings

#### 오류 1: C compiler cannot create executables
* gcc, g++ 및 라이브러리 패키지 설치
* sudo apt-get install libc6-dev g++ gcc

#### 오류 2: X windows libraries were not found 
* X windows 라이브러리 설치
* $ sudo apt-get install xorg-dev

## How to build
1. pintos/src/threads/ 디렉토리로 이동 $ make
2. $ cd build
3. $ pintos -- run alarm-multiple 
	* 프로그램 동작 확인

## Config Environment

### Environment Variables

#### ~/.bashrc 파일에 환경변수 설정
* pintos를 설치한 디렉토리 경로를 추가
* export PATH="$PATH:/home/user/pintos/src/utils“ 추가
* $source ~/.bashrc 수정사항 적용

### (Optional) gcc버전을 pintos 권장인 4.5 버전으로 다운그레이드
* $ sudo apt-get install gcc-4.5
* $ sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.5 50