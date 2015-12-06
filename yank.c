#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define KEY_DOWN  0x100
#define KEY_LEFT  0x101
#define KEY_RIGHT 0x102
#define KEY_UP    0x103

/* Terminal capabilities */
#define T_CLR_EOS             "\033[J"
#define T_CURSOR_INVISIBLE    "\033[?25l"
#define T_CURSOR_VISIBLE      "\033[?25h"
#define T_ENTER_CA_MODE       "\033[?1049h"
#define T_ENTER_STANDOUT_MODE "\033[7m"
#define T_EXIT_CA_MODE        "\033[?1049l"
#define T_EXIT_STANDOUT_MODE  "\033[0m"
#define T_KEY_DOWN            "\033[B"
#define T_KEY_LEFT            "\033[D"
#define T_KEY_RIGHT           "\033[C"
#define T_KEY_UP              "\033[A"
#define T_RESTORE_CURSOR      "\033[u"
#define T_SAVE_CURSOR         "\033[s"

#define CONTROL(c) (c ^ 0x40)
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

static regex_t pattern;

static const char **yankargv;

struct field {
	size_t so; /* start offset */
	size_t eo; /* end offset */
	size_t lo; /* line offset */
};

static struct {
	size_t nmemb;
	size_t size;
	struct field *v;
} f;

static struct {
	size_t size;
	size_t nmemb;
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
	struct termios attr;
} tty;

static void args(int, const char **);
static char *ator(const char *s);
static int fcmp(struct field *, struct field *);
static void input(void);
static void yank(void);

static void tdraw(const char *, size_t, size_t, size_t);
static void tend(void);
static int tgetc(void);
static void tmain(void);
static void tputs(const char *);
static void tsetup(void);
static void twrite(const char *, size_t);

static ssize_t xwrite(int, const char *, size_t);

void
args(int argc, const char **argv)
{
	char *s;
	int c, i;
	int f = REG_EXTENDED;

	s = ator(" ");
	while ((c = getopt(argc, (char * const *) argv, "ilvxd:g:")) != -1) {
		switch (c) {
		case 'd':
			free(s);
			s = ator(optarg);
			break;
		case 'g':
			free(s);
			s = optarg;
			f |= REG_NEWLINE;
			break;
		case 'i':
			f |= REG_ICASE;
			break;
		case 'l':
			free(s);
			s = ator("");
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
			      "[-g pattern [-i]] "
			      "[-- command [argument ...]]\n", stderr);
			exit(1);
		}
	}
	if (optind < argc && strncmp(argv[optind - 1] , "--", 3))
		goto usage;

	if (regcomp(&pattern, s, f)) {
		fputs("yank: invalid regular expression\n", stderr);
		exit(1);
	}

	/* Ensure space for yank command and null terminator. */
	yankargv = calloc(argc - optind + 2, sizeof(const char *));
	if (!yankargv)
		perror("calloc");
	yankargv[0] = YANKCMD;
	for (i = optind; i < argc; i++)
		yankargv[i - optind] = argv[i];
}

