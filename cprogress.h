/*
  CPROGRESS - a simple progress bar under ANSI console
  2024 @ Julian Droske


  INTRODUCTION
  ============

  Show progress with pseudo-multi-thread support with immediate mode design.

  Although it's a single-header library, it can also be used as a separate library, as
  most features are configured on runtime, see below.

  Currently this lib only expected to run on Linux, since only POSIX apis are used,
  e.g. ioctl, ANSI sequence code, etc. Windows may be supported in the future, or even,
  without any platform-specific code.


  USAGE
  =====

  This is a single-header library, do the following anywhere to include this lib

  | #include "cprogress.h"

  To include the implementation, define the CPROGRESS_IMPL before including:

  | #define CPROGRESS_IMPL
  | #include "cprogress.h"

  You need to define format before showing anything. CPROGRESS allows you to define it
  while creating the instance.

  | cprogress_t cprogress = cprogress_create(fmt: string, thread_count: int);

  [fmt] defines what it shows while displaying, see FORMAT below.
  [thread_count] defines how many progresses will be shown, should be bigger than zero.
  Returns an instance object. Any other APIs rely on it.
  Errors are indicated with [cprogress.error], which is zero when everything works fine.

  Then you may want to update every progress with:

  | cprogress_updatethread_percentage(cprogress: cprogress_t *, thread_index: int, percentage: float);

  It does not immediately update UI, but updates data only, which will be shown with
  cprogress_render*(...).
  [cprogress] is usually [&cprogress] which was created above.
  [thread_index] describes which thread data you want to update.
  [percentage] is a float number between 0 and 100.

  or update title with:

  | cprogress_updatethread_title(cprogress: cprogress_t *, thread_index: int, const char *title);

  [title] will be duplicated so it's safe to free it after calling.

  Updaters can be called from anywhere e.g. any thread.

  Then in your main thread, you can write something like:

  | while (cprogress_stillrunning(cprogress: cprogress_t *)) {
  |   cprogress_render(cprogress: cprogress_t *);
  |   cprogress_waitfps(fps: int);
  | }

  This is actually a basic concept of immediate mode ui. By calling cprogress_stillrunning(...),
  program knows whether the whole process is complete. There are two reasons for returning
  false by cprogress_stillrunning(...):
    All progresses have reached 100%.
    Anytime cprogress_abort(cprogress: cprogress_t *) has been called.


  FORMAT
  ======

  Let's get started with an example.

  | $=t [$40b#] $p%

  Like printf(3), there is a "syntax":

  $[width]conversion[arg1]

  in which, [conversion] can be either:
    t: prints title, can be changed using cprogress_updatethread_title(...)
    b: the progress bar without any decorations
      In this case, arg1 is made use of displaying the progress that is done
      and width is a necessary arg.
    p: prints percentage, in float
  while for [width]:
    when as an integer: limits length and pad tailing spaces when not satisfied
    when equals to "=": auto span, like [flex: 1] in flex boxes in CSS
      This can only show once.

  Any other characters or syntaxes will be ignored and be output directly.

  The example above will output like:

    |                                                                  |
    |Simple task      [############                            ] 31.00%|
    |                                                                  |


  EXAMPLE
  =======

  The code below is a very basic OPCODE of drawing multithread progress.

  | #include "stdio.h"
  |
  | #define CPROGRESS_IMPL
  | #include "cprogress.h"
  |
  | cprogress_t cprogress;
  |
  | void thread_function(int thread_index) {
  |   // assuming that you have thread management
  |   float percentage = opcode_get_work_percentage(thread_index);
  |   // update data
  |   cprogress_updatethread_percentage(&cprogress, thread_index, percentage);
  | }
  |
  | int main(void) {
  |   cprogress = cprogress_create("$=t [$40b#] $p%", 4);
  |   if (cprogress.error) {
  |     printf("error occured with code %d\n", cprogress.error);
  |     return 1;
  |   }
  |
  |   opcode_start_threads();
  |
  |   // render
  |   while (cprogress_stillrunning(&cprogress)) {
  |     cprogress_render(&cprogress);
  |     cprogress_waitfps(30);
  |   }
  |
  |   cprogress_destroy(&cprogress);
  |
  |   return 0;
  | }

*/

#ifndef CPROGRESS_H
#define CPROGRESS_H



#include "stdint.h"


#define CPROGRESS_UNDEF (-1)


/* module: stralloc */
typedef struct {
  char *buffer;
  size_t length;
  size_t size; /* indicates max length */
} cprogress_stralloc_t;


