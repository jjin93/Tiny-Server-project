/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"


// doit 은 무엇을 하는 함수인가? 트랜잭션 처리함수. 들어온 요청을 읽고 분석.
// GETrequest 가 들어오면 정적인지 동적인지 파악해서 각각에 맞는 함수를 실행시킨다. 오류 발생 시 에러표시도 포함한다.
void doit(int fd);
// rio -> ROBUST I/O
void read_requesthdrs(rio_t *rp); //csapp.h 40줄 쯤 정의되어 있다.
// parse_uri 함수는 폴더 안에서 특정 이름을 찾아서 파일이 동적인건지, 정적인건지 알려준다.
int parse_uri(char *uri, char *filename, char *cgiargs);

//serve_static 함수는 정적인 파일일때 파일을 클라이언트로 응답한다.
void serve_static(int fd, char *filename, int filesize, char *method);

//get_filetype 함수는 http,text,jpg,png,gif,mp4 파일을 찾아서 serve_static에서 사용
void get_filetype(char *filename, char *filetype);

//serve_dynamic 함수는 동적인 파일을 받았을 때 fork 함수로 자식프로세스를 만든 후 거기서 CGI프로그램을 실행한다.
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void sigchild_handler(int sig);
// void echo(int connfd);  //echo 함수 쓰겠다.

//main의 매개변수 : argc = 배열 길이 argv = filename, port
int main(int argc, char **argv) {
  int listenfd, connfd; // fd는 파일식별자의 약자 file description
  char hostname[MAXLINE], port[MAXLINE];  // hostname:port -> localhost:8000
  // socklen_t 는 소켓 관련 매개 변수에 사용되는 것으로 길이 및 크기 값에 대한 정의를 내려준다..
  socklen_t clientlen;
  struct sockaddr_storage clientaddr; 

  /* Check command line args */
  if (argc != 2) {  //프로그램 실행 시 port를 안썼다면 error 처리
    fprintf(stderr, "usage: %s <port>\n", argv[0]); //argv[0]의 사용법은 파일명 <port>이다라고 사용자에게 알려준다.
    exit(1);
  }
  if (Signal(SIGCHLD, sigchild_handler) == SIG_ERR){
    unix_error("signal child handler error");
  }

  listenfd = Open_listenfd(argv[1]);  //argv[1] 이 포트에 대한 듣기 소켓 오픈
  // 무한 서버 루프 실행
  while (1) {

    clientlen = sizeof(clientaddr);
    //연결 요청 접수
    //연결 요청 큐에 아무것도 없을 경우 기본적으로 연결이 생길때까지 호출자를 막아둔다.
    //소켓이 non_blocking 모드일 경우엔 에러를 띄운다.
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    //getnameinfo:converts socket address structure to host and service name string
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    // 트랜 잭션 수행
    doit(connfd);   // line:netp:tiny:doit
    // echo(connfd);
    Close(connfd);  // line:netp:tiny:close
  }
}

// void echo(int connfd){ //텍스트 줄을 읽고 echo 해주는 함수, rio_readlineb함수가 EOF를 만날 때까지 텍스트 줄을 반복해서 읽고 써준다.
//   size_t n;
//   char buf[MAXLINE];
//   rio_t rio;

//   Rio_readinitb(&rio, connfd);
//   while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0){
//     if (strcmp(buf, "\r\n") == 0)
//       break;
//     Rio_writen(connfd, buf, n);  
//   }
// }

