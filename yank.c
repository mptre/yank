#include <sys/ioctl.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/* Terminal capabilities */
#define T_CLR_EOS             "\033[J"
#define T_CURSOR_INVISIBLE    "\033[?25l"
#define T_CURSOR_VISIBLE      "\033[?25h"
#define T_ENTER_CA_MODE       "\033[?1049h"
#define T_ENTER_STANDOUT_MODE "\033[7m"
#define T_EXIT_CA_MODE        "\033[?1049l"
#define T_EXIT_STANDOUT_MODE  "\033[0m"
#define T_RESTORE_CURSOR      "\0338"
#define T_SAVE_CURSOR         "\0337"

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

enum {
	KEY_ENTER = 1,
	KEY_HOME,
	KEY_END,
	KEY_TERM,
	KEY_UP,
	KEY_RIGHT,
	KEY_DOWN,
	KEY_LEFT
};

struct field {
	size_t	so; /* start offset */
	size_t	eo; /* end offset */
	size_t	lo; /* line offset */
};

static regex_t reg;

static char **yankargv;

static struct {
	size_t	nmemb;
	size_t	size;
	struct	field *v;
} f;

static struct {
	size_t	size;
	size_t	nmemb;
	char	*v;
} in;

static struct {
	int		rfd;
	int		wfd;
	int		ca;	/* use alternate screen */
	struct termios	attr;
} tty;

static void
input(void)
{
	int	n;

	in.size = BUFSIZ;
	if ((in.v = malloc(in.size)) == NULL)
		err(1, NULL);

	while ((n = read(0, in.v + in.nmemb, in.size - in.nmemb)) != 0) {
		if (n == -1)
			err(1, "read");
		in.nmemb += n;

		if (in.nmemb < in.size)
			continue;
		in.size *= 2;
		if ((in.v = realloc(in.v, in.size)) == NULL)
			err(1, NULL);
	}
	memset(in.v + in.nmemb, 0, in.size - in.nmemb);
}

/*
 * Returns s transformed into a negation regular expression pattern concatenated
 * with the default delimiters.
 */
static char *
strtopat(const char *s)
{
	const char	*fmt = "[^%s\f\n\r\t]+";
	char		*pat;
	size_t		len;
	int		n;

	len = strlen(s) + strlen(fmt) + 1;
	if ((pat = malloc(len)) == NULL)
		err(1, NULL);
	n = snprintf(pat, len, fmt, s);
	if (n == -1 || n >= (ssize_t)len)
		errx(1, "pattern too long");

	return pat;
}

/*
 * Returns zero if f1 and f2 intersects, 1 if f2 begins after f1 and -1
 * otherwise. Both field start and end offsets are normalized with their
 * corresponding line offset.
 */
static int
fcmp(const struct field *f1, const struct field *f2)
{
	size_t	s1, s2, e1, e2;

	s1 = f1->so - f1->lo, e1 = f1->eo - f1->lo;
	s2 = f2->so - f2->lo, e2 = f2->eo - f2->lo;

	return MAX(s1, s2) <= MIN(e1, e2) ? 0 : (e1 < s2 ? 1 : -1);
}

static ssize_t
xwrite(int fd, const char *s, size_t nmemb)
{
	ssize_t	r;
	size_t	n;

	n = nmemb;
	do {
		r = write(fd, s, n);
		if (r == -1)
			return r;
		n -= r;
		s += r;
	} while (n);

	return nmemb;
}

static void
yank(const char *s, size_t nmemb)
{
	int	fd[2];
	int	status;
	pid_t	pid;

	if (!isatty(1)) {
		if (xwrite(1, s, nmemb) == -1)
			err(1, "write");
		exit(0);
	}

	if (pipe(fd) == -1)
		err(1, "pipe");
	if (dup2(fd[0], 0) == -1)
		err(1, "dup2");
	if (close(fd[0]) == -1)
		err(1, "close");
	if (xwrite(fd[1], s, nmemb) == -1)
		err(1, "write");
	if (close(fd[1]) == -1)
		err(1, "close");
	pid = fork();
	switch (pid) {
	case -1:
		err(1, "fork");
		/* NOTREACHED */
	case 0:
		execvp(yankargv[0], yankargv);
		err(126 + (errno == ENOENT), "%s", yankargv[0]);
		/* NOTREACHED */
	default:
		if (waitpid(pid, &status, 0) == -1)
			err(1, "waitpid");
		if (WIFSIGNALED(status))
			exit(128 + WTERMSIG(status));
		if (WIFEXITED(status))
			exit(WEXITSTATUS(status));
	}
}