struct cprogress;


/* error */
typedef enum {
  CPROGRESS_ERROR_OK = 0,
  CPROGRESS_ERROR_INVAL = 1,
  CPROGRESS_ERROR_BUFFUL,
  CPROGRESS_ERROR_INTERNAL,
} cprogress_error_t;


/* displaychunk */
typedef enum {
  CPROGRESS_DISPLAYCHUNK_UNKNOWN = 0,
  CPROGRESS_DISPLAYCHUNK_LITERAL = 1,
  CPROGRESS_DISPLAYCHUNK_TITLE,
  CPROGRESS_DISPLAYCHUNK_BAR,
  CPROGRESS_DISPLAYCHUNK_PERCENTAGE,
} cprogress_displaychunk_type_t;

typedef struct {
  cprogress_displaychunk_type_t type;
  union {
    char fill_char;
    const char *literal;
  };
  size_t literal_length;

  int is_autospan;
  size_t span_width;

  /* cache */
  size_t display_width;
} cprogress_displaychunk_t;

#define cprogress_displaychunk_foreach(cp, name) for (cprogress_displaychunk_t *name = (cp)->displaychunks; name->type; ++name)


/* threadinfo */
typedef struct {
  /* persistent */
  int is_valid; /* indicate if it's a EOF */
  int thread_index;

  int is_running;
  char *title;
  float percentage;

  /* internal */
  int is_just_stopped;
} cprogress_threadinfo_t;

#define cprogress_getthreadinfo(cp, thread_index) ((cp)->threadinfos[thread_index])
#define cprogress_threadinfo_getindex(threadinfo) ((threadinfo)->thread_index)
#define cprogress_threadinfo_foreach(cp, name) for (cprogress_threadinfo_t *name = (cp)->threadinfos; name->is_valid; ++name)


/* subscribe */
typedef enum {
  CPROGRESS_EVENT_NONE = CPROGRESS_UNDEF, /* a placeholder */

  CPROGRESS_EVENT_THREADSTART, /* a thread was started */
  CPROGRESS_EVENT_THREADFINISH, /* a thread was finished */
  CPROGRESS_EVENT_FINISH, /* the full process was finished */

  CPROGRESS_EVENT_LENGTH, /* indicate the maximum number of this enum, only use internally */
} cprogress_event_type_t;

typedef void (cprogress_eventsubscriber_func_t (struct cprogress *cprogress, cprogress_event_type_t type, int thread_index));


/* instance */
typedef struct cprogress {
  cprogress_error_t error;

  int has_autospan_element; /* deprecated */

  size_t displaychunks_length;
  cprogress_displaychunk_t *displaychunks;

  cprogress_stralloc_t stralloc;

  int is_running;
  int last_alive_thread_count;
  size_t threadinfos_length;
  cprogress_threadinfo_t *threadinfos;

  cprogress_eventsubscriber_func_t *subscribers[CPROGRESS_EVENT_LENGTH];
} cprogress_t;



/* instance */
cprogress_t cprogress_create(const char *fmt, int thread_count);
void cprogress_destroy(cprogress_t *cprogress);

/* object */


/* task/thread controller */
void cprogress_threadinfo_start(cprogress_threadinfo_t *threadinfo);
void cprogress_threadinfo_abort(cprogress_threadinfo_t *threadinfo);
void cprogress_startthread(cprogress_t *cprogress, int thread_index);
void cprogress_abortthread(cprogress_t *cprogress, int thread_index);

void cprogress_startallthreads(cprogress_t *cprogress);

/* view basic */
size_t cprogress_writeliteral(char *buf, size_t buf_len, const char *literal, size_t alloc_width);
size_t cprogress_writepercentage(char *buf, size_t buf_len, float percentage, size_t alloc_width);
size_t cprogress_writeprogressbar(char *buf, size_t buf_len, char fill_char, float percentage);

void cprogress_writeline(cprogress_t *cprogress, char *buf, size_t buf_len, size_t console_width, const char *title, float percentage);
void cprogress_printline(cprogress_t *cprogress, const char *title, float percentage);

/* view controller */
void cprogress_abort(cprogress_t *cprogress);
int cprogress_stillrunning(cprogress_t *cprogress);
void cprogress_waitfps(int fps);
void cprogress_render(cprogress_t *cprogress);
void cprogress_rendersum(cprogress_t *cprogress, const char *title);

/* view controller alternative: one line to show all till none left */
void cprogress_render_tillcomplete(cprogress_t *cprogress, int fps);

