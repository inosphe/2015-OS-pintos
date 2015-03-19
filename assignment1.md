### 1. 과제 개요

Pintos 실행 명령 `$pintos –v -- run ‘echo x’`에서 echo x 에 해당하는 문자열을 parsing하여 실행할 프로그램 이름과 각각의 arguments로 분리하여 실행할 수 있도록 한다.

### 2. 과제 목표

* 문자열을 분리하여 user program name과 실행시 넘길 program arguments(argv)로 분리한다.
* 분리한 file name을 이용하여 user program을 실행하고 분리한 program arguments를 스택에 삽입하여 실행한 프로그램이 argv를 통하여 받을 수 있도록 한다.
* 누수되는 메모리가 없도록 하며, 각각의 상황에 발생할 수 있는 error 상황에 최대한 대응한다.
* Pintos의 process, thread architecture를 이해한다.
* stack의 구조와 byte alignment에 대해 이해한다.

### 3. 과제 해결을 위한 Pintos 분석 내용

* 여타 운영체제들(Linux, Windows 등)과 마찬가지고 Pintos역시 프로세스 별로 가상메모리 주소를 할당하고 이에 매핑되는 물리 메모리영역(가상환경 상에서 작동하므로, Pintos의 가상메모리 영역)을 page table로 관리한다. 이 page table을 bitmap이라 하며 각각의 page는 4KB의 크기를 갖는다.
* 일반적으로 C(C++) 프로그래밍 시에는 malloc(new)함수를 사용하였는데 이는 하나 이상의 연속된 page의 memory block을 관리하여 사용한다. 그러나 Pintos의 기본 코드에서는 전반적으로 `palloc_get_page` 함수를 많이 사용하고 있는 모습을 볼 수 있다. 할당한 후 오래동안 사용할 메모리가 아닌, 잠시 동안 인자를 처리하기 위해 할당하는 메모리이므로 일시적으로 메모리사용량은 늘겠지만 page단위로 할당 한 후 page단위로 반환하는 것이 메모리 파편화 이슈 및 성능에서 더 유리함을 예상할 수 있다.
* esp는 c0000000에서 시작함을 볼 수 있다.
* 다른학생의 질문이 있어 답변해주면서 유심히 봤던 부분인데, `process_execute` 함수에서 인자로 들어온 `file_name` 변수에 대해서 바로 strtok_r 함수를 호출하면 결과 출력부분에서 약간의 오작동을 확인 할 수 있다. `Execution of 'echo x' complete.` 이 아닌 `Execution of 'echo' complete.` 으로 출력되는 문제인데 이는 `strtok_r`의 명세를 보면 이유를 확인할 수 있다. 
```
The strtok() and strtok_r() functions return a pointer to the beginning of each subsequent token in the string, after replacing the token itself with a NUL character.  When no more tokens remain, a null pointer is returned.
```
strtok_r은 인자로 들어온 문자열을 변형시키며, 이를 예방하기 위해서는 별도의 메모리를 할당하여 문자열을 복사한 후 사용해야 한다.
* 스택은 높은 번지에서 낮은 번지로 거꾸로 자라므로, esp를 이용하여 적절하게 값을 push하기 위해서는 esp값을 일정 바이트만큼 감소시킨 후 삽입해야 한다.

### 4. 해결과정

#### 수정한 파일
* process.h
* process.c

#### 1) git 사용

* private git repository를 생성 한 후 팀원과 공유
* 과제별로 branch를 사용하여 협업 및 제출 할 계획 수립 및 활용

#### 2) error handling

* 기능 추가를 위한 코드를 작성하면서 메모리 할당에 실패, user program 적재 실패 등 error를 handling 할 필요가 생겼는데 할당한 메모리의 반환과 같은 코드가 누적되며 불필요한 코드 블럭이 계속 추가 되었다. goto문을 사용하여 일괄적으로 error handling을 수행하므로 작성자가 실수하여 메모리 누수를 만드는 것을 예방하고 가독성을 높혔다.

#### 3) argument parsing

* strtok_r 함수를 이용하여 입력받은 문자열을 분리하고, 이 때 파싱된 인자 목록을 `argument_stack` 함수를 호출하기 전에 임시로 보관하기 위해서 palloc_get_page 함수를 이용하여 메모리를 할당하였다. 각각의 인자는 4K-1 이하의 크기를 갖도록 강제하였다. 이는 4KB가 page 크기이기 떄문이고 -1은 할당된 4KB 중 마지막 NULL 문자를 위한 공간을 남기기 위함이다. 
* 좀 더 엄밀하게 하자면 `argument_stack` 함수 내에 있는 지역변수 `addr1`을 계산하는 과정을 보면 arguments의 길이의 합을 구하는 연산이 중복되어있는데 이를 좀 더 유연하게 변경하면 `parse` 변수를 위한 heap 공간을 할당하는 과정이 불필요해질 수 있다. 그러나 본 과제에서는 좀 더 과제 수행 목적에 부합하기 위해 이러한 선택을 하지 않고 메모리를 할당받아 사용하는 방향으로 기능하도록 작성하였다.

#### 4) stack

* `argument_stack` 함수 중 argv[n](0<= n <= argc) 를 삽입하는 과정에서 argv의 데이터 영역의 주소를 기억하고 있을 필요가 생기는데 이 때 불필요하게 heap을 할당하는 것 보다 데이터의 길이를 구하는 연산을 중복하는 것이 낫다는 판단으로 `addr0`, `addr1` 변수를 분리하여 동시에 삽입할 수 있도록 하였다.
* stack 삽입 시 memory의 4byte alignment를 위하여 `*esp & 0xfffffffc` 와 같은 형태의 연산을 통해 aligned memory address를 구하였다. 이로써 생긴 빈 영역에는 불필요하지만, 0으로 초기화 하므로써 빈 영역임을 명시적으로 표현하였다.
* 실제 stack에 값을 push할 때에는 `push_stack_int32`, `push_stack_int8`, `push_stack_string` 의 이름을 갖는 macro를 정의하여 사용하였다. 간단한 동작이지만 반복하기에는 부담스럽고, 함수로 표현하자면 불필요한 jump과정이 생기는 것을 우려하여 함수가 아닌 macro를 이용하였다. 이 때 작업자의 실수를 막기 위해 macro는 {} code block으로 감쌌다. 매크로에는 char, int 형이 아닌 int8_t, int32_t형을 사용하므로써 자료형의 크기를 명시적으로 표현하였다. 함수가 아닌 매크로로 작성하므로써 두가지의 tradeoff가 있었는데 하나는 임시로 사용할 t 변수를 필요로 한다는 것이고, argv값을 push할 때, 매크로로 적절히 표현할 수 없어 `argv = addr1-offset1;` 으로 argv값을 별도의 변수에 저장한 후 사용하였다.
* 마지막으로 매크로는 값 삽입에 esp값이 아닌 base address + offset을 이용하므로 `set_esp(addr1, offset1);` expression을 사용하여 stack의 esp값을 세팅하였다.
* 

#### 5. 프로젝트를 진행하며 작업해야 할 것들

* 좀 더 깔끔하게 에러를 처리하기 위한 에러 처리 매크로 및 함수
* local 및 server 환경에서의 test 및 deploy를 위한 shell 스크립트
