#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* Terminal capabilities */
#define T_CLR_EOS             "\033[J"
#define T_COLUMN_ADDRESS      "\033[%dG"
#define T_CURSOR_INVISIBLE    "\033[?25l"
#define T_CURSOR_UP           "\033[%dA"
#define T_CURSOR_VISIBLE      "\033[?25h"
#define T_ENTER_CA_MODE       "\033[?1049h"
#define T_ENTER_STANDOUT_MODE "\033[7m"
#define T_EXIT_CA_MODE        "\033[?1049l"
#define T_EXIT_STANDOUT_MODE  "\033[0m"

#define ESC 27
#define ARROW_PREFIX 91
#define LEFT_ARROW 68
#define RIGHT_ARROW 67

#define CONTROL(c) (c ^ 0x40)
#define MAX(x, y) (x > y ? x : y)
#define MIN(x, y) (x < y ? x : y)

#define NEXT CONTROL('N')
#define PREV CONTROL('P')

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
	int ca;              /* use alternate screen */
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
static void tprintf(const char *, int);
static void tputs(const char *);
static void tsetup(void);
static void twrite(const char *, size_t);

static ssize_t xwrite(int, const char *, size_t);

void
args(int argc, const char **argv)
{
	int c, i;

	while ((c = getopt(argc, (char * const *) argv, "lvxd:")) != -1) {
		switch (c) {
		case 'd':
			delim = optarg;
			break;
		case 'l':
			delim = "";
			break;
		case 'v':
			puts("yank " VERSION);
			exit(0);
		case 'x':
			tty.ca = 1;
			break;
		default:
		usage:
			fputs("usage: yank "
			      "[-lx | -v] "
			      "[-d delim] "
			      "[-- command [argument ...]]\n", stderr);
			exit(1);
		}
	}
	if (optind < argc && strncmp(argv[optind - 1] , "--", 3))
		goto usage;

	/* Ensure space for yank command and null terminator. */
	yankargv = calloc(argc - optind + 2, sizeof(const char *));
	if (!yankargv)
		perror("calloc");
	yankargv[0] = YANKCMD;
	for (i = optind; i < argc; i++)
		yankargv[i - optind] = argv[i];
}

/*
 * Returns the next unicode rune offset relative to s + offset in the direction
 * given by inc.
 */
ssize_t
rune(const char *s, size_t offset, int inc)
{
	ssize_t i;

	for (i = offset + inc;
	     i + inc >= 0 && s[i] && (s[i] & 0xC0) == 0x80;
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
	int n;

	in.size = BUFSIZ;
	in.nmemb = 0;
	in.v = malloc(in.size);
	if (!in.v)
		perror("malloc");

	for (;;) {
		n = read(0, in.v + in.nmemb, in.size - in.nmemb);
		if (n < 0) {
			perror("read");
			break;
		}
		if (!n)
			break;
		in.nmemb += n;

		if (in.size < in.nmemb + 1) {
			in.size *= 2;
			in.v = realloc(in.v, in.size);
			if (!in.v)
				perror("realloc");
		}
	}
	memset(in.v + in.nmemb, 0, in.size - in.nmemb);
}

int
isdelim(const char *s)
{
	size_t i, n;

	if (*s == '\n' || *s == '\r' || *s == '\t')
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
	int s;
	pid_t pid;

	if (!sel.v)
		exit(1);

	if (!isatty(1)) {
		if (xwrite(1, sel.v, sel.nmemb) < 0)
			perror("write");
		exit(0);
	}

	if (pipe(fd) < 0)
		perror("pipe");
	if (dup2(fd[0], 0) < 0)
		perror("dup2");
	if (close(fd[0]) < 0)
		perror("close");
	if (xwrite(fd[1], sel.v, sel.nmemb) < 0)
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
		s = errno;
		perror(yankargv[0]);
		_exit(126 + (s == ENOENT));
	default:
		waitpid(pid, &s, 0);
		if (WIFSIGNALED(s))
			exit(128 + WTERMSIG(s));
		if (WIFEXITED(s))
			exit(WEXITSTATUS(s));
	}
}

ssize_t
xwrite(int fd, const char *s, size_t nmemb)
{
	ssize_t r;
	size_t n = nmemb;

	do {
		r = write(fd, s, n);
		if (r < 0)
			return r;
		n -= r;
		s += r;
	} while (n);

	return nmemb;
}

