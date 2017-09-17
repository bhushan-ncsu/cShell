/******************************************************************************
 *
 *  File Name........: main.c
 *
 *  Description......: Simple driver program for ush's parser
 *
 *  Author...........: Vincent W. Freeh
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "parse.h"
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/user.h> /*ptrace*/
#include <sys/reg.h> /*ORIG_EAX*/
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include<errno.h>
#include<signal.h>
#include<sys/resource.h>

char ushrc_path[256]; //path of ushrc_file
int fd[2];
int fd1, fd2, f_in; //for file i/p, o/p redirection
extern char **environ;
extern int errno;

void myPipe(Pipe p);
int ushrc_location();
int is_builtin(Cmd c);
void builtin_cd(int builtin_flag, Cmd c);
void builtin_echo(int builtin_flag, Cmd c);
void builtin_logout(int builtin_flag, Cmd c);
void builtin_nice(int builtin_flag, Cmd c);
void builtin_pwd(int builtin_flag, Cmd c);
void builtin_setenv(int builtin_flag, Cmd c);
void builtin_unsetenv(int builtin_flag, Cmd c);
void builtin_where(int builtin_flag, Cmd c);
void quitproc();
void sigterm();
//char *strerror(int errnum);

static void prCmd(Cmd c)
{
  int i;

  if ( c ) {
    printf("%s%s ", c->exec == Tamp ? "BG " : "", c->args[0]);
    if ( c->in == Tin )
      printf("<(%s) ", c->infile);
    if ( c->out != Tnil )
      switch ( c->out ) {


      case Tout:
	printf(">(%s) ", c->outfile);
	break;
      case Tapp:
	printf(">>(%s) ", c->outfile);
	break;
      case ToutErr:
	printf(">&(%s) ", c->outfile);
	break;
      case TappErr:
	printf(">>&(%s) ", c->outfile);
	break;
      case Tpipe:
	printf("| ");
	break;
      case TpipeErr:
	printf("|& ");
	break;
      default:
	fprintf(stderr, "Shouldn't get here\n");
	exit(-1);
      }

    if ( c->nargs > 1 ) {
      printf("[");
      for ( i = 1; c->args[i] != NULL; i++ )
	printf("%d:%s,", i, c->args[i]);
      printf("\b]");
    }
    putchar('\n');
    // this driver understands one command
    if ( !strcmp(c->args[0], "end") )
      exit(0);
  }
}

static void prPipe(Pipe p)
{
  int i = 0;
  Cmd c;

  if ( p == NULL )
    return;

  printf("Begin pipe%s\n", p->type == Pout ? "" : " Error");
  for ( c = p->head; c != NULL; c = c->next ) {
    printf("  Cmd #%d: ", ++i);
    prCmd(c);
  }
  printf("End pipe\n");
  prPipe(p->next);
}

int main(int argc, char *argv[], char** envp)
{
  signal(SIGINT, quitproc);
  signal(SIGTERM, sigterm);
  signal(SIGQUIT, SIG_IGN);
  
  Pipe p;

  int fd_ushrc, std_in, std_out, std_err;
  char hostname[128];
  int return_val;
  size_t len;
  return_val = gethostname(hostname, sizeof(hostname));
  //printf("Return value: %d\n", return_val);
  //printf("Hostname: %s\n", hostname);
  
  int ush_ret = ushrc_location();

  if(ush_ret == 1){
    //printf("USHRC is here : %s\n", ushrc_path);
    fd_ushrc = open(ushrc_path , O_RDONLY);
    std_in = dup(0);
    std_out = dup(1);
    std_err = dup(2);
    dup2(fd_ushrc, 0);
    close(fd_ushrc);
    p = parse();
    //int count_n = 1;
    
    while(strcmp(p->head->args[0], "end") != 0){
      //  printf("OUTPUT%d\n", count_n);
      myPipe(p);
      freePipe(p);
      p = parse();
      //count_n ++;
    }
    dup2(std_in, 0);
    dup2(std_out, 1);
    dup2(std_err, 2);
    setbuf(stdin, NULL);
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
  }
  //Execute commands from CMD
  while ( 1 ) {
    printf("%s%%", hostname);
    p = parse();

    if(!p)
      continue;
    
    if(strcmp(p->head->args[0], "end") == 0){
      printf("\n");
      exit(0);
    }
    if(p->head->args[0]){
      myPipe(p);
      freePipe(p);
    }
  }
  
}


