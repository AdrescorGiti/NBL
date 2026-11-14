/* Language launchers for the integrated terminal. GPL-3.0-or-later. */
#include "prototypes.h"
#include "nbl.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static char *build_dir, *executable;
static FILE *compiler_log;
static int build_state[2] = {-1, -1};
static openfilestruct *owner;
static unsigned long revision;

static void cleanup(void)
{
	for (int i = 0; i < 2; i++) {
		if (build_state[i] >= 0)
			close(build_state[i]);
		build_state[i] = -1;
	}
	if (compiler_log)
		fclose(compiler_log);
	if (executable)
		unlink(executable);
	if (build_dir)
		rmdir(build_dir);
	free(executable);
	free(build_dir);
	compiler_log = NULL;
	executable = build_dir = NULL;
	owner = NULL;
}

void nbl_run_forget(openfilestruct *buffer)
{
	if (buffer == owner)
		owner = NULL;
}

static const char *compiler(enum nbl_language language)
{
	const char *setting = nbl_tool_command(language);
	return *setting ? setting : language == NBL_CPP ? "g++" : "gcc";
}

static void compile(enum nbl_language language)
{
	/* The compiler runs in the source directory and writes only to a private
	 * build directory. The input descriptor refers to the edited snapshot. */
	pid_t job = fork();
	if (job < 0) {
		dprintf(STDERR_FILENO, "NBL: %s\n", strerror(errno));
		_exit(126);
	}
	if (!job) {
		struct rlimit limit = { 16 * 1024 * 1024, 16 * 1024 * 1024 };
		if (dup2(fileno(compiler_log), STDERR_FILENO) < 0)
			_exit(126);
		close(build_state[0]);
		setrlimit(RLIMIT_FSIZE, &limit);
		setenv("LC_ALL", "C", 1);
		const char *tool = compiler(language);
		execlp(tool, tool, "-x", language == NBL_CPP ? "c++" : "c",
				"-fdiagnostics-color=never", "-iquote", ".", "/proc/self/fd/3", "-o", executable, (char *)NULL);
		dprintf(STDERR_FILENO, "NBL: %s: %s\n", tool, strerror(errno));
		_exit(127);
	}

	int code;
	pid_t finished;
	do {
		finished = waitpid(job, &code, 0);
	} while (finished < 0 && errno == EINTR);
	if (lseek(fileno(compiler_log), 0, SEEK_SET) >= 0) {
		char bytes[4096];
		ssize_t got;
		size_t sent = 0;
		while (sent < 65536 && (got = read(fileno(compiler_log), bytes, sizeof(bytes))) > 0) {
			if (fwrite(bytes, 1, got, stderr) != (size_t)got)
				break;
			sent += got;
		}
	}
	if (finished < 0 || !WIFEXITED(code) || WEXITSTATUS(code) != 0)
		_exit(finished > 0 && WIFEXITED(code) ? WEXITSTATUS(code) : 1);

	char built = 1;
	IGNORE_CALL_RESULT(write(build_state[1], &built, 1));
	close(build_state[1]);
	close(3);
	execl(executable, executable, (char *)NULL);
	dprintf(STDERR_FILENO, "NBL: %s\n", strerror(errno));
	_exit(126);
}

void nbl_exec_buffer(enum nbl_language language, const char *name)
{
	if (language == NBL_C || language == NBL_CPP)
		compile(language);
	else if (language == NBL_PYTHON) {
		/* Read source separately from stdin so input() still uses the PTY. */
		const char *bootstrap =
			"import os, sys\n"
			"sys.argv.pop(0)\n"
			"filename = sys.argv[0]\n"
			"sys.path[0] = os.path.dirname(os.path.abspath(filename))\n"
			"with os.fdopen(3, 'rb') as source:\n"
			"    code = compile(source.read(), filename, 'exec')\n"
			"exec(code, {'__name__': '__main__', '__file__': filename})\n";
		const char *tool = nbl_tool_command(language);
		if (*tool)
			execlp(tool, tool, "-u", "-c", bootstrap, name, (char *)NULL);
		else {
			execlp("python3", "python3", "-u", "-c", bootstrap, name, (char *)NULL);
			if (errno == ENOENT)
				execlp("python", "python", "-u", "-c", bootstrap, name, (char *)NULL);
			tool = "python3";
		}
		dprintf(STDERR_FILENO, "NBL: %s: %s\n", tool, strerror(errno));
	} else {
		if (nbl_is_bash())
			execlp("bash", "bash", "--noprofile", "--norc", "-c", ". /proc/self/fd/3", name, (char *)NULL);
		else
			execl("/bin/sh", "sh", "-c", ". /proc/self/fd/3", name, (char *)NULL);
		dprintf(STDERR_FILENO, "NBL: sh: %s\n", strerror(errno));
	}
	_exit(127);
}

bool nbl_start_buffer(enum nbl_language language, FILE *snapshot, const char *directory,
		const char *name)
{
	if (language == NBL_C || language == NBL_CPP) {
		const char *tmp = getenv("TMPDIR");
		build_dir = concatenate(tmp && *tmp == '/' ? tmp : "/tmp", "/nbl-build-XXXXXX");
		if (!mkdtemp(build_dir)) {
			statusline(ALERT, _("Cannot create build directory: %s"), strerror(errno));
			free(build_dir);
			build_dir = NULL;
			return FALSE;
		}
		executable = concatenate(build_dir, "/program");
		compiler_log = tmpfile();
		if (!compiler_log || pipe2(build_state, O_CLOEXEC | O_NONBLOCK) < 0) {
			statusline(ALERT, _("Cannot capture compiler output: %s"), strerror(errno));
			cleanup();
			return FALSE;
		}
		fcntl(fileno(compiler_log), F_SETFD, FD_CLOEXEC);
		owner = openfile;
		revision = owner->nbl_revision;
		nbl_clear_diagnostics();
	}
	if (nbl_terminal_start(language, snapshot, directory, name)) {
		if (build_state[1] >= 0) {
			close(build_state[1]);
			build_state[1] = -1;
		}
		return TRUE;
	}
	cleanup();
	return FALSE;
}

void nbl_run_finished(bool failed)
{
	if (!build_dir)
		return;
	char built;
	bool build_failed = failed && read(build_state[0], &built, 1) != 1;
	if (owner && owner->nbl_revision == revision) {
		openfilestruct *visible = openfile;
		openfile = owner;
		nbl_compiler_problems(compiler_log, build_failed);
		openfile = visible;
		if (owner == visible && nbl_problem_count() && build_failed)
			nbl_tools_show_problems();
	}
	cleanup();
}

void nbl_run_cleanup(void)
{
	cleanup();
}
