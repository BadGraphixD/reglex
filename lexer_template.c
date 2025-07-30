#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void parse_0();
extern void parse_1();
extern void parse_2();

typedef struct char_cache {
  int *data;
  size_t start;     // points to first item
  size_t end;       // points one after last item
  size_t size;      // amount of stored items
  size_t ptr;       // points to next unread item
  size_t remaining; // amount of unread items
  size_t cap;       // total capacity of stack
} char_cache_t;

char_cache_t char_cache = {0};

void push(int c) {
  if (char_cache.size >= char_cache.cap) {
    size_t new_capacity = char_cache.cap == 0 ? 64 : char_cache.cap * 2;
    int *new_data = malloc(new_capacity * sizeof(int));
    char_cache.cap = new_capacity;

    if (char_cache.start < char_cache.end) {
      memcpy(new_data, &char_cache.data[char_cache.start], char_cache.size);

    } else if (char_cache.start > char_cache.end) {
      size_t start_size = char_cache.cap - char_cache.start;
      memcpy(new_data, &char_cache.data[char_cache.start], start_size);
      memcpy(&new_data[start_size], char_cache.data, char_cache.end);
    }
  }
  char_cache.size++;
  char_cache.data[char_cache.end] = c;
  char_cache.end = (char_cache.end + 1) % char_cache.cap;
  char_cache.ptr = char_cache.end;
}

void skip() {
  size_t to_skip = char_cache.size - char_cache.remaining;
  char_cache.size -= to_skip;
  char_cache.start = (char_cache.start + to_skip) % char_cache.cap;
}

int read() {
  char_cache.remaining--;
  int result = char_cache.data[char_cache.ptr];
  char_cache.ptr = (char_cache.ptr + 1) % char_cache.cap;
  return result;
}

void accept() {}

void reject() {}

int next() {
  if (char_cache.remaining > 0) {
    return read();
  }
  int c = fgetc(stdin);
  push(c);
  return c;
}

size_t last_checkpoint = 0;

void checkpoint() { last_checkpoint = char_cache.size - char_cache.remaining; }