void myPipe(Pipe p){
  pid_t pid;
  Cmd c;
  int std_in, std_out, std_err;
  std_in = dup(0);
  std_out = dup(1);
  std_err = dup(2);
  
  while(p != NULL){
    for ( c = p->head; c != NULL; c = c->next ) {
      
      int builtin_flag = -1;
      builtin_flag = is_builtin(c);
      
	//Case 0: '|' Input form pipe
	if(c->in == Tpipe || c->in == TpipeErr){
	  if(builtin_flag == -1){
	    dup2(fd[0], 0);
	    close(fd[0]);
	    //printf("CMD2: %s\n", c->args[0]);
	  }
	  else{
	    //Builtin command don't care about input 
	    close(fd[0]);
	    dup2(std_in, 0);
	  }
	}

	//Case 1: Output to pipe '|'
	if(c->out == Tpipe){
	  //printf("CASE-1: %s\n", c->args[0]);
	  pipe(fd);
	  dup2(fd[1], 1);
	  close(fd[1]);
	}
	//Case 2: Output & error to pipe '|&'
	if(c->out == TpipeErr){
	  //printf("CASE0: %s\n", c->args[0]);
	  pipe(fd);
	  dup2(fd[1], 1);
	  dup2(fd[1], 2);
	  close(fd[1]);
	}

	//Case 3: Input redirection '<'
	if(c->in == Tin){
	  if(builtin_flag == -1){
	    fd1 = open(c->infile, O_RDONLY, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	    dup2(fd1, 0);
	    close(fd1);
	  }
	}
	//Case 4: Output to file '>' rewrite contents of file(Done)
	if(c->out == Tout){
	  //printf("O/P redirect > %s\n", c->outfile);
	  fd2 = open(c->outfile, O_WRONLY | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	  dup2(fd2, 1);
	  close(fd2);
	}
	//Case 5: Output & error to file '>&' rewrite contents of file(Done)
	if(c->out == ToutErr){
	  //printf("O/P redirect > %s\n", c->outfile);
	  fd2 = open(c->outfile, O_WRONLY | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	  dup2(fd2, 2);
	  dup2(fd2, 1);
	  close(fd2);
	}
	//Case 6: Output to file '>>' append contents of file(Done)
	if(c->out == Tapp){
	  //printf("O/P redirect >> %s\n", c->outfile);
	  fd2 = open(c->outfile, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	  dup2(fd2, 1);
	  close(fd2);
	}
	//Case 7: Output & error to file '>>&' append contents of file(Done)
	if(c->out == TappErr){
	  //printf("O/P redirect > %s\n", c->outfile);
	  fd2 = open(c->outfile, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	  dup2(fd2, 1);
	  dup2(fd2, 2);
	  close(fd2);
	}


	//LAST COMMAND and output on cmd prompt(Tnil)
	if(c->next == NULL && c->out == Tnil){
	  //printf("LAST COMMAND\n");
	  dup2(std_out, 1);
	  dup2(std_err, 2);
	}

	//Not a builtin command
	if(builtin_flag == -1){
	  pid = fork();
    
	  if(pid == 0){

	    if(c->in != Tin && ((c->in != Tpipe) || (c->in != TpipeErr)))
	      close(fd[0]);

	    if(c->in != Tin && ((c->out != Tpipe) || (c->out != TpipeErr)))
	      close(fd[1]);
      
	    int ret_exec = execvp(c->args[0], c->args);
	    if(ret_exec == -1){


	      
	      char * pwd_n = (char *)malloc(256);
	      char * pwd_n1 = (char *)malloc(256);
	      if(c->args[0][0] == '/'){
	        strcpy(pwd_n, c->args[0]);
		if (0 == access(pwd_n, 0)) { 
		  fprintf(stderr,"%s: Permission denied.\n", c->args[0]);
		} 
		else { 
		  fprintf(stderr,"%s: Command not found.\n", c->args[0]);
	      
		}
		exit(0);
	      }
	       
	      if(errno != 2)
		fprintf(stderr,"%s: Permission denied.\n", c->args[0]);
	      else{
		fprintf(stderr,"%s: Command not found.\n", c->args[0]);
		}
	      exit(0);
	    }
	  }
    
	  else{
	    dup2(std_in, 0);
	    dup2(std_out, 1);
	    dup2(std_err, 2);
	    if(c->next == NULL){
	      //printf("Waiting for last command to execute\n");
	      int status;
	      waitpid(pid, &status, 0);
	      dup2(std_in, 0);
	      dup2(std_out, 1);
	      dup2(std_err, 2);
	      //printf("SUCCESS\n");
	    }
	  }
	}

	//Builtin Command
	if(builtin_flag >= 0){
	  setbuf(stdin, NULL);
	  setbuf(stdout, NULL);
	  setbuf(stderr, NULL);
	  //builtin command present in pipe
	  if(c->out != Tnil){
	    pid = fork();
	    if(pid != 0){
	      builtin_flag = -1;
	      int status;
	      wait(&status);
	      close(fd[1]);
	    }
	    else{
	      if(pid == 0)
		close(fd[0]);
	    }
	  }

	  switch(builtin_flag){
	  case 0:
	    builtin_cd(builtin_flag, c);
	    break;
	  case 1:
	    builtin_echo(builtin_flag, c);
	    break;
	  case 2:
	    builtin_logout(builtin_flag, c);
	    break;
	  case 3:
	    builtin_nice(builtin_flag, c);
	    break;
	  case 4:
	    builtin_pwd(builtin_flag, c);
	    break;
	  case 5:
	    builtin_setenv(builtin_flag, c);
	    break;
	  case 6:
	    builtin_unsetenv(builtin_flag, c);
	    break;
	  case 7:
	    builtin_where(builtin_flag, c);
	    break;
	  case 8:
	    break;
	  }

	  if(pid == 0)
	    exit(0);

	  dup2(std_in, 0);
	  dup2(std_out, 1);
	  dup2(std_err, 2);

	  if(c->next == NULL){
	    int status;
	    waitpid(pid, &status, 0);
	    dup2(std_in, 0);
	    dup2(std_out, 1);
	    dup2(std_err, 2);
	    //printf("SUCCESS\n");
	  }

	}
	
    }
    p = p->next;
  }
}

int ushrc_location(){
  int config_flag = 0;
  int access_flag = 0;
  char ushrc1[] = "/.ushrc";
  char * home_dir, * home_dir1;// = (char *)malloc(256);
  home_dir = getenv("HOME");
  strcpy(home_dir1, home_dir);
  strcat(home_dir1, ushrc1);
  
  int path_flag = access(home_dir1, F_OK);
  
  //Pathname cannot be resolved
  if(!(path_flag == 0)){
    //printf("command not found");
    return 0;
  }
  //else Pathname resolved
  else{
    int file_perm_flag = access(home_dir1, R_OK);

    //No Read permission on file
    if(!(file_perm_flag == 0)){
      //printf("Permission denied");
      return 0;
    }

    //.ushrc File found and also has read permissions
    else{
      strcpy(ushrc_path, home_dir1);
      return 1;
      //printf("Config file found in home dir %s\n", ushrc_path);
    }
  }
}


int is_builtin(Cmd c){
  int i;
  char * builtin[] = {"cd", "echo", "logout", "nice", "pwd", "setenv", "unsetenv", "where"};
  //printf("INSIDE builtin func %s\n", c->args[0]);
  for(i = 0; i<8 ; i++){
    //printf("%d\n", i);

    //If builtin command, return number >=0
    if(strcmp(c->args[0],builtin[i]) == 0)
      return i;
  }
  //else return -1
  return -1;
}


void builtin_cd(int builtin_flag, Cmd c){
  //Case 0: cd with no arguments
  int ret;
  if(c->nargs >= 3){
    fprintf(stderr,"%s: Too many arguments.\n", c->args[0]);
    return ;
  }
  if( c->args[1] == '\0'){
    char * homedir1 = (char *)malloc(256);
    homedir1 = getenv("HOME");
    chdir(homedir1);
  }
  //Case 1: cd with argument beginning with '/'
  else if(c->args[1][0] == '/'){
    int ret = chdir(c->args[1]);
    if(ret == -1)
      fprintf(stderr,"%s: No such file or directory.\n", c->args[1]);
  }
  else{
    char * pwd1 = (char *)malloc(256);
    getcwd(pwd1, 256);  //pwd1 will contain name of pwd
    int len = strlen(pwd1);
    pwd1[len] = '/';
    pwd1[len + 1] = '\0';
    strcat(pwd1, c->args[1]);
    int ret = chdir(pwd1);
    if(ret == -1)
      fprintf(stderr,"%s: No such file or directory.\n", c->args[1]);
  }
    
}

void builtin_echo(int builtin_flag, Cmd c){

  //printf("Argument 1 %s\n", c->args[1]);

  //Case 0: echo with no arguments
  if( c->args[1] == '\0')
    printf("\n");

  //Case 1: echo without $argument
  else{
    int arg_v = 1;
    while(c->args[arg_v] != '\0'){
      printf("%s ", c->args[arg_v]);
      arg_v ++;
    }
    printf("\n");
  }
}

void builtin_logout(int builtin_flag, Cmd c){
  exit(0);
}

void builtin_nice(int builtin_flag, Cmd c){

  int process = PRIO_PROCESS;
  int priority;

  int pid = getpid();
  
  if(c->nargs == 1){
    priority = 4;
    setpriority(process, pid, priority);
  }

  else if(c->nargs == 2){
    priority = atoi(c->args[1]);
    setpriority(process, pid, priority);
  }

  else if(c->nargs >= 3){
    priority = atoi(c->args[1]);
    char * command = c->args[2];

    int child_pid = fork();
    if(child_pid == 0){
      execvp(command, c->args+2);
    }
    else{
      int status;
      setpriority(process, child_pid, priority);
      wait(&status);
    }
  }
   
}

void builtin_pwd(int builtin_flag, Cmd c){
  char * pwd1 = (char *)malloc(256);
  if(c->nargs > 1)
    fprintf(stderr,"%s: ignoring non-option arguments.\n", c->args[0]);
  getcwd(pwd1, 256);  //pwd1 will contain name of pwd
  printf("%s\n", pwd1);
}

void builtin_setenv(int builtin_flag, Cmd c){
  if(c->nargs > 3){
    fprintf(stderr,"%s: Too many arguments.\n", c->args[0]);
    return ;
  }

  if(c->nargs == 3)
    setenv(c->args[1], c->args[2], 1);

  if(c->nargs == 1){
    int i = 1;
    char *s = *environ;
    
    for (; s; i++) {
      printf("%s\n", s);
      s = *(environ+i);
    }
  }
}

void builtin_unsetenv(int builtin_flag, Cmd c){

  unsetenv(c->args[1]);
  
}

void builtin_where(int builtin_flag, Cmd c){

  if(c->nargs == 1){
    fprintf(stderr,"%s: Too few arguments.\n", c->args[0]);
    return ;
  }
  
  char * path;
  char * path1 = (char *)malloc(256);
  path = getenv("PATH");
  //printf("PATH : %s\n", path);
  strcpy(path1, path);
  //printf("PATH1 : %s\n", path1);

  strcpy(c->args[0], c->args[1]);

  if(c->args[1] == '\0'){

  }
  else if(is_builtin(c) >= 0){
    printf("Builtin Command\n");
  }
  
  else{
    char ch;
    int count_path = 0;
    ch = path1[0];

    while(ch != '\0'){
      int count_var = 0;
      char * path_var = (char *)malloc(256);
      while(ch != ':' && ch!= '\0'){
	path_var[count_var] = ch;
	count_var ++;
	count_path ++;
	ch = path1[count_path];
      }
      path_var[count_var++] = '/';
      path_var[count_var] = '\0';
      strcat(path_var, c->args[1]);
      
      //printf("Path var for comparision: %s\n", path_var);
      //check if file is present or not
      if(access(path_var, F_OK) == 0){
	//check if there is execute permission on file
	if(access(path_var, X_OK) == 0){
	  printf("%s\n", path_var);
	}
      }

      if(ch == '\0')
	break;
      else{
	count_path ++;
	ch = path1[count_path];
      }
      free(path_var);
    }
  }
}

void quitproc()
{
  //fprintf(stdout,"Caught signal\n");
  //ignore quit signal
}

void sigterm()
{
  //fprintf(stdout,"Caught term signal\n");
  //shell catches term signal
  exit(0);
}

/*........................ end of main.c ....................................*/