/* data provider */
void cprogress_threadinfo_updatetitle(cprogress_threadinfo_t *threadinfo, const char *title);
void cprogress_threadinfo_updatepercentage(cprogress_threadinfo_t *threadinfo, float percentage);
void cprogress_updatethread_title(cprogress_t *cprogress, int thread_index, const char *title);
void cprogress_updatethread_percentage(cprogress_t *cprogress, int thread_index, float percentage);

/* event controller */
void cprogress_subscribeevent(cprogress_t *cprogress, cprogress_event_type_t type, cprogress_eventsubscriber_func_t *func);
void cprogress_emitevent(cprogress_t *cprogress, cprogress_event_type_t type, int thread_index);



#endif /* !CPROGRESS_H */




#ifdef CPROGRESS_IMPL
#ifndef CPROGRESS_IMPL_



#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "time.h"

#include "sys/ioctl.h"
#include "unistd.h"

#define CPROGRESS_CONSOLE_UPDATEWIDTH_LOOPCOUNT 10
#define CPROGRESS_DISPLAYCHUNK_MAXLEN 16


/*----------------------------------------------------------------------------
| utils & stralloc
----------------------------------------------------------------------------*/

void cprogress_msleep(long ms) {
  struct timespec ts = {
    .tv_sec = ms / 1000L,
    .tv_nsec = (ms % 1000) * 1000000
  };
  nanosleep(&ts, NULL);
}

char *cprogress_strdup(const char *str) {
  if (str) {
    char * newstr = (char *) malloc(strlen(str));
    strcpy(newstr, str);
    return newstr;
  }
  return NULL;
}

cprogress_stralloc_t cprogress_stralloc_create(size_t size) {
  char *buffer = (char *) malloc(size);
  cprogress_stralloc_t stralloc = {
    .buffer = buffer,
    .length = 0,
    .size = buffer? size: 0
  };
  return stralloc;
}

char *cprogress_stralloc_alloc(cprogress_stralloc_t *stralloc, const char *str, size_t len) {
  if (!stralloc || !stralloc->buffer) return NULL;

  size_t actual_length = len + 1;
  if (stralloc->length + actual_length >= stralloc->size) return NULL;

  char *dest = stralloc->buffer + stralloc->length;
  strncpy(dest, str, len);
  stralloc->length += actual_length;
  return dest;
}

void cprogress_stralloc_destroy(cprogress_stralloc_t *stralloc) {
  if (stralloc) {
    if (stralloc->buffer) {
      free(stralloc->buffer);
      stralloc->buffer = NULL;
    }
  }
}


/*----------------------------------------------------------------------------
| instance
----------------------------------------------------------------------------*/

typedef enum {
  CPROGRESS_TOKEN_UNKNOWN = 0,

  CPROGRESS_TOKEN_FMTBEGIN = 1,
  CPROGRESS_TOKEN_MARKAUTOSPAN,

  CPROGRESS_TOKEN_NUMBER,
  CPROGRESS_TOKEN_LITERAL_CHAR,
  /* CPROGRESS_TOKEN_LITERAL, */
} cprogress_token_type_t;

typedef struct {
  cprogress_token_type_t type;
  union {
    int number;
    char ch;
    /* const char *literal; */
  };
  size_t read_length;
} cprogress_token_t;

#define cprogress_isfmtbegin(ch) (ch == '$')
#define cprogress_ismarkautospan(ch) (ch == '=')
#define cprogress_isnumber(ch) ((ch) >= '0' && (ch) <= '9')
#define cprogress_isliteral(ch) ((ch) >= 'a' && (ch) <= 'z' || (ch) >= 'A' && (ch) <= 'Z')

cprogress_token_type_t cprogress_findchartokentype(char ch) {
  if (cprogress_isfmtbegin(ch)) {
    return CPROGRESS_TOKEN_FMTBEGIN;
  } else if (cprogress_ismarkautospan(ch)) {
    return CPROGRESS_TOKEN_MARKAUTOSPAN;
  } else if (cprogress_isnumber(ch)) {
    return CPROGRESS_TOKEN_NUMBER;
  } else if (cprogress_isliteral(ch)) {
    return CPROGRESS_TOKEN_LITERAL_CHAR;
  }

  return CPROGRESS_TOKEN_UNKNOWN;
}

