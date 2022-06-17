#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
char *log_wrapper(void (*request_handler)(int), int fd) {
  struct timeval t1, t2;
  double elapsedTime;

  gettimeofday(&t1, NULL);
  request_handler(fd);
  close(fd);
  gettimeofday(&t2, NULL);

  elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000.0;    // sec to ms
  elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000.0; // us to ms

  char *text = malloc(sizeof(char) * 1000);
  sprintf(text, "%ld: request took: %f", t1.tv_sec, elapsedTime);
  return text;
}
