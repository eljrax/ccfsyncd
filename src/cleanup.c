#include "ccfsync.h"

void
destroy_move_event (struct move_event *me)
{
  free_single_pointer (me->cf_name);
  free_single_pointer (me->event_name);
  free_single_pointer (me->full_local_path);
  free_single_pointer (me);

}

void
destroy_logging ()
{
  if (log_fp != NULL)
    fclose (log_fp);
  if (cfg->syslog)
    closelog ();
}

void
destroy_exclusions (struct exclusions *exclusions)
{
  if (!cfg->exclusion_file)
    return;
  int i;

  for (i = 0; i < exclusions->len; i++) {
    regfree (exclusions->exex[i]);
    free_single_pointer (exclusions->exex[i]);
  }
  free_single_pointer (exclusions->exex);
  free_single_pointer (exclusions);
}

void
free_lfs (GList * to_be_free, GHashTable * local)
{
  int i;
  for (i = 0; i < (int) g_list_length (to_be_free); i++) {
    gchar *hash_key = (gchar *) g_list_nth_data (to_be_free, i);
    local_file *lf = g_hash_table_lookup (local, hash_key);
    log_msg (LOG_MEMDEBUG, "in free_lfs: Killing '%s'", lf->cf_name);
    destroy_local_file (lf);
    g_hash_table_remove (local, hash_key);
  }
}

void
destroy_config ()
{
  free_single_pointer (cfg->auth_endpoint);
  free_single_pointer (cfg->api_username);
  free_single_pointer (cfg->api_key);
  free_single_pointer (cfg->region);
  free_single_pointer (cfg->container);
  free_single_pointer (cfg->monitor_dir);
  free_single_pointer (cfg->log_file);
}

/* Free all memory related to a file copy event 
 * Note that the cf_file member is free():d at the point
 * where it's deleted in the delete thread. 
 */
void
destroy_cf_file_copy (gpointer item)
{
  cf_file_copy *cfc = item;
  free_single_pointer (cfc->old_name);
  free_single_pointer (cfc->new_name);
  free_single_pointer (cfc->sentinel);
  free_single_pointer (cfc);
}


void
destroy_local_file (gpointer item)
{
  local_file *lf = (local_file *) item;
  if (lf == NULL){
    log_msg(LOG_DEBUG, "Got passed a NULL lf object to free!");
    return;
  }
  log_msg (LOG_MEMDEBUG, "Freeing local_file : %s", (char *) lf->cf_name);
  free_single_pointer (lf->cf_name);
  free_single_pointer (lf->st);
  free_single_pointer (lf->hash);
  free_single_pointer (lf->sentinel);
  free_single_pointer (lf->name);
  free_single_pointer (item);
}

void
destroy_cf_file (gpointer item, gpointer user_data)
{
  cf_file *f = item;
  if (f == NULL)
    return;
  log_msg (LOG_MEMDEBUG, "Freeing cf_file: '%s' - %s", f->content_type, f->hash);
  free_single_pointer (f->content_type);
  free_single_pointer (f->hash);
  free_single_pointer (f->local_path);
  free_single_pointer (f->sentinel);
  free_single_pointer (item);
  /* When we build a dummy object (misc.c), we need to free the name as well, since that's not a key in any hash maps or lists 
   * delete_file() calls this
   */
  if (user_data != NULL)
    free_single_pointer (user_data);
}

void
free_single_pointer (gpointer item)
{
  /* We free a *lot* of stuff, only uncomment this if you know what you're doing.
   * Make sure you redirect stdout to a file. You have been warned
   */ 
//  log_msg (LOG_MEMDEBUG, "Freeing single pointer: '%s'", (char *) item);
  free (item);
}

void
cleanup_globals ()
{
  g_async_queue_unref (files_to_delete);
  g_async_queue_unref (files_to_copy);
  g_list_free_full (files_being_uploaded, (GDestroyNotify) free_single_pointer);
  free_single_pointer (auth->token);
  free_single_pointer (auth->endpoint);
  free_single_pointer (auth->token_header);
  free_single_pointer (auth);
  curl_global_cleanup ();
  pthread_mutex_destroy (&files_being_uploaded_mutex);
  pthread_mutex_destroy (&move_events_mutex);
  pthread_mutex_destroy (&auth_in_progress_mutex);
  return;
}