cprogress_token_t cprogress_peektoken(const char *str) {
  cprogress_token_t token = {};

  for (const char *ch = str; *ch; ++ch) {
    if (!token.type) {
      token.type = cprogress_findchartokentype(*ch);
    } else {
      cprogress_token_type_t char_token_type = cprogress_findchartokentype(*ch);
      if (char_token_type != token.type) return token;
    }

    int is_done = 0;

    switch (token.type) {
      default: /* this should never happen */
      case CPROGRESS_TOKEN_UNKNOWN:
        return token;
      case CPROGRESS_TOKEN_NUMBER:
        token.number *= 10;
        token.number += *ch - '0';
        break;
      /* these are single-char cases */
      case CPROGRESS_TOKEN_FMTBEGIN:
      case CPROGRESS_TOKEN_MARKAUTOSPAN:
      case CPROGRESS_TOKEN_LITERAL_CHAR:
        token.ch = *ch;
        is_done = 1;
        break;
      /*
      case CPROGRESS_TOKEN_LITERAL:
        token.literal = str;
        break;
      */
    }
    ++token.read_length;

    if (is_done) break;
  }

  return token;
}


int cprogress_pushchunk(cprogress_t *cprogress, cprogress_displaychunk_t displaychunk) {
  if (cprogress->displaychunks_length >= CPROGRESS_DISPLAYCHUNK_MAXLEN - 1) return 1;
  cprogress->displaychunks[cprogress->displaychunks_length++] = displaychunk;
  return 0;
}


#define _cprogress_create_returnerror(e) { cprogress_destroy(&cprogress); return (cprogress_t) { .error = e }; }
cprogress_t cprogress_create(const char *fmt, int thread_count) {
  cprogress_t cprogress = {
    .displaychunks = (cprogress_displaychunk_t *) malloc(CPROGRESS_DISPLAYCHUNK_MAXLEN * sizeof(cprogress_displaychunk_t)),
    .stralloc = cprogress_stralloc_create(strlen(fmt)),
    .is_running = 1,
    .threadinfos_length = thread_count,
    .threadinfos = (cprogress_threadinfo_t *) malloc((thread_count + 1) * sizeof(cprogress_threadinfo_t))
  };

  if (!cprogress.displaychunks || !cprogress.stralloc.buffer || !cprogress.threadinfos)
    _cprogress_create_returnerror(CPROGRESS_ERROR_INTERNAL);

  for (int i = 0; i < cprogress.threadinfos_length; ++i) {
    cprogress.threadinfos[i] = (cprogress_threadinfo_t) { .is_valid = 1, .thread_index = i };
  }
  cprogress.threadinfos[cprogress.threadinfos_length] = (cprogress_threadinfo_t) { .is_valid = 0 };

  const char *literal = NULL;
  size_t literal_length = 0;

  char last_ch = 0;
  for (const char *chptr = fmt; *chptr; ++chptr) {
    if (cprogress_isfmtbegin(*chptr) && !cprogress_isfmtbegin(last_ch)) {
      /* save previous literal if present */
      if (literal && literal_length) {
        literal = cprogress_stralloc_alloc(&cprogress.stralloc, literal, literal_length);
        cprogress_displaychunk_t displaychunk = {
          .type = CPROGRESS_DISPLAYCHUNK_LITERAL,
          .literal = literal,
          .literal_length = literal_length,
          .span_width = CPROGRESS_UNDEF
        };
        if (cprogress_pushchunk(&cprogress, displaychunk))
          _cprogress_create_returnerror(CPROGRESS_ERROR_BUFFUL);

        /* reinit literal vars */
        literal = NULL;
        literal_length = 0;
      }

      cprogress_displaychunk_t displaychunk = {
        .span_width = CPROGRESS_UNDEF
      };

      /* parse current token */
      char fmt_name = 0;
      ++chptr; /* move on to next char, marking fmtbegin as "already parsed" */
      while (*chptr && !fmt_name) {
        cprogress_token_t token = cprogress_peektoken(chptr);
        if (token.type == CPROGRESS_TOKEN_MARKAUTOSPAN) {
          displaychunk.is_autospan = 1;
          /* only accept one element that is auto spanned */
          if (cprogress.has_autospan_element) _cprogress_create_returnerror(CPROGRESS_ERROR_INVAL);
          cprogress.has_autospan_element = 1;
        } else if (token.type == CPROGRESS_TOKEN_NUMBER) {
          displaychunk.span_width = token.number;
        } else if (token.type == CPROGRESS_TOKEN_LITERAL_CHAR) {
          fmt_name = token.ch;
        } else {
          _cprogress_create_returnerror(CPROGRESS_ERROR_INVAL);
        }

        chptr += token.read_length;
      }

      /* format should either be spanned or at fixed length */
      if (displaychunk.span_width != CPROGRESS_UNDEF && displaychunk.is_autospan) {
        _cprogress_create_returnerror(CPROGRESS_ERROR_INVAL);
      }

      /* progress bar must has a specific length */
      if (displaychunk.type == CPROGRESS_DISPLAYCHUNK_BAR &&
        displaychunk.span_width == CPROGRESS_UNDEF && !displaychunk.is_autospan) {
        _cprogress_create_returnerror(CPROGRESS_ERROR_INVAL);
      }

      /* such weird, we need to move back to current formst mark
        is because chptr marks chars that are already read before the next loop
        for instance, reading one char makes chptr unchanged */
      --chptr;

      char peeked_next_char = *(chptr + 1);
      switch (fmt_name) {
        default:
          _cprogress_create_returnerror(CPROGRESS_ERROR_INVAL);
        /* title, $[number]t */
        case 't':
          displaychunk.type = CPROGRESS_DISPLAYCHUNK_TITLE;
          break;
        /* progress bar, $[number]b[char] */
        case 'b':
          displaychunk.type = CPROGRESS_DISPLAYCHUNK_BAR;
          if (peeked_next_char) {
            displaychunk.fill_char = peeked_next_char;
            ++chptr;
          } else {
            _cprogress_create_returnerror(CPROGRESS_ERROR_INVAL);
          }
          break;
        /* progress percent, $[number]p */
        case 'p':
          displaychunk.type = CPROGRESS_DISPLAYCHUNK_PERCENTAGE;
          break;
      }

      /* we are ready to push the chunk */
      if (cprogress_pushchunk(&cprogress, displaychunk)) _cprogress_create_returnerror(CPROGRESS_ERROR_BUFFUL);
    } else {
      // TODO remove double $ */
      if (!literal) literal = chptr;
      /* not a fmt definition, just get into literal pool */
      ++literal_length;
    }

    last_ch = *chptr;
  }

  /* deal with tailing literal */
  if (literal_length) {
    literal = cprogress_stralloc_alloc(&cprogress.stralloc, literal, literal_length);
    cprogress_displaychunk_t displaychunk = {
      .type = CPROGRESS_DISPLAYCHUNK_LITERAL,
      .literal = literal,
      .literal_length = literal_length,
      .span_width = CPROGRESS_UNDEF
    };
    if (cprogress_pushchunk(&cprogress, displaychunk)) _cprogress_create_returnerror(CPROGRESS_ERROR_BUFFUL);
  }

  if (cprogress_pushchunk(&cprogress, (cprogress_displaychunk_t) { .type = CPROGRESS_DISPLAYCHUNK_UNKNOWN }))
    _cprogress_create_returnerror(CPROGRESS_ERROR_BUFFUL);

  return cprogress;
}


