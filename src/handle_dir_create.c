#include "ccfsync.h"
#include <sys/inotify.h>

void *
handle_dir_create (void *data)
{
  /* Give any reasonably sized cp -rf chance to finish */
  sleep (1);
  struct move_thread_data *mtd = data;
  GHashTable *watches = mtd->watches;
  log_msg (LOG_DEBUG, "In handle_dir_create, dir created: '%s'", mtd->tmp_path);

  /* Get list of files in the newly created directory, and a list of files from CF and 
   * compare
   */
  GHashTable *files_in_dir = list_files_local (mtd->tmp_path, cfg->monitor_dir, mtd->exclusions);
  /* Add inotify watch to new directory asap */
  int tmp = inotify_add_watch (mtd->fd, mtd->tmp_path, mtd->events_mask);
  if (tmp < 0)
    log_msg (LOG_ERR, "In handle_dir_create: Failed to set watch on '%s': %s Possible race condition hit!", mtd->tmp_path, strerror (errno));
  g_hash_table_insert (watches, g_strdup (mtd->tmp_path), GINT_TO_POINTER (tmp));
  pthread_mutex_unlock (&watches_mutex);

  add_watches_recursively (mtd->tmp_path, mtd->fd, &watches, mtd->events_mask);

  /* Nothing to do here */
  if (g_hash_table_size (files_in_dir) == 0) {
    pthread_exit (EXIT_SUCCESS);
  }

  GHashTable *cf_files = g_hash_table_new (g_str_hash, g_str_equal);
  list_files_cf (&cf_files, NULL, mtd->exclusions);

  /* compare_remote uploads files from new directory */
  GList *to_be_free_lf = compare_remote (files_in_dir, cf_files);
  free_lfs (to_be_free_lf, files_in_dir);

  // This is a NOOP in our case as far as CF is concerned
  printf ("The directory %s was created.\n", mtd->tmp_path);
  g_hash_table_destroy (files_in_dir);
  g_hash_table_destroy (cf_files);
  free_single_pointer (mtd->tmp_path);
  free_single_pointer (mtd->cf_tmp_path);
  free_single_pointer (mtd);

  pthread_exit (EXIT_SUCCESS);


}
