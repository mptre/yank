#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* Control characters */
#define T_CLR_EOS             "\033[J"
#define T_COLUMN_ADDRESS      "\033[%dG"
#define T_CURSOR_INVISIBLE    "\033[?25l"
#define T_CURSOR_UP           "\033[%dA"
#define T_CURSOR_VISIBLE      "\033[?25h"
#define T_ENTER_STANDOUT_MODE "\033[7m"
#define T_EXIT_STANDOUT_MODE  "\033[0m"

#define CONTROL(c) (c ^ 0x40)

static const char *delim = " ";

static const char **yankargv;

static struct {
	size_t size;
	size_t nmemb;
	size_t pmemb;  /* number of bytes that fits on screen */
	size_t nlines;
	char *v;
} in;

static struct {
	size_t nmemb;
	const char *v;
} sel;

static struct {
	int in;
	int out;
	unsigned int height;
	unsigned int width;
	struct termios attr;
} tty;

static void args(int, const char **);
static int field(const char *, int, size_t *, size_t *);
static void input(void);
static int isdelim(const char *);
static ssize_t rune(const char *s, size_t, int);
static void yank(void);

static void tend(void);
static void tdraw(const char *, size_t, int, int);
static void tmain(void);
static void tprintf(const char *, ...);
static void tsetup(void);
static void twrite(const char *, size_t);

void
args(int argc, const char **argv)
{
	size_t n;
	int c, i;

	/* Ensure space for null terminator. */
	n = (argc + 1)*sizeof(const char *);
	yankargv = malloc(n);
	if (!yankargv)
		perror("malloc");
	memset(yankargv, 0, n);
	yankargv[0] = YANKCMD;

	while ((c = getopt(argc, (char * const *) argv, "vd:")) != -1) {
		switch (c) {
		case 'd':
			delim = optarg;
			break;
		case 'v':
			puts("yank " VERSION);
			exit(0);
		default:
			fputs("usage: yank "
			      "[-v] "
			      "[-d delim] "
			      "[-- command [argument ...]]\n", stderr);
			exit(1);
		}
	}

	for (i = optind; i < argc; i++)
		yankargv[i - optind] = argv[i];
}

/*
 * Returns the next unicode rune offset relative to s + offset in the direction
 * given by inc.
 */
static ssize_t
rune(const char *s, size_t offset, int inc)
{
	ssize_t i;

	for (i = offset + inc;
	     i + inc >= 0 && s[i] && (s[i] & 0xC0) == 0X80;
	     i += inc)
		/* NOP */;

	return i - offset;
}

int
field(const char *s, int inc, size_t *start, size_t *stop)
{
	ssize_t i, j, r;

	r = 0;
	i = *start;
	for (;;) {
		if (i < 0 || !s[i])
			return 0;

		if (!isdelim(&s[i]))
			break;
		r = rune(s, i, inc);
		i += r;
	}
	i -= r < -1 ? r - inc : 0;

	j = i;
	for (;;) {
		r = rune(s, j, inc);
		j += r;
		if (j < 0 || !s[j] || isdelim(&s[j]))
			break;
	}
	j -= r < -1 ? r : inc;

	*start = i;
	*stop  = j;
	return 1;
}

void
input(void)
{
	FILE *f = stdin;

	in.size = BUFSIZ;
	in.nmemb = 0;
	in.v = malloc(in.size);
	if (!in.v)
		perror("malloc");

	for (;;) {
		in.nmemb += fread(in.v + in.nmemb, 1, in.size - in.nmemb, f);
		if (in.size < in.nmemb + 1) {
			in.size *= 2;
			in.v = realloc(in.v, in.size);
			if (!in.v)
				perror("realloc");
		}

		if (feof(f))
			break;
		if (ferror(f)) {
			perror("fread");
			break;
		}
	}
	memset(in.v + in.nmemb, 0, in.size - in.nmemb);
}

int
isdelim(const char *s)
{
	size_t i, n;

	if (*s == '\n' || *s == '\t')
		return 1;

	n = rune(s, 0, 1);
	for (i = 0; delim[i]; i += rune(delim, i, 1)) {
		if (!strncmp(s, &delim[i], n))
			return 1;
	}
	return 0;
}

void
yank(void)
{
	int fd[2];
	pid_t pid;

	if (!sel.v)
		exit(1);

	if (!isatty(1)) {
		if (write(1, sel.v, sel.nmemb) < 0)
			perror("write");
		exit(0);
	}

	if (pipe(fd) < 0)
		perror("pipe");
	if (dup2(fd[0], 0) < 0)
		perror("dup2");
	if (close(fd[0]) < 0)
		perror("close");
	if (write(fd[1], sel.v, sel.nmemb) < 0)
		perror("write");
	if (close(fd[1]) < 0)
	    perror("close");
	pid = fork();
	switch (pid) {
	case -1:
		perror("fork");
		exit(1);
	case 0:
		execvp(yankargv[0], (char * const *) yankargv);
		perror(yankargv[0]);
		exit(1);
	default:
		waitpid(pid, NULL, 0);
	}
}