#define _cprogress_destroy_tryfree(v) if (v) { free(v); v = NULL; }
void cprogress_destroy(cprogress_t *cprogress) {
  if (cprogress) {
    _cprogress_destroy_tryfree(cprogress->displaychunks);
    cprogress_stralloc_destroy(&cprogress->stralloc);
    if (cprogress->threadinfos) {
      cprogress_threadinfo_foreach(cprogress, threadinfo) {
        cprogress_threadinfo_abort(threadinfo);
      }
      _cprogress_destroy_tryfree(cprogress->threadinfos);
    }
  }
}



/*----------------------------------------------------------------------------
| task/thread controller
----------------------------------------------------------------------------*/

void cprogress_threadinfo_start(cprogress_threadinfo_t *threadinfo) {
  if (!threadinfo) return;

  if (threadinfo->title) {
    free(threadinfo->title);
  }
  threadinfo->title = NULL;
  threadinfo->percentage = 0;
  threadinfo->is_running = 1;
}

void cprogress_threadinfo_abort(cprogress_threadinfo_t *threadinfo) {
  if (!threadinfo) return;

  threadinfo->is_running = 0;
  threadinfo->is_just_stopped = 1;
  /* let cprogress_threadinfo_start(...) and cprogress_abort(...) clean up everything
    because cprogress_render(...) uses the data here */
}

void cprogress_startthread(cprogress_t *cprogress, int thread_index) {
  if (!cprogress || thread_index < 0 || thread_index >= cprogress->threadinfos_length) return;

  cprogress_threadinfo_start(&cprogress_getthreadinfo(cprogress, thread_index));
}

