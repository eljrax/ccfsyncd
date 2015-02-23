#include "ccfsync.h"

void
signal_ignore (int sig)
{
  log_msg (LOG_DEBUG, "Ignoring signal %d", sig);
  return;

}

void
signal_handler (int sig)
{


  if (!threaded) {
    exit(EXIT_SUCCESS);
  }

  if (exiting) {
    return;
  }
  exiting = TRUE;

  /* We need to create a full-blown object here, since the cleanup functions 
   * will throw a fit if we don't 
   */
  int i;

  if (sig == SIGINT || sig == SIGTERM) {
    drain_queue (files_to_upload);
    drain_queue (files_to_delete);
    drain_queue (files_to_copy);

    /* Kill the upload threads */
    for (i = 0; i < cfg->num_upload_threads; i++) {
      local_file *lf = malloc (sizeof (local_file));
      lf->st = malloc (sizeof (struct stat));
      lf->cf_name = g_strdup ("dummy");
      lf->hash = g_strdup ("dummy");
      lf->name = g_strdup ("dummy");
      lf->sentinel = g_strdup ("exit");
      g_async_queue_push (files_to_upload, lf);
    }

    /* Kill the delete threads */
    for (i = 0; i < cfg->num_delete_threads; i++) {
      cf_file *cf = malloc (sizeof (cf_file));
      cf->sentinel = g_strdup ("exit");
      cf->content_type = g_strdup ("dummy");
      cf->hash = g_strdup ("dummy");
      cf->local_path = g_strdup ("dummy");
      g_async_queue_push (files_to_delete, cf);
    }

    /* Kill the copy threads */
    for (i = 0; i < cfg->num_copy_threads; i++) {
      cf_file_copy *cfc = malloc (sizeof (cf_file_copy));
      cfc->old_name = g_strdup ("dummy");
      cfc->new_name = g_strdup ("dummy");
      cfc->sentinel = g_strdup ("exit");
      cfc->cf_file = NULL;
      g_async_queue_push (files_to_copy, cfc);
    }
  }


  log_msg (LOG_DEBUG, "Caught signal %d\n", sig);

  /* Once all the threads which are joined from main() dies, we'll quit */

}