void
tdraw(const char *s, size_t nmemb, int start, int stop)
{
	twrite(s, start);
	tprintf(T_ENTER_STANDOUT_MODE);
	twrite(s + start, stop - start + 1);
	tprintf(T_EXIT_STANDOUT_MODE);
	twrite(s + stop + 1, nmemb - stop);
}

void
tprintf(const char *format, ...)
{
	char s[32];
	va_list args;
	int n;

	va_start(args, format);
	n = vsnprintf(s, sizeof(s), format, args);
	va_end(args);

	twrite(s, n);
}

void
twrite(const char *s, size_t nmemb)
{
	if (write(tty.out, s, nmemb) < 0)
		perror("write");
}

void
tsetup(void)
{
	struct termios attr;
	struct winsize ws;
	char *s1, *s2;
	size_t d;

	tty.in = open("/dev/tty", O_RDONLY);
	if (!tty.in)
		perror("open");
	if (ioctl(tty.in, TIOCGWINSZ, &ws) < 0) {
		perror("ioctl");
		tty.height = 24;
		tty.width = 80;
	} else {
		tty.height = ws.ws_row;
		tty.width = ws.ws_col;
	}

	s1 = in.v;
	for (;;) {
		s2 = strchr(s1, '\n');
		if (!s2) {
			in.pmemb += (in.v + in.nmemb) - s1;
			break;
		}

		d = s2 - s1;
		if (d && !(d % tty.width))
			/* Invariant: the line length is divisble by the
			 * terminal width. */
			in.nlines += d/tty.width;
		else
			in.nlines += d/tty.width + 1;

		s2++;
		in.pmemb += s2 - s1;
		s1 = s2;

		if (in.nlines == tty.height) {
			in.pmemb--;
			break;
		}
	}
	memset(in.v + in.pmemb, 0, in.nmemb - in.pmemb);

	tcgetattr(tty.in, &tty.attr);
	memcpy(&attr, &tty.attr, sizeof(struct termios));
	attr.c_lflag &= ~(ICANON|ECHO|ISIG);
	tcsetattr(tty.in, TCSANOW, &attr);

	tty.out = open("/dev/tty", O_WRONLY);
	if (!tty.out)
		perror("open");

	tprintf(T_CURSOR_INVISIBLE);
}

void
tend(void)
{
	if (in.nlines)
		tprintf(T_CURSOR_UP, in.nlines);
	tprintf(T_COLUMN_ADDRESS, 1);
	tprintf(T_CLR_EOS);
	tprintf(T_CURSOR_VISIBLE);
	tcsetattr(tty.in, TCSANOW, &tty.attr);
	close(tty.in);
	close(tty.out);
}

void
tmain(void)
{
	struct { size_t start; size_t stop; } f, reset;
	int c;

	f.start = f.stop = 0;
	if (field(in.v, 1, &f.start, &f.stop))
		tdraw(in.v, in.pmemb, f.start, f.stop);
	else
		twrite(in.v, in.pmemb);
	for (;;) {
		reset = f;
		if (read(tty.in, &c, 1) < 0)
			perror("read");
		switch (c & 0xFF) {
		case '\n':
			sel.nmemb = f.stop - f.start + 1;
			sel.v = in.v + f.start;
			/* FALLTHROUGH */
		case CONTROL('C'):
			return;
		case CONTROL('A'):
			f.start = 0;
		if (0) {
		case CONTROL('N'):
			f.start = f.stop + rune(in.v, f.stop, 1);
		}
			if (field(in.v, 1, &f.start, &f.stop))
				break;
			f = reset;
			continue;
		case CONTROL('E'):
			f.stop = in.pmemb - 1;
		if (0) {
		case CONTROL('P'):
			f.stop = f.start + rune(in.v, f.start, -1);
		}
			if (field(in.v, -1, &f.stop, &f.start))
				break;
			f = reset;
			continue;
		default:
			continue;
		}
		if (in.nlines)
			tprintf(T_CURSOR_UP, in.nlines);
		tprintf(T_COLUMN_ADDRESS, 1);
		tdraw(in.v, in.pmemb, f.start, f.stop);
	}
}

int
main(int argc, const char *argv[])
{
	args(argc, argv);
	input();
	tsetup();
	tmain();
	tend();
	yank();
	return 0;
}