void
input(void)
{
	int n;

	in.size = BUFSIZ;
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

/*
 * Returns s transformed into a negation regular expression concatenated with
 * the default delimiters.
 */
char *
ator(const char *s)
{
	const char *f = "[^%s\f\n\r\t]+";
	char *r;
	size_t n;

	n = strlen(s) + strlen(f) + 1;
	r = malloc(n);
	if (!r)
		perror("malloc");
	if (snprintf(r, n, f, s) < 0)
		perror("snprintf");

	return r;
}

/*
 * Returns zero if f1 and f2 intersects, 1 if f2 begins after f1 and -1
 * otherwise. Both field start and end offsets are normalized with their
 * corresponding line offset.
 */
int
fcmp(struct field *f1, struct field *f2)
{
	size_t s1, s2, e1, e2;

	s1 = f1->so - f1->lo, e1 = f1->eo - f1->lo;
	s2 = f2->so - f2->lo, e2 = f2->eo - f2->lo;

	return MAX(s1, s2) <= MIN(e1, e2) ? 0 : (e1 < s2 ? 1 : -1);
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
tdraw(const char *s, size_t nmemb, size_t start, size_t stop)
{
	twrite(s, start);
	tputs(T_ENTER_STANDOUT_MODE);
	twrite(s + start, stop - start + 1);
	tputs(T_EXIT_STANDOUT_MODE);
	twrite(s + stop + 1, nmemb - stop);
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
	regmatch_t r;
	char *s, *e;
	size_t m, n, w;
	unsigned int i, j;

	tty.in = open("/dev/tty", O_RDONLY);
	if (!tty.in)
		perror("open");

	ws.ws_col = 80, ws.ws_row = 24;
	if (ioctl(tty.in, TIOCGWINSZ, &ws) < 0)
		perror("ioctl");

	f.size = 32;
	f.v = malloc(f.size*sizeof(struct field));
	if (!f.v)
		perror("malloc");
	m = n = MIN(ws.ws_col*ws.ws_row, (ssize_t) in.nmemb);
	s = e = in.v;
	while (m && !regexec(&pattern, e, 1, &r, 0)) {
		f.v[f.nmemb].so = f.v[f.nmemb].eo = e - s;
		f.v[f.nmemb].so += r.rm_so;
		f.v[f.nmemb].eo += MAX(MIN(r.rm_eo, (ssize_t) m) - 1, 0);
		e += r.rm_eo;
		m -= MIN(r.rm_eo, (ssize_t) m);

		if (++f.nmemb < f.size)
			continue;
		f.size *= 2;
		f.v = realloc(f.v, f.size*sizeof(struct field));
		if (!f.v)
			perror("realloc");
	}

	for (i = j = 0, s = e = in.v; n && i < ws.ws_row; i++) {
		if (s == e) {
			e = memchr(s, '\n', n);
			if (e) {
				if (ws.ws_row - i > 1)
					e++;
			} else {
				e = in.v + in.nmemb;
			}
		}

		w = MIN(e - s, ws.ws_col);
		for (; j < f.nmemb && f.v[j].so < (size_t) (s - in.v + w); j++)
			f.v[j].lo = s - in.v;
		s += w;
		n -= w;
	}
	f.nmemb = MIN(f.nmemb, j);
	/* Ensure last field does not exceed the terminal width. */
	if (n && f.v[f.nmemb - 1].eo - f.v[f.nmemb - 1].lo >= ws.ws_col)
		f.v[f.nmemb - 1].eo = f.v[f.nmemb - 1].lo + ws.ws_col - 1;
	/* Number of bytes to output. */
	f.v[f.nmemb].lo = MAX(s - in.v - 1, 0);

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
	/* Emit the number of lines and save the cursor position. */
	for (j = 0; j < i; j++)
		tputs("\n");
	for (j = 0; j < i; j++)
		tputs(T_KEY_UP);
	tputs(T_SAVE_CURSOR);
}

void
tend(void)
{
	tputs(T_RESTORE_CURSOR);
	tputs(T_CLR_EOS);
	tputs(T_CURSOR_VISIBLE);
	if (tty.ca)
		tputs(T_EXIT_CA_MODE);
	tcsetattr(tty.in, TCSANOW, &tty.attr);
	close(tty.in);
	close(tty.out);
}

int
tgetc(void)
{
	char buf[3];
	ssize_t n;

	n = read(tty.in, buf, sizeof(buf));
	if (n < 0) {
		perror("read");
		return 0;
	}
	if (n > 2) {
		if (!strncmp(T_KEY_UP, buf, n))
			return KEY_UP;
		if (!strncmp(T_KEY_RIGHT, buf, n))
			return KEY_RIGHT;
		if (!strncmp(T_KEY_DOWN, buf, n))
			return KEY_DOWN;
		if (!strncmp(T_KEY_LEFT, buf, n))
			return KEY_LEFT;
		return 0;
	}

	return buf[0];
}

void
tmain(void)
{
	int c, i, j, k;
	size_t n;

	i = j = 0;
	n = f.v[f.nmemb].lo;
	if (f.nmemb)
		tdraw(in.v, n, f.v[0].so, f.v[0].eo);
	else
		twrite(in.v, n);
	for (;;) {
		c = tgetc();
		switch (c) {
		case '\n':
			sel.nmemb = f.v[i].eo - f.v[i].so + 1;
			sel.v = in.v + f.v[i].so;
			/* FALLTHROUGH */
		case CONTROL('C'):
		case CONTROL('D'):
			return;
		case CONTROL('A'):
			j = 0;
			break;
		case CONTROL('N'):
		case KEY_RIGHT:
			j = i + 1;
			break;
		case CONTROL('E'):
			j = f.nmemb - 1;
			break;
		case CONTROL('P'):
		case KEY_LEFT:
			j = i - 1;
			break;
		case KEY_DOWN:
			j = i;
			while (j < (ssize_t) f.nmemb && f.v[i].lo == f.v[j].lo)
				j++;
			if (j == (ssize_t) f.nmemb)
				continue;
			/* FALLTHROUGH */
		if (0) {
		case KEY_UP:
			k = i;
			while (k && f.v[i].lo == f.v[k].lo)
				k--;
			j = k;
			while (j && f.v[j - 1].lo == f.v[k].lo)
				j--;
		}
			for (; fcmp(&f.v[i], &f.v[j]) < 0
			     && f.v[j].lo == f.v[j + 1].lo; j++)
				/* NOP */;
			break;
		default:
			continue;
		}
		if (j < 0 || j >= (ssize_t) f.nmemb)
			continue;
		i = j;
		tputs(T_RESTORE_CURSOR);
		tdraw(in.v, n, f.v[i].so, f.v[i].eo);
	}
}

int
main(int argc, const char *argv[])
{
	setlocale(LC_ALL, "");
	args(argc, argv);
	input();
	tsetup();
	tmain();
	tend();
	yank();
	return 0;
}