void doit(int fd){
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  // 요청 라인 읽고 분석하기
  /* Read request line and headers*/
  Rio_readinitb(&rio, fd); // system book 10.5 , rio 구조체 초기화..
  Rio_readlineb(&rio, buf, MAXLINE); // buf에 읽은 것 담겨 있음.
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  // if(strcasecmp(method, "GET")){ strcasecmp는 대소문자 구분없이 비교
  if(strcasecmp(method, "GET") && strcasecmp(method, "HEAD")){
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);
  /* Parse URI from GET request  */
  
  is_static = parse_uri(uri, filename, cgiargs);
  if(stat(filename, &sbuf) < 0){
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static){ /*Serve static content*/
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
      //S_ISREG: 정규 파일인지 판별
      //S_IRURT: 사용자 파일을 읽을 수 있음
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    // serve_static(fd, filename, sbuf.st_size);

    serve_static(fd, filename, sbuf.st_size, method);
  }
  else{/*Serve dynamic content*/
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
      //S_ISREG: 정규 파일인지 판별
      //S_IXURT: 사용자가 파일을 실행할 수 있음
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    // serve_dynamic(fd, filename, cgiargs);
    serve_dynamic(fd, filename, cgiargs, method);
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
  char buf[MAXLINE], body[MAXBUF];
  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

// 요청 헤더 읽기 함수
void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE); //MAXLINE 까지 읽기
  while(strcmp(buf, "\r\n")){ //끝나는 줄 나올때까지 계속 읽기
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}
int parse_uri(char *uri, char *filename, char *cgiargs){
  char *ptr;
  //cgi-bin 없다면
  if(!strstr(uri, "cgi-bin")){/*static content*/
    //strstr(str1, str2): str1에서 str2가 시작하는 시점을 찾는다, 이때 string2가 없는 경우 NULL return
    strcpy(cgiargs, "");
    //strcpy(arr, str): arr에 str을 복사
    strcpy(filename, "."); // ./uri/home.html가 된다.
    strcat(filename, uri);
    //strcat(arr, str): arr뒤에 str을 붙인다.
    if (uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");
    return 1;
  }else{//dynamic content , /cgi-bin/adder?a=1&b=1
    ptr = index(uri, '?');
    // CGI 인자 추출
    if (ptr){
      // 물음표 뒤에 인자 다 갖다 붙인다.
      strcpy(cgiargs, ptr+1);
      //포인터는 문자열 마지막으로 바꾼다.
      *ptr = '\0'; // uri 물음표 뒤 다 없애기
    }
    else{
      strcpy(cgiargs, ""); // 물음표 뒤 녀석들 전부 넣어주기
    }
    // 나머지 부분 상대 URI로 바꿈
    strcpy(filename, "."); // ./cgi-bin/adder
    strcat(filename, uri);//  ./uri 가 된다.
    return 0;
  }
}

// fd 응답받는 소켓(연결식별자), 파일 이름, 파일 사이즈, 메소드
void serve_static(int fd, char *filename, int filesize, char *method){
  // rio_t file_rio; // readinitb 를 쓰기위해 선언
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];
  /* Send response headers to client */
  get_filetype(filename, filetype); // 파일 접미어 검사해서 파일 이름에서 타입 가지고 오기.
  // 클라이언트에게 응답 보내기
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  // 서버에 출력
  printf("%s", buf);
  if(!strcasecmp(method, "HEAD")) // HEAD가 들어오면 응답 바디부분 보내지 않는다.
    return;
  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  // /*파일이나 디바이스를 응용 프로그램의 주소 공간 메모리에 대응시킨다.
  //   1인자 => 시작 포인터 주소 (아래의 예제 참조)
  //   2인자 => 파일이나 주소공간의 메모리 크기
  //   3인자 => PROT 설정 (읽기, 쓰기, 접근권한, 실행)
  //   4인자 => flags는 다른 프로세스와 공유할지 안할지를 결정한다.
  //   5인자 => fd는 쓰거나 읽기용으로 열린 fd값을 넣어준다.
  //   6인자 => offset은 0으로 하던지 알아서 조절한다.*/

  /*11.9
    Modify Tiny so that when it serves static content, 
    it copies the requested file to the connected descriptor
    using malloc, rio_readn, and rio_writen, instead of mmap and rio_writen.
  */
  srcp = (char*)Malloc(filesize);
  // Rio_readinitb(&file_rio, srcfd); // 버퍼 써서 rio_read
  // Rio_readnb(&file_rio, srcp, filesize); // Rio_readn 과 Rio_readnb의 차이점 알기
  Rio_readn(srcfd, srcp, filesize); // 버퍼 안쓰고 read
  Close(srcfd); // Close 의 위치가 여기가 맞는지?
  Rio_writen(fd, srcp, filesize);
  // Munmap(srcp, filesize);
  free(srcp);
}

/*
 * get_filetype - Derive file type from filename
 */
void get_filetype(char *filename, char *filetype){
  if(strstr(filename, ".html")){
    strcpy(filetype, "text/html");
  }else if(strstr(filename, ".gif")){
    strcpy(filetype, "image/gif");
  }else if(strstr(filename, ".png")){
    strcpy(filetype, "image/png");
  }else if(strstr(filename, ".jpg")){
    strcpy(filetype, "image/jpg");
  /* 11.7 mpg video files */
  }else if(strstr(filename, ".mp4")){
    strcpy(filetype, "video/mp4");
  }else{
    strcpy(filetype, "image/plain");
  }
}


void serve_dynamic(int fd, char *filename, char *cgiargs, char *method){
  char buf[MAXLINE], *emptylist[] = { NULL };
  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));
  if (!strcasecmp(method, "HEAD"))
    return;

  if (Fork() == 0) { /* Child */
          /* Real server would set all CGI vars here */
    printf("7777777777777777\n");
    setenv("QUERY_STRING", cgiargs, 1);
    printf("666666666666666666666\n");
    Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */
    printf("5555555555555555555555\n");
    Execve(filename, emptylist, environ); /* Run CGI program */
  }
  Wait(NULL);
}

void sigchild_handler(int sig){
  int old_errno = errno;
  int status;
  pid_t pid;
  while((pid = waitpid(-1, &status, WNOHANG)) > 0){}
  errno = old_errno;

}