void cprogress_abortthread(cprogress_t *cprogress, int thread_index) {
  if (!cprogress || thread_index < 0 || thread_index >= cprogress->threadinfos_length) return;

  cprogress_threadinfo_abort(&cprogress_getthreadinfo(cprogress, thread_index));
}

void cprogress_startallthreads(cprogress_t *cprogress) {
  if (!cprogress) return;

  cprogress_threadinfo_foreach(cprogress, threadinfo) {
    cprogress_threadinfo_start(threadinfo);
  }
}


/*----------------------------------------------------------------------------
| view basic
----------------------------------------------------------------------------*/

// TODO utf8
size_t cprogress_charlen(const char *buf) {
  if (!buf || !*buf) return 0;
  return 1;
}

size_t cprogress_measurechar(const char *buf) {
  size_t len = cprogress_charlen(buf);
  return len <= 1? len: 2;
}

size_t cprogress_measuredisplaychunk(cprogress_displaychunk_t *displaychunk, const char *str, size_t autospan_width) {
  if (displaychunk->span_width != CPROGRESS_UNDEF) return displaychunk->span_width;
  if (displaychunk->type == CPROGRESS_DISPLAYCHUNK_LITERAL) return displaychunk->literal_length;
  if (str) {
    size_t width = 0;
    while (*str) {
      str += cprogress_charlen(str);
      width += cprogress_measurechar(str);
    }
    return width;
  }
  return autospan_width;
}

size_t cprogress_snprintw(char *buf, size_t buf_len, const char *literal, size_t alloc_width) {
  size_t written_length = 0;
  size_t display_width = 0;

  const char *ptr = literal;
  do {
    size_t char_length = cprogress_charlen(ptr);

    size_t after_length = written_length + char_length;
    if (after_length > buf_len) break;
    size_t after_width = display_width + cprogress_measurechar(ptr);
    if (alloc_width != CPROGRESS_UNDEF && after_width > alloc_width) break;

    memcpy(buf + written_length, ptr, char_length);
    ptr += char_length;
    written_length = after_length;
    display_width = after_width;
  } while (*ptr);

  if (alloc_width != CPROGRESS_UNDEF && display_width < alloc_width) {
    while (written_length < buf_len && display_width < alloc_width) {
      buf[display_width] = ' ';
      ++display_width; ++written_length;
    }
  }

  return written_length;
}

/* it must to be one-width-per-char as in ASCII, so there's no need to implement one */
#define cprogress_sprintpercentage(buf, buf_len, percentage) snprintf(buf, buf_len, "%.2f", percentage)


size_t cprogress_writeliteral(char *buf, size_t buf_len, const char *literal, size_t alloc_width) {
  if (alloc_width == CPROGRESS_UNDEF && !literal) {
    /* what should we draw? */
    return 0;
  }

  if (!literal) {
    size_t written_length = 0;
    size_t display_width = 0;
    while (written_length < buf_len && display_width < alloc_width) {
      buf[written_length] = ' ';
      ++written_length; ++display_width;
    }
    return written_length;
  }

  return cprogress_snprintw(buf, buf_len, literal, alloc_width);
}

size_t cprogress_writepercentage(char *buf, size_t buf_len, float percentage, size_t alloc_width) {
  char percentage_string[7] = {};
  cprogress_sprintpercentage(percentage_string, 6, percentage);
  return cprogress_writeliteral(buf, buf_len, percentage_string, alloc_width);
}

size_t cprogress_writeprogressbar(char *buf, size_t buf_len, char fill_char, float percentage) {
  int left_length = buf_len * (percentage / 100.0);

  int curr = 0;
  while (curr < left_length) buf[curr++] = fill_char;
  while (curr < buf_len) buf[curr++] = ' ';

  return buf_len;
}