static void
twrite(const char *s, size_t nmemb)
{
	if (xwrite(tty.wfd, s, nmemb) == -1)
		err(1, "write");
}

static void
tputs(const char *s)
{
	size_t	n;

	n = strlen(s);
	twrite(s, n);
}

static void
tsetup(void)
{
	struct termios	attr;
	struct winsize	ws;
	regmatch_t	r;
	char		*s, *e;
	size_t		m, n, w;
	unsigned int	i, j;

	if ((tty.rfd = open("/dev/tty", O_RDONLY)) == -1)
		err(1, "/dev/tty");
	if ((tty.wfd = open("/dev/tty", O_WRONLY)) == -1)
		err(1, "/dev/tty");

	if (ioctl(tty.rfd, TIOCGWINSZ, &ws) == -1)
		err(1, "TIOCGWINSZ");

	f.size = 32;
	if ((f.v = malloc(f.size*sizeof(struct field))) == NULL)
		err(1, NULL);
	m = n = MIN(ws.ws_col*ws.ws_row, (ssize_t)in.nmemb);
	s = e = in.v;
	while (m && !regexec(&reg, e, 1, &r, 0) && r.rm_eo - r.rm_so) {
		f.v[f.nmemb].so = f.v[f.nmemb].eo = e - s;
		f.v[f.nmemb].so += r.rm_so;
		f.v[f.nmemb].eo += MAX(MIN(r.rm_eo, (ssize_t)m) - 1, 0);
		e += r.rm_eo;
		m -= MIN(r.rm_eo, (ssize_t)m);

		if (++f.nmemb < f.size)
			continue;
		f.size *= 2;
		if ((f.v = realloc(f.v, f.size*sizeof(struct field))) == NULL)
			err(1, NULL);
	}

	for (i = j = 0, s = e = in.v; n && i < ws.ws_row; i++) {
		if (s == e && !(e = memchr(s + 1, '\n', n)))
			e = in.v + in.nmemb;

		w = MIN(e - s, ws.ws_col);
		for (; j < f.nmemb && f.v[j].so < (size_t)(s - in.v + w); j++)
			f.v[j].lo = s - in.v;
		s += w;
		n -= w;
	}
	f.nmemb = MIN(f.nmemb, j);
	/* Ensure last field does not exceed the terminal width. */
	if (n > 0 && f.nmemb > 0
	    && f.v[f.nmemb - 1].eo - f.v[f.nmemb - 1].lo >= ws.ws_col)
		f.v[f.nmemb - 1].eo = f.v[f.nmemb - 1].lo + ws.ws_col - 1;
	/* Number of bytes to output. */
	f.v[f.nmemb].lo = MAX(s - in.v - 1, 0);

	if (tcgetattr(tty.rfd, &tty.attr) == -1)
		err(1, "tcgetattr");
	attr = tty.attr;
	attr.c_lflag &= ~(ICANON|ECHO|ISIG);
	if (tcsetattr(tty.rfd, TCSANOW, &attr) == -1)
		err(1, "tcsetattr");

	if (tty.ca)
		tputs(T_ENTER_CA_MODE);
	tputs(T_CURSOR_INVISIBLE);
	/* Emit the number of lines and save the cursor position. */
	for (j = 0; j < i; j++)
		tputs("\n");
	for (j = 0; j < i; j++)
		tputs("\033M");
	tputs(T_SAVE_CURSOR);
}

static void
tend(void)
{
	tputs(T_RESTORE_CURSOR);
	tputs(T_CLR_EOS);
	tputs(T_CURSOR_VISIBLE);
	if (tty.ca)
		tputs(T_EXIT_CA_MODE);
	tcsetattr(tty.rfd, TCSANOW, &tty.attr);
	close(tty.rfd);
	close(tty.wfd);
}