void
tdraw(const char *s, size_t nmemb, int start, int stop)
{
	twrite(s, start);
	tputs(T_ENTER_STANDOUT_MODE);
	twrite(s + start, stop - start + 1);
	tputs(T_EXIT_STANDOUT_MODE);
	twrite(s + stop + 1, nmemb - stop);
}

void
tprintf(const char *format, int x)
{
	char s[32];
	int n;

	n = snprintf(s, sizeof(s), format, x);

	twrite(s, n);
}

void
tputs(const char *s)
{
	size_t n = strlen(s);

	twrite(s, n);
}

void
twrite(const char *s, size_t nmemb)
{
	if (xwrite(tty.out, s, nmemb) < 0)
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
	while (in.pmemb < in.nmemb && in.nlines < tty.height) {
		s2 = strchr(s1, '\n');
		if (s2) {
			d = MAX(s2 - s1, 1);
			if (in.nlines < tty.height - 1)
				s2++;
		} else {
			d = MIN((size_t) ((in.v + in.nmemb) - s1),
				(tty.height - in.nlines)*tty.width);
			s2 = in.v + in.pmemb + d;
			if (d < tty.width)
				/* Invariant: the last line does not contain a
				 * trailing new line and is shorter than the
				 * terminal width. Therefor compensate for the
				 * nlines increment below due to ceiling. */
				in.nlines--;
		}
		in.nlines += d/tty.width + (d % tty.width > 0); /* ceil */
		in.pmemb += s2 - s1;
		s1 = s2;
	}
	memset(in.v + in.pmemb, 0, in.nmemb - in.pmemb);

	tcgetattr(tty.in, &tty.attr);
	memcpy(&attr, &tty.attr, sizeof(struct termios));
	attr.c_lflag &= ~(ICANON|ECHO|ISIG);
	tcsetattr(tty.in, TCSANOW, &attr);

	tty.out = open("/dev/tty", O_WRONLY);
	if (!tty.out)
		perror("open");

	if (tty.ca)
		tputs(T_ENTER_CA_MODE);
	tputs(T_CURSOR_INVISIBLE);
}

void
tend(void)
{
	if (in.nlines)
		tprintf(T_CURSOR_UP, in.nlines);
	tprintf(T_COLUMN_ADDRESS, 1);
	tputs(T_CLR_EOS);
	tputs(T_CURSOR_VISIBLE);
	if (tty.ca)
		tputs(T_EXIT_CA_MODE);
	tcsetattr(tty.in, TCSANOW, &tty.attr);
	close(tty.in);
	close(tty.out);
}

/* Transfroms the escape sequence into actions' default keycode. */
void
trans_esc_seq(char *out) {
	char esc_seq[2];
	if (read(tty.in, esc_seq, 2) < 0) {
		perror("read");
		return;
	}
			
	if (esc_seq[0] != ARROW_PREFIX)  {
		return;
	}

	switch(esc_seq[1]) {
		case LEFT_ARROW:
			*out = PREV;
			break;
		case RIGHT_ARROW:
			*out = NEXT;
			break;
	}
}

void
tmain(void)
{
	size_t start, stop, t;
	char c;
	start = stop = 0;
	if (field(in.v, 1, &start, &stop))
		tdraw(in.v, in.pmemb, start, stop);
	else
		twrite(in.v, in.pmemb);
	for (;;) {
		if (read(tty.in, &c, 1) < 0)
			perror("read");

		if (c == ESC)
			trans_esc_seq(&c);

		switch (c) {
		case '\n':
			sel.nmemb = stop - start + 1;
			sel.v = in.v + start;
			/* FALLTHROUGH */
		case CONTROL('C'):
		case CONTROL('D'):
			return;
		case CONTROL('A'):
			t = 0;
			/* FALLTHROUGH */
		if (0) {
		case NEXT:
			t = stop + rune(in.v, stop, 1);
		}
			if (!field(in.v, 1, &t, &stop))
				continue;
			start = t;
			break;
		case CONTROL('E'):
			t = in.pmemb - 1;
			/* FALLTHROUGH */
		if (0) {
		case PREV:
			t = start + rune(in.v, start, -1);
		}
			if (!field(in.v, -1, &t, &start))
				continue;
			stop = t;
			break;
		default:
			continue;
		}
		if (in.nlines)
			tprintf(T_CURSOR_UP, in.nlines);
		tprintf(T_COLUMN_ADDRESS, 1);
		tdraw(in.v, in.pmemb, start, stop);
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
