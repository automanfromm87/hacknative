#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HACK_SDL2
#include <SDL.h>

// Poll all pending events, return 1 if user requested quit, 0 otherwise
int hack_sdl_poll_quit(void) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT)
      return 1;
  }
  return 0;
}
#endif

// ---- String operations ----

char *hack_strcat(const char *a, const char *b) {
  size_t la = strlen(a);
  size_t lb = strlen(b);
  char *result = (char *)malloc(la + lb + 1);
  memcpy(result, a, la);
  memcpy(result + la, b, lb + 1);
  return result;
}

int hack_strlen(const char *s) {
  return (int)strlen(s);
}

char *hack_substr(const char *s, int start, int len) {
  int slen = (int)strlen(s);
  if (start < 0) start = 0;
  if (start >= slen) {
    char *r = (char *)malloc(1);
    r[0] = '\0';
    return r;
  }
  if (start + len > slen) len = slen - start;
  char *result = (char *)malloc(len + 1);
  memcpy(result, s + start, len);
  result[len] = '\0';
  return result;
}

int hack_intval(const char *s) {
  return atoi(s);
}

char *hack_str_repeat(const char *s, int n) {
  if (n <= 0) {
    char *r = (char *)malloc(1);
    r[0] = '\0';
    return r;
  }
  size_t slen = strlen(s);
  size_t total = slen * n;
  char *result = (char *)malloc(total + 1);
  for (int i = 0; i < n; i++) {
    memcpy(result + i * slen, s, slen);
  }
  result[total] = '\0';
  return result;
}

// ---- Vec (dynamic int array) ----

typedef struct {
  int *data;
  int size;
  int cap;
} HackVec;

HackVec *hack_vec_new(void) {
  HackVec *v = (HackVec *)malloc(sizeof(HackVec));
  v->data = (int *)malloc(sizeof(int) * 8);
  v->size = 0;
  v->cap = 8;
  return v;
}

void hack_vec_push(HackVec *v, int val) {
  if (v->size >= v->cap) {
    v->cap *= 2;
    v->data = (int *)realloc(v->data, sizeof(int) * v->cap);
  }
  v->data[v->size++] = val;
}

int hack_vec_get(HackVec *v, int index) {
  if (index < 0 || index >= v->size) {
    fprintf(stderr, "Vec index out of bounds: %d (size=%d)\n", index, v->size);
    return 0;
  }
  return v->data[index];
}

void hack_vec_set(HackVec *v, int index, int val) {
  if (index < 0 || index >= v->size) {
    fprintf(stderr, "Vec index out of bounds: %d (size=%d)\n", index, v->size);
    return;
  }
  v->data[index] = val;
}

int hack_vec_size(HackVec *v) {
  return v->size;
}

void hack_print_r_vec(HackVec *v) {
  printf("vec[");
  for (int i = 0; i < v->size; i++) {
    if (i > 0) printf(", ");
    printf("%d", v->data[i]);
  }
  printf("]\n");
}

// ---- Dict (string -> int hash map) ----

typedef struct {
  char *key;
  int value;
} DictEntry;

typedef struct {
  DictEntry *entries;
  int size;
  int cap;
} HackDict;

HackDict *hack_dict_new(void) {
  HackDict *d = (HackDict *)malloc(sizeof(HackDict));
  d->entries = (DictEntry *)malloc(sizeof(DictEntry) * 8);
  d->size = 0;
  d->cap = 8;
  return d;
}

void hack_dict_set(HackDict *d, const char *key, int val) {
  // Update existing key
  for (int i = 0; i < d->size; i++) {
    if (strcmp(d->entries[i].key, key) == 0) {
      d->entries[i].value = val;
      return;
    }
  }
  // Add new key
  if (d->size >= d->cap) {
    d->cap *= 2;
    d->entries = (DictEntry *)realloc(d->entries, sizeof(DictEntry) * d->cap);
  }
  d->entries[d->size].key = strdup(key);
  d->entries[d->size].value = val;
  d->size++;
}

int hack_dict_get(HackDict *d, const char *key) {
  for (int i = 0; i < d->size; i++) {
    if (strcmp(d->entries[i].key, key) == 0) {
      return d->entries[i].value;
    }
  }
  fprintf(stderr, "Dict key not found: %s\n", key);
  return 0;
}

int hack_dict_size(HackDict *d) {
  return d->size;
}

const char *hack_dict_key_at(HackDict *d, int index) {
  if (index < 0 || index >= d->size) {
    fprintf(stderr, "Dict index out of bounds: %d\n", index);
    return "";
  }
  return d->entries[index].key;
}

int hack_dict_val_at(HackDict *d, int index) {
  if (index < 0 || index >= d->size) {
    fprintf(stderr, "Dict index out of bounds: %d\n", index);
    return 0;
  }
  return d->entries[index].value;
}

void hack_print_r_dict(HackDict *d) {
  printf("dict[");
  for (int i = 0; i < d->size; i++) {
    if (i > 0) printf(", ");
    printf("\"%s\" => %d", d->entries[i].key, d->entries[i].value);
  }
  printf("]\n");
}
