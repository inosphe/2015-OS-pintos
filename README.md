# 2015-OS-pintos

# getting started

## Setup Boshs
* bochs 폴더로 이동
* $ ./configure --enable-gdb-stub --with-nogui
* $ make
* $ sudo make install

### Trouble Shootings

#### 오류 1: C compiler cannot create executables
* gcc, g++ 및 라이브러리 패키지 설치
* sudo apt-get install libc6-dev g++ gcc

#### 오류 2: X windows libraries were not found 
* X windows 라이브러리 설치
* $ sudo apt-get install xorg-dev

## How to build
1. pintos/src/threads/ 디렉토리로 이동
2. $ make
3. $ cd build
4. $ pintos -- run alarm-multiple 
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


#references
* http://nzt.co-story.net/wordpress/?p=102
* http://lacti.me/2008/04/01/make-pintos-environment/
* https://www.google.co.kr/search?client=safari&rls=en&q=strtok_r+return+null&ie=UTF-8&oe=UTF-8&gfe_rd=cr&ei=954JVc2jM4vN8geXpYCoAg#newwindow=1&rls=en&q=pintos+strtok_r+start_process	- pintos search
* http://hypermin.tistory.com/16	- word-align
* http://www.nongnu.org/avr-libc/user-manual/group__avr__stdint.html - types
* http://imarch.pe.kr/?cat=23&wpmp_switcher=mobile
* http://courses.cs.vt.edu/cs3204/spring2007/gback/lectures/Lecture19.pdf
* http://web.stanford.edu/class/cs140/projects/pintos/pintos.html#SEC_Contents - official
* http://web.stanford.edu/class/cs140/projects/pintos/pintos_3.html#SEC37 - Virtual Memory Layout
* http://web.stanford.edu/class/cs140/projects/pintos/pintos_3.html#SEC44 - Argument Parsing
* http://csl.skku.edu/SSE3044F12/Resources - 성균관대
* http://egloos.zum.com/YSocks/v/504475

# etc.

## shortcut
* inosphe@ubuntu:/mnt/hgfs/inosphe/documents/github/2015-OS-pintos/pintos/src/userprog$ make
* inosphe@ubuntu:/mnt/hgfs/inosphe/documents/github/2015-OS-pintos/pintos/src/userprog$ pintos -p ../examples/echo -a echo -- -f run 'echo x'