void cprogress_writeline(cprogress_t *cprogress, char *buf, size_t buf_len, size_t console_width, const char *title, float percentage) {

  char *line = buf;
  if (!line || console_width <= 1) return;

  char percentage_string[7] = {};
  cprogress_sprintpercentage(percentage_string, 6, percentage);

  /* measure */

  size_t taken_display_width = 0;
  cprogress_displaychunk_foreach(cprogress, displaychunk) {
    if (!displaychunk->is_autospan) {
      size_t display_width = 0;
      switch (displaychunk->type) {
        case CPROGRESS_DISPLAYCHUNK_LITERAL:
          display_width = cprogress_measuredisplaychunk(displaychunk, displaychunk->literal, CPROGRESS_UNDEF);
          break;
        case CPROGRESS_DISPLAYCHUNK_TITLE:
          display_width = cprogress_measuredisplaychunk(displaychunk, title, CPROGRESS_UNDEF);
          break;
        case CPROGRESS_DISPLAYCHUNK_BAR:
          display_width = cprogress_measuredisplaychunk(displaychunk, NULL, CPROGRESS_UNDEF);
          break;
        case CPROGRESS_DISPLAYCHUNK_PERCENTAGE:
          display_width = cprogress_measuredisplaychunk(displaychunk, percentage_string, CPROGRESS_UNDEF);
          break;
      }
      displaychunk->display_width = display_width;
      taken_display_width += display_width;
    }
  }

  size_t autospan_width = console_width > taken_display_width?
    console_width - taken_display_width:
    0;

  /* actual render */

  char *ptr = line;
  size_t avail_length = buf_len;
  cprogress_displaychunk_foreach(cprogress, displaychunk) {
    if (avail_length <= 0) break;

    size_t display_width = displaychunk->is_autospan? autospan_width: displaychunk->display_width;
    if (display_width == CPROGRESS_UNDEF) {} /* well, we have no idea */
    /* TODO error message */

    size_t print_length = 0;
    switch (displaychunk->type) {
      case CPROGRESS_DISPLAYCHUNK_LITERAL:
        print_length = cprogress_writeliteral(ptr, avail_length, displaychunk->literal, display_width);
        break;
      case CPROGRESS_DISPLAYCHUNK_TITLE:
        print_length = cprogress_writeliteral(ptr, avail_length, title, display_width);
        break;
      case CPROGRESS_DISPLAYCHUNK_BAR:
        print_length = cprogress_writeprogressbar(ptr, display_width, displaychunk->fill_char, percentage);
        break;
      case CPROGRESS_DISPLAYCHUNK_PERCENTAGE:
        print_length = cprogress_writeliteral(ptr, avail_length, percentage_string, display_width);
        break;
    }

    ptr += print_length;
    avail_length -= print_length;
  }
}


/*----------------------------------------------------------------------------
| view controller
----------------------------------------------------------------------------*/

int cprogress_getconsolewidth() {
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  return w.ws_col;
}

#define _cprogress_printline_widthtolength(width) (width * 4 + 1)

void cprogress_printline(cprogress_t *cprogress, const char *title, float percentage) {
  /* TODO move to instance */
  static int console_width = CPROGRESS_UNDEF;
  static int keep_consolewidth_loopcount = 0;

  static char *buf = NULL;
  static size_t buf_len = 0;

  if (console_width == CPROGRESS_UNDEF) {
    console_width = cprogress_getconsolewidth();
    buf = malloc(buf_len = _cprogress_printline_widthtolength(console_width));
    if (!buf) {} /* TODO well i have no idea neither */
  } else if (keep_consolewidth_loopcount >= CPROGRESS_CONSOLE_UPDATEWIDTH_LOOPCOUNT) {
    keep_consolewidth_loopcount = 0;
    int new_console_width = cprogress_getconsolewidth();
    if (new_console_width != console_width) {
      buf = realloc(buf, buf_len = _cprogress_printline_widthtolength(console_width));
      if (!buf) {} /* TODO same as above */
      console_width = new_console_width;
    }
  }

  /* actual draw */

  memset(buf, 0, buf_len);
  cprogress_writeline(cprogress, buf, buf_len, console_width, title, percentage);
  printf("%s", buf);
  fflush(stdout);

  /* misc */

  ++keep_consolewidth_loopcount;
}


void cprogress_abort(cprogress_t *cprogress) {
  if (!cprogress) return;

  cprogress->is_running = 0;
}

int cprogress_stillrunning(cprogress_t *cprogress) {
  if (!cprogress) return 0;

  int is_all_finished = 1;
  cprogress_threadinfo_foreach(cprogress, threadinfo) {
    if (threadinfo->is_running || threadinfo->is_just_stopped) {
      is_all_finished = 0;
      break;
    }
  }

  if (is_all_finished) cprogress_abort(cprogress);

  if (!cprogress->is_running) cprogress_emitevent(cprogress, CPROGRESS_EVENT_FINISH, CPROGRESS_UNDEF);

  return cprogress->is_running;
}

void cprogress_waitfps(int fps) {
  cprogress_msleep(1000 / fps);
}

/* only do clear and redraw in current line */
void cprogress_renderline(cprogress_t *cprogress, const char *title, float percentage) {
  if (!cprogress) return;

  printf("\x1b[1G\x1b[1K"); /* move to column 1, clear the entire line */
  cprogress_printline(cprogress, title, percentage);
}

