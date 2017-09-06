// RUN: %dragonegg -g -x c -S -o /dev/null - < %s

#include <stdio.h>

int main(int argc, char *argv[]) {
  printf("Hello world\n");
  return 0;
}
