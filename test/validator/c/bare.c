// RUN: %dragonegg -g -x c -S -o /dev/null - < %s

static int n;

static int foo();
static int bar();

static int foo() {
  return 0;
}

static int bar() {
  if (foo() != 0)
    return -1;
  return 0;
}

int main(int argc, char *argv[]) {
  return 0;
}