void cprogress_render(cprogress_t *cprogress) {
  if (!cprogress) return;

  /* count how many threads are alive */
  int alive_thread_count = 0;
  cprogress_threadinfo_foreach(cprogress, threadinfo) {
    if (threadinfo->is_running)
      ++alive_thread_count;
  }

  /* move to head for redraw */
  if (cprogress->last_alive_thread_count)
    printf("\x1b[%dA", cprogress->last_alive_thread_count);

  cprogress_threadinfo_foreach(cprogress, threadinfo) {
    if (threadinfo->is_just_stopped) {
      threadinfo->is_just_stopped = 0;
      cprogress_renderline(cprogress, threadinfo->title, threadinfo->percentage);
      puts(""); /* move to next line */
      /* TODO move to cprogress_stillrunning(...) */
      cprogress_emitevent(cprogress, CPROGRESS_EVENT_THREADFINISH, cprogress_threadinfo_getindex(threadinfo));
    }
  }

  cprogress_threadinfo_foreach(cprogress, threadinfo) {
    if (threadinfo->is_running) {
      cprogress_renderline(cprogress, threadinfo->title, threadinfo->percentage);
      puts(""); /* move to next line */
    }
  }

  cprogress->last_alive_thread_count = alive_thread_count;
}

void cprogress_rendersum(cprogress_t *cprogress, const char *title) {
  if (!cprogress) return;

  int alive_thread_count = 0;
  float percentage = 0;
  cprogress_threadinfo_foreach(cprogress, threadinfo) {
    if (threadinfo->is_running) {
      percentage += threadinfo->percentage;
      ++alive_thread_count;
    }
  }
  percentage /= alive_thread_count;

  cprogress_renderline(cprogress, title, percentage);
}

void cprogress_render_tillcomplete(cprogress_t *cprogress, int fps) {
  if (!cprogress) return;

  while (cprogress_stillrunning(cprogress)) {
    cprogress_render(cprogress);
    cprogress_waitfps(fps);
  }
}


/*----------------------------------------------------------------------------
| data provider
----------------------------------------------------------------------------*/

void cprogress_threadinfo_updatetitle(cprogress_threadinfo_t *threadinfo, const char *title) {
  if (!threadinfo || !threadinfo->is_running) return;

  char *previous_title = threadinfo->title;
  if (previous_title) free(previous_title);

  threadinfo->title = cprogress_strdup(title);
}

void cprogress_threadinfo_updatepercentage(cprogress_threadinfo_t *threadinfo, float percentage) {
  if (!threadinfo || !threadinfo->is_running) return;

  if (percentage >= 100) {
    percentage = 100;
    cprogress_threadinfo_abort(threadinfo);
  }
  if (percentage < 0) percentage = 0;
  threadinfo->percentage = percentage;
}

void cprogress_updatethread_title(cprogress_t *cprogress, int thread_index, const char *title) {
  if (!cprogress || thread_index < 0 || thread_index >= cprogress->threadinfos_length) return;
  cprogress_threadinfo_updatetitle(&cprogress_getthreadinfo(cprogress, thread_index), title);
}

void cprogress_updatethread_percentage(cprogress_t *cprogress, int thread_index, float percentage) {
  if (!cprogress || thread_index < 0 || thread_index >= cprogress->threadinfos_length) return;
  cprogress_threadinfo_updatepercentage(&cprogress_getthreadinfo(cprogress, thread_index), percentage);
}


void cprogress_subscribeevent(cprogress_t *cprogress, cprogress_event_type_t type, cprogress_eventsubscriber_func_t *func) {
  if (!cprogress) return;

  if (type != CPROGRESS_EVENT_NONE && type < CPROGRESS_EVENT_LENGTH) {
    cprogress->subscribers[type] = func;
  }
}

void cprogress_emitevent(cprogress_t *cprogress, cprogress_event_type_t type, int thread_index) {
  if (!cprogress) return;

  if (type != CPROGRESS_EVENT_NONE && type < CPROGRESS_EVENT_LENGTH ||
    (thread_index > 0 && thread_index < cprogress->threadinfos_length || thread_index == CPROGRESS_UNDEF)) {
    cprogress_eventsubscriber_func_t *func = cprogress->subscribers[type];
    if (func) func(cprogress, type, thread_index);
  }
}



#endif /* !CPROGRESS_IMPL_ */
#endif /* CPROGRESS_IMPL */

