#include "ccfsync.h"
#include <sys/types.h>
#include <sys/inotify.h>

void *
handle_dir_move (void *data)
{

  struct move_thread_data *mtd = data;
  struct inotify_event *event = mtd->ev;
  struct move_event *me = mtd->me;
  GHashTable *watches = mtd->watches;
  int wd;
  unsigned int j, k;

  log_msg (LOG_DEBUG, "handle_dir_move thread spawned for dir: '%s'", mtd->tmp_path);

  pthread_mutex_lock (&move_events_mutex);
  for (k = 0; k < g_list_length (move_events); k++) {
    me = g_list_nth_data (move_events, k);
    if (me->cookie == event->cookie) {
      log_msg (LOG_INFO, "DIRECTORY MOVE: Directory '%s' has moved to '%s'", me->cf_name, mtd->cf_tmp_path);
    }
  }
  pthread_mutex_unlock (&move_events_mutex);

  /* Recursively get a list of all files in the directories below the one we're moving, and add them to the copy queue */
  GList *files_in_dir = get_cf_files_from_dir (me->cf_name, mtd->exclusions);
  for (j = 0; j < g_list_length (files_in_dir); j++) {

    gchar *file = g_list_nth_data (files_in_dir, j);
    cf_file_copy *cfc = malloc (sizeof (cf_file_copy));
    cfc->sentinel = g_strdup ("ok");
    cfc->old_name = g_strdup (file);
    gchar *tmp_base_name = file + strlen (me->cf_name);

    cfc->new_name = NULL;

    Sasprintf (cfc->new_name, "%s%s", mtd->cf_tmp_path, tmp_base_name);

    log_msg (LOG_DEBUG, "In handle_dir_move: Handling file with old_name = '%s' new_name = '%s', will put on files_to_copy queue", cfc->old_name, cfc->new_name);
    cfc->cf_file = build_cf_file_from_lf (cfc->old_name);
    g_async_queue_push (files_to_copy, cfc);
  }

  g_list_free_full (files_in_dir, free_single_pointer);

  /* Remove watch for the old name directory */
  pthread_mutex_lock (&watches_mutex);
  gint *tmp_wd = g_hash_table_lookup (watches, me->full_local_path);
  inotify_rm_watch (mtd->fd, GPOINTER_TO_INT (tmp_wd));
  g_hash_table_remove (watches, me->full_local_path);
  pthread_mutex_unlock (&watches_mutex);
  log_msg (LOG_DEBUG, "In handle_dir_move: Removed watch for '%s' with wd %d", me->full_local_path, GPOINTER_TO_INT (tmp_wd));

  /* Add watch for the newly moved directory */
  gint new_wd = inotify_add_watch (mtd->fd, mtd->tmp_path,
				   IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVE);
  pthread_mutex_lock (&watches_mutex);
  g_hash_table_insert (watches, g_strdup (mtd->tmp_path), GINT_TO_POINTER (new_wd));
  pthread_mutex_unlock (&watches_mutex);
  log_msg (LOG_DEBUG, "In handle_dir_move: Added watch for '%s' with wd %d", mtd->tmp_path, (int) new_wd);



  /* Delete sub-directory watches of the no longer existing directory and
   * Re-add inotify watches for subdirectories of the directory we just created (moved to)
   * So the watch for /old/dir/ will point to /new/dir/
   */
  printf ("Get sub_dirs from '%s'\n", mtd->tmp_path);


  GList *sub_dirs = get_dirs (mtd->tmp_path, mtd->tmp_path);

  for (j = 0; j < g_list_length (sub_dirs); j++) {

    /* Get name of "new" directory ie. /new/dir/ */
    gchar *tmp_dir = g_strdup ((gchar *) g_list_nth_data (sub_dirs, j));

    /* Delete corresponding watch in the previous dir which no longer exists ie. /old/dir/ */
    gchar *old_tmp_dir = NULL;
    Sasprintf (old_tmp_dir, "%s%s", me->full_local_path, tmp_dir + strlen (mtd->tmp_path));


    log_msg (LOG_DEBUG, "In handle_dir_move: Removing watch on '%s' which had wd = %d", old_tmp_dir, mtd->fd);
    pthread_mutex_lock (&watches_mutex);
    gint *tmp_wd_n = g_hash_table_lookup (watches, old_tmp_dir);
    if ((inotify_rm_watch (mtd->fd, GPOINTER_TO_INT (tmp_wd_n))) < 0)
      log_msg (LOG_ERR, "In handle_dir_move: Failed to remove inotify watch from directory '%s' : %s", old_tmp_dir, strerror (errno));

    g_hash_table_remove (watches, old_tmp_dir);
    free_single_pointer (old_tmp_dir);

    /* Add watch for the "new" sub-directory */
    if ((wd = inotify_add_watch (mtd->fd, tmp_dir, IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVE)) < 0) {
      log_msg (LOG_ERR, "In handle_dir_move: Failed to add watch on directory '%s' : %s Possible race condition hit!", tmp_dir, strerror (errno));
    }
    else {
      log_msg (LOG_DEBUG, "In handle_dir_move: Adding inotify watch for new sub-directory: '%s'  wd = %d", tmp_dir, wd);
      g_hash_table_insert (watches, tmp_dir, GINT_TO_POINTER (wd));
    }
    pthread_mutex_unlock (&watches_mutex);
//    free_single_pointer(tmp_dir);
  }
  g_list_free_full (sub_dirs, free_single_pointer);

  pthread_mutex_lock (&move_events_mutex);
  move_events = g_list_remove (move_events, me);
  pthread_mutex_unlock (&move_events_mutex);

  destroy_move_event (me);

  log_msg (LOG_DEBUG, "Directory successfully moved: %s", mtd->tmp_path);
  free_single_pointer (mtd->tmp_path);
  free_single_pointer (mtd->cf_tmp_path);
  free_single_pointer (mtd);
  return NULL;
}
