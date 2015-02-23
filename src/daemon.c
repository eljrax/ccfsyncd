#include "ccfsync.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_PID_LEN 10

int
delete_local_file (char *file)
{
  if (unlink (file) < 0) {
    log_msg (LOG_ERR, "Failed to remove file %s: %s", file, strerror (errno));
    return FALSE;
  }
  return TRUE;
}

/* Checks for the existance of a PID file */
char *
pid_file_exists (char *file)
{

  char *pid = (char *) calloc (1, MAX_PID_LEN);
  struct stat st;

  if (pid == NULL)
    return NULL;

  if ((stat (file, &st)) < 0) {
    free_single_pointer (pid);
    return NULL;
  }
  FILE *fp = fopen (cfg->pid_file, "r+");

  if (fp == NULL)
    suicide ("Failed to open PID file %s for writing: %s", cfg->pid_file, strerror (errno));
  if (fread (pid, 1, sizeof (pid), fp) <= 0 && ferror (fp)) {
    free_single_pointer (pid);
    return NULL;
  }

  strip_char (pid, '\n');
  return pid;
}

int
try_lock ()
{

  char *read_pid = pid_file_exists (cfg->pid_file);
  int pid_exists = FALSE;
  if (read_pid){
    pid_exists = TRUE;
    free_single_pointer(read_pid);
  }
  int lfp = open (cfg->pid_file, O_RDWR | O_CREAT, 0640);

  if (lfp < 0){
    return FALSE;
  }

  if (lockf (lfp, F_TLOCK, 0) < 0) {
    close (lfp);
    return FALSE;
  }

  if (lockf (lfp, F_ULOCK, 0) < 0)
    log_msg (LOG_WARNING, "Unlock failed on PID file %s", cfg->pid_file);

  if (pid_exists)
    log_msg(LOG_INFO, "Stale PID file detected - did %s crash?", PACKAGE_NAME);

  delete_local_file (cfg->pid_file);

  return TRUE;
}


int
get_lock ()
{

  char *read_pid;
  char pid[MAX_PID_LEN];
  int lfp;

  read_pid = pid_file_exists (cfg->pid_file);
  lfp = open (cfg->pid_file, O_RDWR | O_CREAT, 0640);

  if (lfp < 0)
    suicide ("Failed to open PID file %s for writing: %s", cfg->pid_file, strerror (errno));	/* can not open */

  if (lockf (lfp, F_TLOCK, 0) < 0) {
    log_msg (LOG_CRIT, "Another process with PID %s appears to already be running", read_pid ? read_pid : "(unknown)");
    free_single_pointer (read_pid);
    return FALSE;
  }

  if (ftruncate (lfp, 0) < 0) {
    free_single_pointer (read_pid);
    suicide ("Failed to truncate PID file %s: %s", cfg->pid_file, strerror (errno));
  }

  sprintf (pid, "%d\n", getpid ());
  if (write (lfp, pid, strlen (pid)) < 0) {	/* record pid to lockfile */
    free_single_pointer (read_pid);
    suicide ("Unable to write PID to PID file %s: %s", cfg->pid_file, strerror (errno));
  }

  free_single_pointer (read_pid);
  return TRUE;
}

void
daemonise ()
{

  if (!try_lock ()) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      suicide ("Another process appears to already be running");
    }
    suicide ("Unable to lock PID file %s: %s", cfg->pid_file, strerror (errno));
  }


  if (cfg->foreground) {
    if (!get_lock ()) {
      suicide (NULL);
    }
    return;
  }

  if (getppid () == 1)
    return;

  int fd;

  pid_t pid = fork ();

  if (pid < 0) {
    suicide ("fork() failed. Bailing...");
  }

  if (pid > 0)
    _exit (0);

  setsid ();

  if (!get_lock ()) {
    suicide (NULL);
  }

  close (fileno (stdin));
  close (fileno (stdout));
  close (fileno (stderr));
  fd = open ("/dev/null", O_RDWR);
  if (dup (fd) < 0)
    log_msg (LOG_ERR, "Failed to duplicate file-descriptor for stdout");
  if (dup (fd) < 0)
    log_msg (LOG_ERR, "Failed to duplicate file-descriptor for stderr");

  signal (SIGCHLD, SIG_IGN);
  signal (SIGTSTP, SIG_IGN);
  signal (SIGTTOU, SIG_IGN);
  signal (SIGTTIN, SIG_IGN);

  log_msg (LOG_DEBUG, "I've forked! New PID:  %d", pid);
}

/* Reads the PID file, and sends a kill signal to the process owning it */
void
terminate_process ()
{

  if (try_lock ()) {
    log_msg (LOG_INFO, "No running process was found");
    exit (EXIT_SUCCESS);
  }

  char *pid = pid_file_exists (cfg->pid_file);
  int _pid;

  if (!pid) {
    log_msg (LOG_INFO, "No running process was found");
    free_single_pointer (pid);
    exit (EXIT_SUCCESS);
  }

  if ((_pid = char_to_pos_int (pid)) < 0) {
    log_msg (LOG_WARNING, "Failed to obtain PID for running process");
    free_single_pointer (pid);
    exit (EXIT_FAILURE);
  }

  if (kill (_pid, SIGTERM) < 0)
    log_msg (LOG_ERR, "Failed to send signal to process %d: %s", _pid, strerror (errno));

  free_single_pointer (pid);
  log_msg (LOG_INFO, "Process %d terminated", _pid);
  exit (EXIT_SUCCESS);

}
