#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char const *argv[]) {
  if (argc != 2) {
    fprintf(2, "usage: sleep <time>\n");
	exit(1);
  }
  sleep(atoi(argv[1]) * 10);
  exit(0);
}