static int
tgetc(void)
{
	static struct {
		const char	*s;
		int		c;
	}	keys[] = {
		{ "\n",		KEY_ENTER },
		{ "\001",	KEY_HOME },	/* Ctrl-A */
		{ "\003",	KEY_TERM },	/* Ctrl-C */
		{ "\004",	KEY_TERM },	/* Ctrl-D */
		{ "\005",	KEY_END },	/* Ctrl-E */
		{ "\016",	KEY_RIGHT },	/* Ctrl-N */
		{ "\020",	KEY_LEFT },	/* Ctrl-P */
		{ "G",		KEY_END },
		{ "g",		KEY_HOME },
		{ "h",		KEY_LEFT },
		{ "j",		KEY_DOWN },
		{ "k",		KEY_UP },
		{ "l",		KEY_RIGHT },
		{ "\033[A",	KEY_UP },
		{ "\033[C",	KEY_RIGHT },
		{ "\033[B",	KEY_DOWN },
		{ "\033[D",	KEY_LEFT },
		{ NULL,		0 },
	};
	char	buf[4];
	ssize_t	n;
	int	i;

	n = read(tty.rfd, buf, sizeof(buf) - 1);
	if (n == -1)
		err(1, "read");
	if (n == 0)
		return KEY_TERM;	/* EOF */
	buf[n] = '\0';

	for (i = 0; keys[i].s != NULL; i++)
		if (strncmp(keys[i].s, buf, strlen(keys[i].s)) == 0)
			return keys[i].c;

	return 0;
}

static const struct field *
tmain(void)
{
	size_t	n;
	int	c, i, j, k;

	i = j = 0;
	n = f.v[f.nmemb].lo;
	for (;;) {
		tputs(T_RESTORE_CURSOR);
		if (f.nmemb > 0) {
			twrite(in.v, f.v[i].so);
			tputs(T_ENTER_STANDOUT_MODE);
			twrite(in.v + f.v[i].so, f.v[i].eo - f.v[i].so + 1);
			tputs(T_EXIT_STANDOUT_MODE);
			twrite(in.v + f.v[i].eo + 1, n - f.v[i].eo);
		} else {
			twrite(in.v, n);
		}

		c = tgetc();
		switch (c) {
		case KEY_ENTER:
			if (f.nmemb > 0)
				return &f.v[i];
			break;
		case KEY_TERM:
			return NULL;
		case KEY_HOME:
			j = 0;
			break;
		case KEY_RIGHT:
			j = i + 1;
			break;
		case KEY_END:
			j = f.nmemb - 1;
			break;
		case KEY_LEFT:
			j = i - 1;
			break;
		case KEY_DOWN:
			j = i;
			while (j < (ssize_t)f.nmemb && f.v[i].lo == f.v[j].lo)
				j++;
			if (j == (ssize_t)f.nmemb)
				break;
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
		}
		if (j >= 0 && j < (ssize_t)f.nmemb)
			i = j;
	}
}

static void
usage(void)
{
	fprintf(stderr, "usage: yank [-lx | -v] [-d delim] [-g pattern [-i]] "
	    "[-- command [args]]\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	const struct field	*field;
	char			*pat;
	int			c, i, rflags = REG_EXTENDED;

	setlocale(LC_CTYPE, "");

#ifdef __OpenBSD__
	if (pledge("stdio tty proc exec", NULL) == -1)
		err(1, "pledge");
#endif

	pat = strtopat(" ");
	while ((c = getopt(argc, argv, "ilvxd:g:")) != -1)
		switch (c) {
		case 'd':
			free(pat);
			pat = strtopat(optarg);
			break;
		case 'g':
			free(pat);
			if ((pat = strdup(optarg)) == NULL)
				err(1, NULL);
			rflags |= REG_NEWLINE;
			break;
		case 'i':
			rflags |= REG_ICASE;
			break;
		case 'l':
			free(pat);
			pat = strtopat("");
			break;
		case 'v':
			puts("yank " VERSION);
			exit(0);
		case 'x':
			tty.ca = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (regcomp(&reg, pat, rflags) != 0)
		errx(1, "invalid regular expression");

	/* Ensure space for yank command and null terminator. */
	if ((yankargv = calloc(argc + 2, sizeof(char *))) == NULL)
		err(1, NULL);
	yankargv[0] = YANKCMD;
	for (i = 0; i < argc; i++)
		yankargv[i] = argv[i];

	input();
	tsetup();
	field = tmain();
	tend();
	if (field == NULL)
		return 1;
	yank(in.v + field->so, field->eo - field->so + 1);

	return 0;
}
